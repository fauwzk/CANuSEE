#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <DNSServer.h>
#include "epd_bitmap_logo_3008.h"
#include <ElegantOTA.h>
#include "version.h" // Inclut dynamiquement la version via PlatformIO CI

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 3
#define BTN_MENU 6

const char *ssid = "CANuSEE_Config";
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

struct Settings
{
    int last_screen;
    int boost_screen_type;
    float turbo_min;
    float turbo_max;
    int engload_screen_type;
    int coolant_screen_type;
    int intake_temp_screen_type;
    int tick_line_gauge;
    int target_speed;
    int brightness;
};

#define EEPROM_SIZE sizeof(Settings)
Settings cfg;

int BOOST_SCREEN = 0, ENGLOAD_SCREEN = 0, COOLANT_SCREEN = 0, IAT_SCREEN = 0;
int TICK_LINE_GAUGE = 2, TARGET_SPEED = 100, OLED_BRIGHTNESS = 255;

enum AppState
{
    STATE_CONNECTING,
    STATE_GAUGES,
    STATE_MENU,
    STATE_STYLE_MENU,
    STATE_EDIT_MIN,
    STATE_EDIT_MAX,
    STATE_EDIT_SPEED,
    STATE_EDIT_BRIGHTNESS,
    STATE_CONFIG
};
AppState currentState = STATE_CONNECTING;
bool ota_updating = false;
String version_string = "CANuSEE " FW_VERSION;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);
const int centerX = 64;
const int screenNumbers = 9;
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7, TURBO_MAX_BAR = 1.5;

// Variables brutes (Aucun lissage)
float mapPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0, engineRPM = 0.0;
float coolantTemp = 0.0, turboPressureState = 0.0, targetBoost = -1000.0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashRPM = 0, dashLoad = 0;

bool timerRunning = false, timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0, currentSpeed = 0.0;

bool isTransitioning = false;
int slideOffset = 0;
int slideDirection = 1;
uint8_t oldScreenBuffer[1024];

enum IconType
{
    ICON_EXIT,
    ICON_GEAR,
    ICON_SUN,
    ICON_GAUGE,
    ICON_TURBO,
    ICON_TEMP,
    ICON_ENGINE,
    ICON_TIMER,
    ICON_BLE,
    ICON_DASH,
    ICON_SLIDERS,
    ICON_AIR
};

#define MAX_MENU_ITEMS 24
struct MenuItem
{
    const char *text;
    int action;
    int iconType;
};

MenuItem currentMenu[MAX_MENU_ITEMS];
int menuSize = 0, menuCursor = 0;

#define ACT_CLOSE 0
#define ACT_OPEN_STYLE_MENU 1
#define ACT_EDIT_MIN 2
#define ACT_EDIT_MAX 3
#define ACT_EDIT_SPEED 4
#define ACT_EDIT_BRIGHTNESS 5
#define ACT_ENTER_CONFIG 6
#define ACT_GO_SCREEN_0 10
#define ACT_BACK_TO_MENU 30
#define ACT_SET_STYLE_TEXT 31
#define ACT_SET_STYLE_GRAPH 32
#define ACT_SET_STYLE_DIAL 33
#define ACT_SET_STYLE_BAR 34

const char *screenNames[] = {"MAP/MAF", "Boost", "IAT", "Load", "Coolant", "Dash", "Timer", "Speed", "BLE"};

class Button
{
public:
    uint8_t pin;
    bool state;
    bool lastState;
    unsigned long lastDebounceTime;

    Button(uint8_t p)
    {
        pin = p;
        state = false;
        lastState = false;
        lastDebounceTime = 0;
    }

    bool pressed()
    {
        bool reading = (digitalRead(pin) == LOW);
        if (reading != lastState)
            lastDebounceTime = millis();
        lastState = reading;
        if ((millis() - lastDebounceTime) > 50)
        {
            if (reading != state)
            {
                state = reading;
                return state;
            }
        }
        return false;
    }
};

Button btnUp(BTN_UP);
Button btnDown(BTN_DOWN);
Button btnOk(BTN_OK);
Button btnMenu(BTN_MENU);

static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static boolean scanIsRunning = false;
static BLERemoteCharacteristic *pTxCharacteristic = nullptr;
static BLERemoteCharacteristic *pRxCharacteristic = nullptr;
static BLEAdvertisedDevice *myDevice;

String bleStatusStr = "Scanning...";
String elmBuffer = "";
bool elmResponseReady = false;

int elmInitStep = 0;
unsigned long lastElmRequest = 0;
uint8_t currentExpectedPID = 0;
uint32_t packetsReceived = 0;

void scanCompleteCB(BLEScanResults results)
{
    scanIsRunning = false;
    BLEDevice::getScan()->clearResults();
}

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
        connected = true;
        bleStatusStr = "Connected!";
    }
    void onDisconnect(BLEClient *pclient)
    {
        connected = false;
        bleStatusStr = "Disconnected";
        elmInitStep = 0;
        doScan = true;
        if (currentState == STATE_GAUGES)
            currentState = STATE_CONNECTING;
    }
};

bool connectToServer()
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

void sendELMCommand(String cmd)
{
    cmd += "\r";
    if (connected && pTxCharacteristic != nullptr)
    {
        pTxCharacteristic->writeValue(cmd.c_str(), cmd.length());
        lastElmRequest = millis();
        elmBuffer = "";
        elmResponseReady = false;
    }
}

void parseOBDResponse(String response, uint8_t pid)
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

uint8_t getNextSmartPID()
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

void processBLE()
{
    if (doConnect == true)
    {
        if (!connectToServer())
            bleStatusStr = "Conn Fail";
        doConnect = false;
    }

    if (connected)
    {
        bool triggerNextRequest = false;
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

void setOledBrightness(uint8_t b) { u8g2.setContrast(b); }
void drawStringCenter(int y, String text)
{
    int w = u8g2.getStrWidth(text.c_str());
    u8g2.setCursor(centerX - (w / 2), y);
    u8g2.print(text);
}
void drawStringLeft(int x, int y, String text)
{
    u8g2.setCursor(x, y);
    u8g2.print(text);
}
void drawStringRight(int x, int y, String text)
{
    int w = u8g2.getStrWidth(text.c_str());
    u8g2.setCursor(x - w, y);
    u8g2.print(text);
}

void drawVectorIcon(int cx, int cy, int type)
{
    switch (type)
    {
    case ICON_EXIT: // Porte ouverte + Flèche
        u8g2.drawFrame(cx - 14, cy - 16, 14, 32);
        u8g2.drawBox(cx - 11, cy - 13, 8, 26);
        u8g2.drawLine(cx - 2, cy, cx + 14, cy);
        u8g2.drawTriangle(cx + 6, cy - 6, cx + 16, cy, cx + 6, cy + 6);
        break;
    case ICON_GEAR: // Rouage / Paramètres
        u8g2.drawDisc(cx, cy, 12);
        u8g2.drawBox(cx - 4, cy - 16, 8, 32);
        u8g2.drawBox(cx - 16, cy - 4, 32, 8);
        u8g2.drawBox(cx - 12, cy - 12, 24, 24);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy, 6);
        u8g2.setDrawColor(1);
        break;
    case ICON_SUN: // Luminosité
        u8g2.drawDisc(cx, cy, 8);
        u8g2.drawLine(cx, cy - 12, cx, cy - 18);
        u8g2.drawLine(cx, cy + 12, cx, cy + 18);
        u8g2.drawLine(cx - 12, cy, cx - 18, cy);
        u8g2.drawLine(cx + 12, cy, cx + 18, cy);
        u8g2.drawLine(cx - 8, cy - 8, cx - 13, cy - 13);
        u8g2.drawLine(cx + 8, cy + 8, cx + 13, cy + 13);
        u8g2.drawLine(cx - 8, cy + 8, cx - 13, cy + 13);
        u8g2.drawLine(cx + 8, cy - 8, cx + 13, cy - 13);
        break;
    case ICON_GAUGE: // Compteur de vitesse / Cadran
        u8g2.drawCircle(cx, cy + 8, 18);
        u8g2.setDrawColor(0);
        u8g2.drawBox(cx - 20, cy + 8, 40, 20);
        u8g2.setDrawColor(1);
        u8g2.drawLine(cx - 18, cy + 8, cx + 18, cy + 8);
        u8g2.drawLine(cx, cy + 8, cx + 12, cy - 4); // Aiguille
        u8g2.drawDisc(cx, cy + 8, 3);
        break;
    case ICON_TURBO: // Escargot de turbo parfait
        u8g2.drawDisc(cx, cy + 2, 14);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy + 2, 8);
        u8g2.setDrawColor(1);
        u8g2.drawDisc(cx, cy + 2, 3);
        u8g2.drawBox(cx + 4, cy - 14, 12, 12);
        u8g2.drawLine(cx, cy + 2, cx + 5, cy - 3);
        u8g2.drawLine(cx, cy + 2, cx - 5, cy - 3);
        u8g2.drawLine(cx, cy + 2, cx - 5, cy + 7);
        break;
    case ICON_TEMP: // Thermomètre
        u8g2.drawFrame(cx - 5, cy - 16, 10, 26);
        u8g2.drawDisc(cx, cy + 10, 9);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy + 10, 6);
        u8g2.drawLine(cx, cy + 7, cx, cy - 12);
        u8g2.setDrawColor(1);
        u8g2.drawDisc(cx, cy + 10, 3);
        u8g2.drawLine(cx, cy + 10, cx, cy - 4);
        u8g2.drawLine(cx + 6, cy - 8, cx + 10, cy - 8);
        u8g2.drawLine(cx + 6, cy - 2, cx + 10, cy - 2);
        break;
    case ICON_ENGINE: // Bloc moteur V4 stylisé
        u8g2.drawBox(cx - 14, cy - 4, 28, 18);
        u8g2.drawBox(cx - 10, cy - 12, 8, 8);
        u8g2.drawBox(cx + 2, cy - 12, 8, 8);
        u8g2.drawDisc(cx - 16, cy + 6, 5);
        u8g2.drawDisc(cx + 16, cy + 6, 5);
        u8g2.drawLine(cx - 16, cy + 11, cx + 16, cy + 11);
        break;
    case ICON_TIMER: // Chrono 0-100
        u8g2.drawCircle(cx, cy + 2, 16);
        u8g2.drawBox(cx - 4, cy - 18, 8, 4);
        u8g2.drawLine(cx + 11, cy - 9, cx + 16, cy - 14);
        u8g2.drawLine(cx, cy + 2, cx, cy - 10);
        break;
    case ICON_BLE: // Logo Bluetooth
        u8g2.drawLine(cx, cy - 16, cx, cy + 16);
        u8g2.drawLine(cx, cy - 16, cx + 10, cy - 6);
        u8g2.drawLine(cx + 10, cy - 6, cx - 10, cy + 6);
        u8g2.drawLine(cx, cy + 16, cx + 10, cy + 6);
        u8g2.drawLine(cx + 10, cy + 6, cx - 10, cy - 6);
        break;
    case ICON_DASH: // Tableau de bord 4 cases
        u8g2.drawFrame(cx - 16, cy - 16, 14, 14);
        u8g2.drawFrame(cx + 2, cy - 16, 14, 14);
        u8g2.drawFrame(cx - 16, cy + 2, 14, 14);
        u8g2.drawFrame(cx + 2, cy + 2, 14, 14);
        u8g2.drawBox(cx - 14, cy + 4, 10, 10);
        break;
    case ICON_SLIDERS: // Réglages Min / Max
        u8g2.drawLine(cx - 14, cy - 8, cx + 14, cy - 8);
        u8g2.drawBox(cx - 8, cy - 14, 6, 12);
        u8g2.drawLine(cx - 14, cy + 8, cx + 14, cy + 8);
        u8g2.drawBox(cx + 2, cy + 2, 6, 12);
        break;
    case ICON_AIR: // Filtre à air MAP/MAF
        u8g2.drawFrame(cx - 12, cy - 14, 24, 28);
        for (int i = -10; i <= 10; i += 5)
            u8g2.drawLine(cx - 12, cy + i, cx + 12, cy + i);
        break;
    }
}

String generateWebPage()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
        return "<html><body><h3>File not found</h3></body></html>";
    String html = file.readString();
    file.close();
    return html;
}

void saveValues()
{
    cfg.last_screen = screenIndex;
    cfg.boost_screen_type = BOOST_SCREEN;
    cfg.turbo_min = TURBO_MIN_BAR;
    cfg.turbo_max = TURBO_MAX_BAR;
    cfg.engload_screen_type = ENGLOAD_SCREEN;
    cfg.coolant_screen_type = COOLANT_SCREEN;
    cfg.intake_temp_screen_type = IAT_SCREEN;
    cfg.tick_line_gauge = TICK_LINE_GAUGE;
    cfg.target_speed = TARGET_SPEED;
    cfg.brightness = OLED_BRIGHTNESS;
    EEPROM.put(0, cfg);
    EEPROM.commit();
}

void loadValues()
{
    EEPROM.get(0, cfg);
    screenIndex = (cfg.last_screen >= 0 && cfg.last_screen < screenNumbers) ? cfg.last_screen : 0;
    BOOST_SCREEN = constrain(cfg.boost_screen_type, 0, 3);
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = constrain(cfg.engload_screen_type, 0, 3);
    COOLANT_SCREEN = constrain(cfg.coolant_screen_type, 0, 3);
    IAT_SCREEN = constrain(cfg.intake_temp_screen_type, 0, 3);
    TICK_LINE_GAUGE = (cfg.tick_line_gauge > 0) ? cfg.tick_line_gauge : 2;
    TARGET_SPEED = constrain(cfg.target_speed, 10, 300);
    OLED_BRIGHTNESS = constrain(cfg.brightness, 0, 255);
}

void restart_ESP()
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvR12_tr);
    drawStringCenter(35, "REBOOTING!");
    u8g2.sendBuffer();
    delay(1000);
    ESP.restart();
}

void buildMenu()
{
    menuSize = 0;

    if (screenIndex >= 1 && screenIndex <= 4)
        currentMenu[menuSize++] = {"Gauge Style ->", ACT_OPEN_STYLE_MENU, ICON_GAUGE};

    currentMenu[menuSize++] = {"Brightness", ACT_EDIT_BRIGHTNESS, ICON_SUN};

    if (screenIndex == 1)
    {
        currentMenu[menuSize++] = {"Turbo Min", ACT_EDIT_MIN, ICON_SLIDERS};
        currentMenu[menuSize++] = {"Turbo Max", ACT_EDIT_MAX, ICON_SLIDERS};
    }
    if (screenIndex == 6)
        currentMenu[menuSize++] = {"Target Speed", ACT_EDIT_SPEED, ICON_TIMER};

    currentMenu[menuSize++] = {"MAP/MAF", ACT_GO_SCREEN_0 + 0, ICON_AIR};
    currentMenu[menuSize++] = {"Boost", ACT_GO_SCREEN_0 + 1, ICON_TURBO};
    currentMenu[menuSize++] = {"IAT Temp", ACT_GO_SCREEN_0 + 2, ICON_TEMP};
    currentMenu[menuSize++] = {"Engine Load", ACT_GO_SCREEN_0 + 3, ICON_ENGINE};
    currentMenu[menuSize++] = {"Coolant", ACT_GO_SCREEN_0 + 4, ICON_TEMP};
    currentMenu[menuSize++] = {"Dashboard", ACT_GO_SCREEN_0 + 5, ICON_DASH};
    currentMenu[menuSize++] = {"0-100 Timer", ACT_GO_SCREEN_0 + 6, ICON_TIMER};
    currentMenu[menuSize++] = {"Speedometer", ACT_GO_SCREEN_0 + 7, ICON_GAUGE};
    currentMenu[menuSize++] = {"BLE Status", ACT_GO_SCREEN_0 + 8, ICON_BLE};

    currentMenu[menuSize++] = {"Mode Config", ACT_ENTER_CONFIG, ICON_GEAR};
    currentMenu[menuSize++] = {"Exit Menu", ACT_CLOSE, ICON_EXIT};
    menuCursor = 0;
}

void buildStyleMenu()
{
    menuSize = 0;
    currentMenu[menuSize++] = {"Text", ACT_SET_STYLE_TEXT, ICON_AIR};
    currentMenu[menuSize++] = {"Graph", ACT_SET_STYLE_GRAPH, ICON_TURBO};
    currentMenu[menuSize++] = {"Dial", ACT_SET_STYLE_DIAL, ICON_GAUGE};
    currentMenu[menuSize++] = {"Bar", ACT_SET_STYLE_BAR, ICON_SLIDERS};
    currentMenu[menuSize++] = {"<- Back", ACT_BACK_TO_MENU, ICON_EXIT};

    int currentType = 0;
    if (screenIndex == 1)
        currentType = BOOST_SCREEN;
    else if (screenIndex == 2)
        currentType = IAT_SCREEN;
    else if (screenIndex == 3)
        currentType = ENGLOAD_SCREEN;
    else if (screenIndex == 4)
        currentType = COOLANT_SCREEN;

    menuCursor = constrain(currentType, 0, 3);
}

void startScreenTransition()
{
    memcpy(oldScreenBuffer, u8g2.getBufferPtr(), 1024);
    isTransitioning = true;
    slideOffset = 0;
}

void draw_StatusBar(String title)
{
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, title);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringRight(128, 8, String(screenIndex + 1) + "/" + String(screenNumbers));
    // Ligne de séparation de l'OS élégante
    u8g2.drawLine(0, 10, 128, 10);
}

void drawMenuScreen()
{
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 8, String(menuCursor + 1) + "/" + String(menuSize));

    u8g2.drawFrame(124, 0, 4, 46);
    int scrollHeight = max(6, 46 / menuSize);
    int scrollY = 0;
    if (menuSize > 1)
        scrollY = (menuCursor * (46 - scrollHeight)) / (menuSize - 1);
    u8g2.drawBox(124, scrollY, 4, scrollHeight);

    int iconX = 61, iconY = 28;
    drawVectorIcon(iconX, iconY, currentMenu[menuCursor].iconType);

    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 48, 128, 16);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawStringCenter(61, currentMenu[menuCursor].text);
    u8g2.setDrawColor(1);
}

void drawEditScreen(String title, String valueStr, float progress)
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(14, title);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(36, valueStr);

    // Slider Bar Automatique
    u8g2.drawFrame(14, 42, 100, 6);
    int fillWidth = progress * 96;
    fillWidth = constrain(fillWidth, 0, 96);
    u8g2.drawBox(16, 44, fillWidth, 2);

    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 52, 128, 12);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(60, "U/D: Edit | OK: Save");
    u8g2.setDrawColor(1);
}

// Nouvel écran de connexion plus dynamique sans le texte moche
void drawConnectingScreen()
{
    u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);

    // Boîte noire au fond pour dégager l'UI
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 44, 128, 20);
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(52, bleStatusStr);

    u8g2.drawFrame(14, 56, 100, 6);

    if (connected)
    {
        // Barre de chargement de l'étape ELM327
        int fill = (elmInitStep / 5.0) * 96;
        if (fill > 0)
            u8g2.drawBox(16, 58, fill, 2);
    }
    else
    {
        // Animation fluide de balayage (K2000 style) pour "Searching"
        int width = 20;
        int max_x = 96 - width;
        int pos = (millis() / 15) % (max_x * 2);
        int xOffset = (pos < max_x) ? pos : (max_x * 2) - pos;
        u8g2.drawBox(16 + xOffset, 58, width, 2);
    }
}

void drawConfigScreen()
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(14, "MODE CONFIG");
    u8g2.drawLine(0, 18, 128, 18);

    // UI type Boîte de dialogue encadrée
    u8g2.drawFrame(10, 24, 108, 24);
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(36, "WiFi: " + String(ssid));
    drawStringCenter(45, "MDP: 12345678");

    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 52, 128, 12);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(60, "IP: 192.168.4.1");
    u8g2.setDrawColor(1);
}

#define AREA_CHART_HISTORY 94
struct AreaChartData
{
    double values[AREA_CHART_HISTORY];
    uint8_t currentIndex;
    bool initialized;
};
AreaChartData turboHistory = {{0}, 0, false}, loadHistory = {{0}, 0, false}, coolantHistory = {{0}, 0, false}, iatHistory = {{0}, 0, false};

String alignSign(String value) { return (!value.startsWith("-")) ? " " + value : value; }
String formatDecimal(double value, uint8_t decimals)
{
    String result = (value < 0) ? "-" : "";
    value = abs(value);
    result += String((int)value) + ".";
    double decPart = value - (int)value;
    for (int i = 0; i < decimals; i++)
    {
        decPart *= 10;
        int digit = (int)decPart;
        result += String(digit);
        decPart -= digit;
    }
    return result;
}

// Typographie ajustée pour éviter les dépassements (overflow) sur de longues valeurs
void draw_InfoText(String title, double value, String unit)
{
    draw_StatusBar(title);
    u8g2.setFont(u8g2_font_helvB18_tr); // Changé de 24 à 18
    String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
    drawStringCenter(48, valStr + " " + unit);
}

void draw_AreaChartWithHistory(AreaChartData &history, double newValue, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    double valToStore = constrain(newValue, minValue, maxValue);
    history.values[history.currentIndex] = valToStore;
    history.currentIndex = (history.currentIndex + 1) % AREA_CHART_HISTORY;
    if (history.currentIndex == 0)
        history.initialized = true;

    draw_StatusBar(label);
    int chartX = 32, chartY = 16, chartWidth = AREA_CHART_HISTORY, chartHeight = 44, baseY = chartY + chartHeight;
    u8g2.drawFrame(chartX, chartY, chartWidth, chartHeight);

    double range = maxValue - minValue;
    for (int i = 0; i < chartWidth; i++)
    {
        int historyIdx = (history.currentIndex + i) % AREA_CHART_HISTORY;
        if (!history.initialized && i >= history.currentIndex)
            break;
        double val = constrain(history.values[historyIdx], minValue, maxValue);
        int pixelHeight = (int)(chartHeight * ((val - minValue) / range));
        u8g2.drawLine(chartX + i, baseY, chartX + i, baseY - pixelHeight);
    }

    if (targetValue > -999.0)
    {
        double t_val = constrain(targetValue, minValue, maxValue);
        int ty = baseY - (int)(chartHeight * ((t_val - minValue) / range));
        for (int tx = chartX; tx < chartX + chartWidth; tx += 4)
            u8g2.drawPixel(tx, ty);
    }

    int alignBorderX = chartX - 2;
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringRight(alignBorderX, chartY + 8, alignSign(formatDecimal(maxValue, 1)));
    drawStringRight(alignBorderX, baseY, alignSign(formatDecimal(minValue, 1)));
    drawStringRight(alignBorderX, chartY + (chartHeight / 2) + 4, alignSign(formatDecimal(newValue, 1)));
}

void draw_LinearGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    draw_StatusBar(label);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(36, String(value, 1) + " " + unit);

    int barX = 4, barY = 42, barW = 120, barH = 12; // Légèrement remonté pour la safe zone
    u8g2.drawFrame(barX, barY, barW, barH);

    float val = constrain(value, minValue, maxValue);
    int segments = 23;
    int activeSegs = (int)(((val - minValue) / (maxValue - minValue)) * segments);
    for (int i = 0; i < activeSegs; i++)
        u8g2.drawBox(barX + 3 + (i * 5), barY + 3, 4, barH - 6);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 62, String(minValue, 1));
    drawStringRight(128, 62, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        float t_val = constrain(targetValue, minValue, maxValue);
        int t_x = barX + 2 + (int)(((t_val - minValue) / (maxValue - minValue)) * (barW - 4));
        u8g2.drawTriangle(t_x, barY - 1, t_x - 3, barY - 5, t_x + 3, barY - 5);
    }
}

void draw_RoundGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    draw_StatusBar(label);

    int cx = 64, cy = 64, r = 50;

    for (int i = 0; i <= 10; i++)
    {
        float a = PI - (i * PI / 10.0);
        int r_inner = r - ((i % 5 == 0) ? 6 : 3);
        u8g2.drawLine(cx + cos(a) * r, cy - sin(a) * r, cx + cos(a) * r_inner, cy - sin(a) * r_inner);
    }

    float val = constrain(value, minValue, maxValue);
    float angle = PI - ((val - minValue) / (maxValue - minValue)) * PI;
    int nx = cx + cos(angle) * (r - 8);
    int ny = cy - sin(angle) * (r - 8);
    u8g2.drawTriangle(nx, ny, cx + cos(angle + 0.15) * 6, cy - sin(angle + 0.15) * 6, cx + cos(angle - 0.15) * 6, cy - sin(angle - 0.15) * 6);

    u8g2.setDrawColor(0);
    u8g2.drawBox(20, 46, 88, 20);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(62, String(value, 1) + " " + unit);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 62, String(minValue, 1));
    drawStringRight(128, 62, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        float t_angle = PI - ((constrain(targetValue, minValue, maxValue) - minValue) / (maxValue - minValue)) * PI;
        u8g2.drawCircle(cx + cos(t_angle) * (r + 2), cy - sin(t_angle) * (r + 2), 3);
    }
}

void draw_GaugeScreen(uint8_t index)
{
    switch (index)
    {
    case 0:
        draw_StatusBar("AIR SENSORS");
        u8g2.setFont(u8g2_font_helvB14_tr);
        drawStringCenter(34, "MAP : " + String((int)mapPressure) + " kPa");
        drawStringCenter(56, "MAF : " + String(mafPressure, 1) + " g/s");
        break;
    case 1:
        if (BOOST_SCREEN == 0)
            draw_InfoText("Boost", turboPressureState, "Bar");
        else if (BOOST_SCREEN == 1)
            draw_AreaChartWithHistory(turboHistory, turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else if (BOOST_SCREEN == 2)
            draw_RoundGauge(turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else
            draw_LinearGauge(turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        break;
    case 2:
        if (IAT_SCREEN == 0)
            draw_InfoText("Temp IAT", intakeTemp, "C");
        else if (IAT_SCREEN == 1)
            draw_AreaChartWithHistory(iatHistory, intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        else if (IAT_SCREEN == 2)
            draw_RoundGauge(intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        else
            draw_LinearGauge(intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        break;
    case 3:
        if (ENGLOAD_SCREEN == 0)
            draw_InfoText("Charge", engineLoad, "%");
        else if (ENGLOAD_SCREEN == 1)
            draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge", "%");
        else if (ENGLOAD_SCREEN == 2)
            draw_RoundGauge(engineLoad, 0, 100, "Charge", "%");
        else
            draw_LinearGauge(engineLoad, 0, 100, "Charge", "%");
        break;
    case 4:
        if (COOLANT_SCREEN == 0)
            draw_InfoText("Temp LdR", coolantTemp, "C");
        else if (COOLANT_SCREEN == 1)
            draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        else if (COOLANT_SCREEN == 2)
            draw_RoundGauge(coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        else
            draw_LinearGauge(coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        break;
    case 5:
        draw_StatusBar("DASHBOARD");
        u8g2.setFont(u8g2_font_helvB08_tr); // Police ajustée pour éviter l'overflow
        drawStringLeft(0, 32, "BST: " + String(turboPressureState, 1) + "b");
        drawStringLeft(68, 32, "IAT: " + String(intakeTemp, 0) + "C");
        drawStringLeft(0, 56, "LDR: " + String(coolantTemp, 0) + "C");
        drawStringLeft(68, 56, "RPM: " + String((int)dashRPM));
        break;
    case 6:
        draw_StatusBar("CHRONO");
        u8g2.setFont(u8g2_font_helvB10_tr); // Typo adaptée
        drawStringCenter(24, "0 - " + String(TARGET_SPEED) + " km/h");
        u8g2.setFont(u8g2_font_helvB14_tr); // Typo adaptée pour ne pas écraser
        if (timerRunning)
            drawStringCenter(46, String((millis() - speedTimerStart) / 1000.0, 2) + " s");
        else if (lastTimerValue > 0)
            drawStringCenter(46, String(lastTimerValue, 2) + " s");
        else
        {
            u8g2.setFont(u8g2_font_helvB14_tr);
            drawStringCenter(46, "READY");
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        drawStringCenter(62, "Speed: " + String((int)currentSpeed) + " km/h");
        break;
    case 7:
        draw_InfoText("Speed", currentSpeed, "km/h");
        break;
    case 8:
        draw_StatusBar("OBD BLE");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(0, 24);
        u8g2.print("Status: " + bleStatusStr);
        u8g2.setCursor(0, 34);
        u8g2.print("Packets: " + String(packetsReceived));
        u8g2.setCursor(0, 44);
        u8g2.print("Init Step: " + String(elmInitStep) + "/5");
        u8g2.setCursor(0, 54);
        // Troncature du buffer pour éviter la bouillie visuelle
        u8g2.print("Buf: " + elmBuffer.substring(0, 16));
        break;
    }
}

void startCaptivePortal()
{
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(200);
    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(ssid, "12345678");
    delay(500);
    dnsServer.start(DNS_PORT, "*", apIP);
}

void startServer()
{
    startCaptivePortal();
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", generateWebPage()); });
    ElegantOTA.begin(&server);
    ElegantOTA.onStart([]()
                       { ota_updating = true; });
    server.begin();
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);
    pinMode(BTN_MENU, INPUT_PULLUP);
    if (digitalRead(BTN_MENU) == LOW)
        currentState = STATE_CONFIG;

    u8g2.begin();
    u8g2.setBusClock(400000);
    EEPROM.begin(EEPROM_SIZE);
    loadValues();
    setOledBrightness(OLED_BRIGHTNESS);

    // ==========================================
    // ANIMATION DE DÉMARRAGE : SCANNER 3D HIGH-TECH
    // ==========================================

    // Phase 1 : Faisceau laser qui scanne l'écran de haut en bas pour révéler le logo 3008
    for (int h = 0; h <= 64; h += 2)
    {
        u8g2.clearBuffer();

        // Affiche l'image uniquement au-dessus de la ligne du laser
        u8g2.setClipWindow(0, 0, 128, h);
        u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
        u8g2.setMaxClipWindow();

        // Dessine le rayon laser horizontal (une double ligne très brillante)
        if (h < 64)
        {
            u8g2.setDrawColor(1);
            u8g2.drawLine(0, h, 128, h);
            u8g2.drawLine(0, h + 1, 128, h + 1);
        }

        u8g2.sendBuffer();
        delay(15); // Vitesse du scanner
    }

    delay(200); // Courte pause une fois le logo totalement révélé

    // Phase 2 : Le bandeau d'information glisse de bas en haut (mécanique)
    for (int y = 64; y >= 44; y -= 2)
    {
        u8g2.clearBuffer();
        u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);

        // Dessine le panneau noir qui remonte progressivement
        u8g2.setDrawColor(0);
        u8g2.drawBox(0, y, 128, 64 - y);
        u8g2.setDrawColor(1);
        u8g2.drawLine(0, y, 128, y); // Ligne supérieure de séparation du bandeau

        u8g2.sendBuffer();
        delay(15);
    }

    // Phase 3 : Le système s'initialise (Le texte s'affiche et la jauge se remplit rapidement)
    for (int i = 0; i <= 100; i += 6)
    {
        u8g2.clearBuffer();
        u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);

        // Conserve le bandeau noir en place
        u8g2.setDrawColor(0);
        u8g2.drawBox(0, 44, 128, 20);
        u8g2.setDrawColor(1);
        u8g2.drawLine(0, 44, 128, 44);

        // Affiche les textes
        u8g2.setFont(u8g2_font_helvB10_tr);
        drawStringCenter(54, "CANuSEE");
        u8g2.setFont(u8g2_font_4x6_tr);
        drawStringCenter(62, version_string);

        // Barre de chargement fluide
        u8g2.drawFrame(14, 56, 100, 2);
        u8g2.drawBox(14, 56, i, 2);

        u8g2.sendBuffer();
        delay(20);
    }

    if (!LittleFS.begin())
    {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_helvR12_tr);
        drawStringCenter(30, "FS Error!");
        u8g2.sendBuffer();
        delay(2000);
        restart_ESP();
    }

    if (currentState == STATE_CONFIG)
    {
        startServer();
    }
    else
    {
        WiFi.mode(WIFI_OFF);
        BLEDevice::init("CANuSEE");
        BLEScan *pBLEScan = BLEDevice::getScan();
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        doScan = true;
    }
}

void loop()
{
    if (currentState == STATE_CONFIG)
    {
        server.handleClient();
        ElegantOTA.loop();
        dnsServer.processNextRequest();
        if (ota_updating)
            return;
    }
    if (currentState == STATE_GAUGES || currentState == STATE_CONNECTING)
        processBLE();

    bool upPressed = btnUp.pressed(), downPressed = btnDown.pressed(), okPressed = btnOk.pressed(), menuPressed = btnMenu.pressed();

    if (menuPressed)
    {
        if (currentState == STATE_CONFIG || currentState == STATE_CONNECTING)
            restart_ESP();
        else if (currentState == STATE_GAUGES || currentState == STATE_STYLE_MENU)
        {
            buildMenu();
            currentState = STATE_MENU;
        }
        else if (currentState == STATE_MENU || currentState >= STATE_EDIT_MIN)
            currentState = STATE_GAUGES;
    }

    if (upPressed || downPressed)
    {
        int dir = upPressed ? -1 : 1;
        if (currentState == STATE_GAUGES && !isTransitioning)
        {
            slideDirection = dir;
            startScreenTransition();
            if (upPressed)
                screenIndex = (screenIndex == 0) ? (screenNumbers - 1) : (screenIndex - 1);
            if (downPressed)
                screenIndex = (screenIndex + 1) % screenNumbers;
            saveValues();
        }
        else if (currentState == STATE_MENU || currentState == STATE_STYLE_MENU)
        {
            menuCursor += dir;
            if (menuCursor < 0)
                menuCursor = menuSize - 1;
            if (menuCursor >= menuSize)
                menuCursor = 0;
        }
        else if (currentState == STATE_EDIT_MIN)
            TURBO_MIN_BAR = constrain(TURBO_MIN_BAR + (dir * -0.1), -1.0, 0.5);
        else if (currentState == STATE_EDIT_MAX)
            TURBO_MAX_BAR = constrain(TURBO_MAX_BAR + (dir * -0.1), 0.5, 3.0);
        else if (currentState == STATE_EDIT_SPEED)
            TARGET_SPEED = constrain(TARGET_SPEED + (dir * -10), 40, 200);
        else if (currentState == STATE_EDIT_BRIGHTNESS)
        {
            OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS + (dir * -25), 0, 255);
            setOledBrightness(OLED_BRIGHTNESS);
        }
    }

    if (okPressed)
    {
        if (currentState == STATE_MENU)
        {
            int action = currentMenu[menuCursor].action;
            if (action == ACT_CLOSE)
                currentState = STATE_GAUGES;
            else if (action == ACT_ENTER_CONFIG)
            {
                u8g2.clearBuffer();
                drawStringCenter(30, "Hold MENU & Reboot");
                u8g2.sendBuffer();
                delay(2000);
                restart_ESP();
            }
            else if (action == ACT_OPEN_STYLE_MENU)
            {
                buildStyleMenu();
                currentState = STATE_STYLE_MENU;
            }
            else if (action == ACT_EDIT_MIN)
                currentState = STATE_EDIT_MIN;
            else if (action == ACT_EDIT_MAX)
                currentState = STATE_EDIT_MAX;
            else if (action == ACT_EDIT_SPEED)
                currentState = STATE_EDIT_SPEED;
            else if (action == ACT_EDIT_BRIGHTNESS)
                currentState = STATE_EDIT_BRIGHTNESS;
            else if (action >= ACT_GO_SCREEN_0 && action <= ACT_GO_SCREEN_0 + screenNumbers)
            {
                screenIndex = action - ACT_GO_SCREEN_0;
                saveValues();
                currentState = STATE_GAUGES;
            }
        }
        else if (currentState == STATE_STYLE_MENU)
        {
            int action = currentMenu[menuCursor].action;
            if (action == ACT_BACK_TO_MENU)
            {
                buildMenu();
                currentState = STATE_MENU;
            }
            else if (action >= ACT_SET_STYLE_TEXT && action <= ACT_SET_STYLE_BAR)
            {
                int newStyle = action - ACT_SET_STYLE_TEXT;
                if (screenIndex == 1)
                    BOOST_SCREEN = newStyle;
                else if (screenIndex == 2)
                    IAT_SCREEN = newStyle;
                else if (screenIndex == 3)
                    ENGLOAD_SCREEN = newStyle;
                else if (screenIndex == 4)
                    COOLANT_SCREEN = newStyle;
                saveValues();
                buildMenu();
                currentState = STATE_MENU;
            }
        }
        else if (currentState >= STATE_EDIT_MIN && currentState <= STATE_EDIT_BRIGHTNESS)
        {
            saveValues();
            buildMenu();
            currentState = STATE_MENU;
        }
    }

    // ==== RAFFRAICHISSEMENT ULTRA-RAPIDE 25 FPS (40ms) ====
    static unsigned long lastDrawTime = 0;
    if (millis() - lastDrawTime > 40)
    {
        lastDrawTime = millis();

        u8g2.clearBuffer();
        if (currentState == STATE_CONNECTING)
            drawConnectingScreen();
        else if (currentState == STATE_GAUGES)
        {
            draw_GaugeScreen(screenIndex);

            if (isTransitioning)
            {
                slideOffset += 24;
                if (slideOffset >= 128)
                    isTransitioning = false;
                else
                {
                    uint8_t temp[1024];
                    uint8_t *currentScreen = u8g2.getBufferPtr();
                    for (int p = 0; p < 8; p++)
                    {
                        int pageStart = p * 128;
                        if (slideDirection == 1)
                        {
                            memcpy(&temp[pageStart], &oldScreenBuffer[pageStart + slideOffset], 128 - slideOffset);
                            memcpy(&temp[pageStart + 128 - slideOffset], &currentScreen[pageStart], slideOffset);
                        }
                        else
                        {
                            memcpy(&temp[pageStart], &currentScreen[pageStart + 128 - slideOffset], slideOffset);
                            memcpy(&temp[pageStart + slideOffset], &oldScreenBuffer[pageStart], 128 - slideOffset);
                        }
                    }
                    memcpy(currentScreen, temp, 1024);
                }
            }
        }
        else if (currentState == STATE_CONFIG)
            drawConfigScreen();
        else if (currentState == STATE_MENU || currentState == STATE_STYLE_MENU)
            drawMenuScreen();

        // Utilisation des pourcentages pour générer les Sliders
        else if (currentState == STATE_EDIT_MIN)
            drawEditScreen("Turbo Min", String(TURBO_MIN_BAR, 1) + " b", (TURBO_MIN_BAR + 1.0) / 1.5);
        else if (currentState == STATE_EDIT_MAX)
            drawEditScreen("Turbo Max", String(TURBO_MAX_BAR, 1) + " b", (TURBO_MAX_BAR - 0.5) / 2.5);
        else if (currentState == STATE_EDIT_SPEED)
            drawEditScreen("Target Speed", String(TARGET_SPEED) + " km/h", (TARGET_SPEED - 40.0) / 160.0);
        else if (currentState == STATE_EDIT_BRIGHTNESS)
            drawEditScreen("Brightness", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)) + " %", OLED_BRIGHTNESS / 255.0);

        u8g2.sendBuffer();
    }
    yield();
}