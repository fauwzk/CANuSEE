#include "obd_ble.h"

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include "app_state.h"
#include "config.h"

// UUID du service BLE exposé par les adaptateurs OBD "OBDBLE" génériques.
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");

static bool doConnect = false;
bool bleConnected = false;
static bool doScan = false;
static bool scanIsRunning = false;
static BLERemoteCharacteristic *pTxCharacteristic = nullptr;
static BLERemoteCharacteristic *pRxCharacteristic = nullptr;
static BLEAdvertisedDevice *myDevice = nullptr;

String bleStatusStr = "Scanning...";
String elmBuffer = "";
static bool elmResponseReady = false;

uint8_t elmInitStep = 0;
static unsigned long lastElmRequest = 0;
static uint8_t currentExpectedPID = 0;
uint32_t packetsReceived = 0;

// Valeurs brutes issues du bus OBD (aucun lissage).
float mapPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0, engineRPM = 0.0;
float coolantTemp = 0.0, turboPressureState = 0.0, targetBoost = -1000.0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashRPM = 0, dashLoad = 0;

bool timerRunning = false, timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0, currentSpeed = 0.0;

static void scanCompleteCB(BLEScanResults results)
{
    scanIsRunning = false;
    BLEDevice::getScan()->clearResults();
}

// Callback de notification BLE : accumule les caractères jusqu'au '>' qui
// termine chaque réponse ELM327 (le protocole AT est un simple texte).
static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    packetsReceived++;
    for (int i = 0; i < length; i++)
    {
        char c = (char)pData[i];
        if (c == '>')
            elmResponseReady = true;
        else if (c != '\r' && c != '\n' && c != ' ')
            elmBuffer += c;
    }
}

class MyClientCallback : public BLEClientCallbacks
{
    void onConnect(BLEClient *pclient)
    {
        bleConnected = true;
        bleStatusStr = "Connected!";
    }
    void onDisconnect(BLEClient *pclient)
    {
        bleConnected = false;
        bleStatusStr = "Disconnected";
        elmInitStep = 0;
        doScan = true;
        if (currentState == STATE_GAUGES)
            currentState = STATE_CONNECTING;
    }
};

static bool connectToServer()
{
    bleStatusStr = "Connecting...";
    BLEClient *pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(myDevice))
        return false;

    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
        pClient->disconnect();
        return false;
    }

    std::map<std::string, BLERemoteCharacteristic *> *pChars = pRemoteService->getCharacteristics();
    for (auto const &pair : *pChars)
    {
        BLERemoteCharacteristic *pChar = pair.second;
        if (pChar->canWrite() || pChar->canWriteNoResponse())
            pTxCharacteristic = pChar;
        if (pChar->canNotify())
        {
            pRxCharacteristic = pChar;
            pRxCharacteristic->registerForNotify(notifyCallback);
            uint8_t enableValue[] = {0x01, 0x00};
            BLERemoteDescriptor *pDesc = pChar->getDescriptor(BLEUUID((uint16_t)0x2902));
            if (pDesc != nullptr)
                pDesc->writeValue(enableValue, 2, true);
        }
    }

    if (pTxCharacteristic != nullptr && pRxCharacteristic != nullptr)
    {
        bleStatusStr = "Init ELM327...";
        elmInitStep = 0;
        elmBuffer = "";
        elmResponseReady = false;
        return true;
    }

    bleStatusStr = "RX/TX Not Found";
    pClient->disconnect();
    return false;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        String name = advertisedDevice.getName().c_str();
        name.toUpperCase();
        if (name == "OBDBLE" || name.indexOf("OBD") != -1)
        {
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = false;
            scanIsRunning = false;
        }
    }
};

static void sendELMCommand(String cmd)
{
    cmd += "\r";
    if (bleConnected && pTxCharacteristic != nullptr)
    {
        pTxCharacteristic->writeValue(cmd.c_str(), cmd.length());
        lastElmRequest = millis();
        elmBuffer = "";
        elmResponseReady = false;
    }
}

// Décode la réponse "41 XX AA BB..." d'un PID Mode 01 et met à jour la
// variable globale correspondante. Formules issues de la norme OBD-II.
static void parseOBDResponse(String response, uint8_t pid)
{
    String searchStr = "41";
    if (pid < 0x10)
        searchStr += "0";
    searchStr += String(pid, HEX);
    searchStr.toUpperCase();
    response.toUpperCase();

    int idx = response.indexOf(searchStr);
    if (idx != -1 && response.length() >= idx + 6)
    {
        String byteAStr = response.substring(idx + 4, idx + 6);
        long A = strtol(byteAStr.c_str(), NULL, 16);
        long B = 0;
        if (response.length() >= idx + 8)
        {
            String byteBStr = response.substring(idx + 6, idx + 8);
            B = strtol(byteBStr.c_str(), NULL, 16);
        }

        switch (pid)
        {
        case 0x04:
            engineLoad = (A * 100.0) / 255.0;
            dashLoad = engineLoad;
            break;
        case 0x05:
            coolantTemp = A - 40;
            dashCoolant = coolantTemp;
            break;
        case 0x0B:
            mapPressure = A;
            turboPressureState = (A - 100.0) * 0.01;
            if (turboPressureState < 0)
                turboPressureState = 0;
            dashBoost = turboPressureState;
            break;
        case 0x0C:
            engineRPM = ((A * 256.0) + B) / 4.0;
            dashRPM = engineRPM;
            break;
        case 0x0D:
            if (A == 0)
            {
                timerReady = true;
                timerRunning = false;
            }
            else if (A > 0 && A < TARGET_SPEED && timerReady && !timerRunning)
            {
                speedTimerStart = millis();
                timerRunning = true;
                timerReady = false;
            }
            else if (A >= TARGET_SPEED && timerRunning)
            {
                lastTimerValue = (millis() - speedTimerStart) / 1000.0;
                timerRunning = false;
                timerReady = false;
            }
            currentSpeed = A;
            break;
        case 0x0F:
            intakeTemp = A - 40;
            dashIAT = intakeTemp;
            break;
        case 0x10:
            mafPressure = ((A * 256.0) + B) / 100.0;
            break;
        case 0x70:
            targetBoost = (((A * 256.0) + B) * 0.03125) * 0.01 - 1.0;
            if (targetBoost < 0)
                targetBoost = 0;
            break;
        }
    }
}

// Choisit le prochain PID à interroger selon l'écran affiché, pour ne
// jamais demander plus de données que ce que l'écran courant affiche.
static uint8_t getNextSmartPID()
{
    static uint8_t dashStep = 0, boostStep = 0, airStep = 0;
    switch (screenIndex)
    {
    case 0:
        airStep = !airStep;
        return airStep ? 0x0B : 0x10;
    case 1:
        boostStep = !boostStep;
        return boostStep ? 0x0B : 0x70;
    case 2:
        return 0x0F;
    case 3:
        return 0x04;
    case 4:
        return 0x05;
    case 5:
        dashStep = (dashStep + 1) % 4;
        if (dashStep == 0)
            return 0x0B;
        if (dashStep == 1)
            return 0x0F;
        if (dashStep == 2)
            return 0x05;
        if (dashStep == 3)
            return 0x0C;
    case 6:
        return 0x0D;
    case 7:
        return 0x0D;
    default:
        return 0x0C;
    }
}

void initBLE()
{
    WiFi.mode(WIFI_OFF); // Le BLE et le WiFi partagent le radio : on coupe le WiFi hors portail de config.
    BLEDevice::init("CANuSEE");
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    doScan = true;
}

void processBLE()
{
    if (doConnect == true)
    {
        if (!connectToServer())
            bleStatusStr = "Conn Fail";
        doConnect = false;
    }

    if (bleConnected)
    {
        bool triggerNextRequest = false;
        // L'étape ATSP0 (auto-détection du protocole) est plus lente que les autres.
        unsigned long timeoutLimit = (elmInitStep == 4) ? 1500 : 500;
        if (!elmResponseReady && (millis() - lastElmRequest > timeoutLimit))
            triggerNextRequest = true;

        if (elmResponseReady)
        {
            if (elmInitStep < 5)
            {
                elmInitStep++;
                if (elmInitStep == 5 && currentState == STATE_CONNECTING)
                    currentState = STATE_GAUGES;
            }
            else
            {
                parseOBDResponse(elmBuffer, currentExpectedPID);
            }
            elmBuffer = "";
            elmResponseReady = false;
            triggerNextRequest = true;
        }

        if (triggerNextRequest)
        {
            // Séquence d'initialisation ELM327 : reset, echo off, linefeed off,
            // espaces off, puis auto-détection du protocole. Ensuite, polling continu des PID.
            if (elmInitStep == 0)
                sendELMCommand("ATZ");
            else if (elmInitStep == 1)
                sendELMCommand("ATE0");
            else if (elmInitStep == 2)
                sendELMCommand("ATL0");
            else if (elmInitStep == 3)
                sendELMCommand("ATS0");
            else if (elmInitStep == 4)
                sendELMCommand("ATSP0");
            else
            {
                currentExpectedPID = getNextSmartPID();
                String cmd = "01";
                if (currentExpectedPID < 0x10)
                    cmd += "0";
                cmd += String(currentExpectedPID, HEX);
                sendELMCommand(cmd);
            }
        }
    }
    else if (doScan)
    {
        if (!scanIsRunning)
        {
            scanIsRunning = true;
            BLEDevice::getScan()->start(2, scanCompleteCB, false);
        }
    }
}
