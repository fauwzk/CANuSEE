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
#include "version.h"

// ==== BLE Libraries ====
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ==== Pins Configuration (4 Boutons) ====
#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 3
#define BTN_MENU 6

// ==== WiFi Config ====
const char *ssid = "CANuSEE_Setup";
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

// ==== OLED (U8g2) Full Buffer (F) ====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);
const int centerX = 64;
const int screenNumbers = 9;
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7, TURBO_MAX_BAR = 1.5;

// Variables OBD brutes
float mapPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0, engineRPM = 0.0;
float coolantTemp = 0.0, turboPressureState = 0.0, targetBoost = -1000.0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashRPM = 0, dashLoad = 0;

// Moteur de lissage (Anti-Escalier / Interpolation)
struct SmoothData
{
    float val;
    void update(float target)
    {
        if (abs(target - val) > (abs(target) * 0.5 + 20.0))
            val = target; // Raccrochage direct si énorme écart
        else
            val += (target - val) * 0.20; // Glissement organique vers la cible
    }
};
SmoothData smBoost = {0}, smIAT = {0}, smLoad = {0}, smCoolant = {0}, smRPM = {0}, smSpeed = {0}, smMAP = {0}, smMAF = {0};

// Variables pour le Chrono 0-100
bool timerRunning = false, timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0, currentSpeed = 0.0;

// Variables du Moteur de Transition Vidéo
bool isTransitioning = false;
int slideOffset = 0;
int slideDirection = 1; // 1 = Next (Vers la gauche), -1 = Prev (Vers la droite)
uint8_t oldScreenBuffer[1024];

#define MAX_MENU_ITEMS 10
struct MenuItem
{
    const char *text;
    int action;
    int icon; // Font Open Iconic (0=Exit, 1=Settings, 2=Style, 3=Brightness, 4=Arrow, 5=Speed)
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
    { // Constructeur explicite
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

// =========================================================
// ==== MOTEUR BLE ULTRA-RAPIDE (Spécifique OBDBLE FFF0) ====
// =========================================================
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
        {
            elmResponseReady = true;
        }
        else if (c != '\r' && c != '\n' && c != ' ')
        {
            elmBuffer += c;
        }
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
        case 0x0B: // Pression MAP (Collecteur en kPa)
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
        case 0x10: // Débitmètre MAF (en g/s)
            mafPressure = ((A * 256.0) + B) / 100.0;
            break;
        case 0x70: // Commanded Boost Pressure (Pression cible turbo)
            targetBoost = (((A * 256.0) + B) * 0.03125) * 0.01 - 1.0;
            if (targetBoost < 0)
                targetBoost = 0;
            break;
        }
    }
}

// ==== SMART POLLING : Demande uniquement ce qui est affiché ====
uint8_t getNextSmartPID()
{
    static uint8_t dashStep = 0;
    static uint8_t boostStep = 0;
    static uint8_t airStep = 0;

    switch (screenIndex)
    {
    case 0:
        airStep = !airStep;
        return airStep ? 0x0B : 0x10; // Alterne entre MAP (kPa) et MAF (g/s)
    case 1:
        boostStep = !boostStep;
        return boostStep ? 0x0B : 0x70; // Alterne Pression Réelle et Cible
    case 2:
        return 0x0F;
    case 3:
        return 0x04;
    case 4:
        return 0x05;
    case 5: // Dash
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
        return 0x0D; // Timer (Besoin Vitesse)
    case 7:
        return 0x0D; // Speed
    default:
        return 0x0C; // Par défaut RPM
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

        // Timeout (Laisse plus de temps pour l'étape 4 de détection auto ATSP0)
        unsigned long timeoutLimit = (elmInitStep == 4) ? 1500 : 500;
        if (!elmResponseReady && (millis() - lastElmRequest > timeoutLimit))
        {
            triggerNextRequest = true;
        }

        if (elmResponseReady)
        {
            if (elmInitStep < 5)
            {
                elmInitStep++;
                if (elmInitStep == 5 && currentState == STATE_CONNECTING)
                {
                    currentState = STATE_GAUGES; // Démarrage terminé !
                }
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

// Fonction de transition Vidéo
void startScreenTransition()
{
    memcpy(oldScreenBuffer, u8g2.getBufferPtr(), 1024);
    isTransitioning = true;
    slideOffset = 0;
}

String generateWebPage()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
        return "<html><body><h3>File not found</h3></body></html>";
    String html = file.readString();
    file.close();
    html.reserve(html.length() + 1024);

    // Remplacement dynamique du JavaScript pour simuler le double écran
    html.replace("case 0: value = data.map; break;", "/* Modifié dynamiquement pour Screen 0 */");
    html.replace(
        "if (data.screen >= 0 && data.screen <= 4 || data.screen == 7) {",
        "if (data.screen === 0) {\n"
        "                drawCenterText(\"MAP: \" + Number(data.map).toFixed(0) + \" kPa\", 32);\n"
        "                drawCenterText(\"MAF: \" + Number(data.maf).toFixed(1) + \" g/s\", 48);\n"
        "            } else if (data.screen >= 1 && data.screen <= 4 || data.screen == 7) {");

    html.replace("%MIN%", String(TURBO_MIN_BAR));
    html.replace("%MAX%", String(TURBO_MAX_BAR));
    html.replace("%VERSION%", version_string);

    html.replace("%SELECTED_BOOST_TEXT%", (BOOST_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_BOOST_GAUGE%", (BOOST_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_BOOST_DIAL%", (BOOST_SCREEN == 2) ? "selected" : "");
    html.replace("%SELECTED_BOOST_BAR%", (BOOST_SCREEN == 3) ? "selected" : "");

    html.replace("%SELECTED_LOAD_TEXT%", (ENGLOAD_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_LOAD_GAUGE%", (ENGLOAD_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_LOAD_DIAL%", (ENGLOAD_SCREEN == 2) ? "selected" : "");
    html.replace("%SELECTED_LOAD_BAR%", (ENGLOAD_SCREEN == 3) ? "selected" : "");

    html.replace("%SELECTED_COOLANT_TEXT%", (COOLANT_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_COOLANT_GAUGE%", (COOLANT_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_COOLANT_DIAL%", (COOLANT_SCREEN == 2) ? "selected" : "");
    html.replace("%SELECTED_COOLANT_BAR%", (COOLANT_SCREEN == 3) ? "selected" : "");

    html.replace("%SELECTED_IAT_TEXT%", (IAT_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_IAT_GAUGE%", (IAT_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_IAT_DIAL%", (IAT_SCREEN == 2) ? "selected" : "");
    html.replace("%SELECTED_IAT_BAR%", (IAT_SCREEN == 3) ? "selected" : "");

    html.replace("%TICKS%", String(TICK_LINE_GAUGE));
    html.replace("%MAX_SPEED%", String(TARGET_SPEED));
    html.replace("%BRIGHTNESS_PCT%", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)));
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
    BOOST_SCREEN = (cfg.boost_screen_type >= 0 && cfg.boost_screen_type <= 3) ? cfg.boost_screen_type : 0;
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = (cfg.engload_screen_type >= 0 && cfg.engload_screen_type <= 3) ? cfg.engload_screen_type : 0;
    COOLANT_SCREEN = (cfg.coolant_screen_type >= 0 && cfg.coolant_screen_type <= 3) ? cfg.coolant_screen_type : 0;
    IAT_SCREEN = (cfg.intake_temp_screen_type >= 0 && cfg.intake_temp_screen_type <= 3) ? cfg.intake_temp_screen_type : 0;
    TICK_LINE_GAUGE = (cfg.tick_line_gauge > 0) ? cfg.tick_line_gauge : 2;
    TARGET_SPEED = (cfg.target_speed >= 10 && cfg.target_speed <= 300) ? cfg.target_speed : 100;
    OLED_BRIGHTNESS = (cfg.brightness >= 0 && cfg.brightness <= 255) ? cfg.brightness : 255;
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

void draw_StatusBar(String title)
{
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, title);
    // Affichage du numéro de page en haut à droite !
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringRight(128, 8, String(screenIndex + 1) + "/" + String(screenNumbers));
}

void drawConnectingScreen()
{
    u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 44, 128, 20);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(52, bleStatusStr);

    if (connected)
        drawStringCenter(62, "Init Step: " + String(elmInitStep) + "/5");
    else
        drawStringCenter(62, "Searching OBD...");
}

void buildMenu()
{
    menuSize = 0;
    if (screenIndex >= 1 && screenIndex <= 4)
        currentMenu[menuSize++] = {"Gauge Style ->", ACT_OPEN_STYLE_MENU, 2}; // Style

    currentMenu[menuSize++] = {"Brightness", ACT_EDIT_BRIGHTNESS, 3}; // Soleil

    if (screenIndex == 1)
    {
        currentMenu[menuSize++] = {"Turbo Min", ACT_EDIT_MIN, 1}; // Paramètres
        currentMenu[menuSize++] = {"Turbo Max", ACT_EDIT_MAX, 1};
    }
    if (screenIndex == 6)
    {
        currentMenu[menuSize++] = {"Target Speed", ACT_EDIT_SPEED, 5}; // Speed/Timer
    }

    for (int i = 0; i < screenNumbers; i++)
    {
        currentMenu[menuSize++] = {screenNames[i], ACT_GO_SCREEN_0 + i, 4}; // Flèche (Ecran)
    }

    currentMenu[menuSize++] = {"Mode Config", ACT_ENTER_CONFIG, 1}; // Paramètres
    currentMenu[menuSize++] = {"Exit Menu", ACT_CLOSE, 0};          // Quitter
    menuCursor = 0;
}

void buildStyleMenu()
{
    menuSize = 0;
    currentMenu[menuSize++] = {"Text", ACT_SET_STYLE_TEXT, 2};
    currentMenu[menuSize++] = {"Graph", ACT_SET_STYLE_GRAPH, 2};
    currentMenu[menuSize++] = {"Dial", ACT_SET_STYLE_DIAL, 2};
    currentMenu[menuSize++] = {"Bar", ACT_SET_STYLE_BAR, 2};
    currentMenu[menuSize++] = {"<- Back", ACT_BACK_TO_MENU, 0};

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

// Typographie vectorielle pour les icônes U8g2
void drawMenuScreen()
{
    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(10, (currentState == STATE_STYLE_MENU) ? "STYLE" : "MENU");

    // Dessin de l'icône associée avec la police Open Iconic
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    int iconCode = currentMenu[menuCursor].icon;
    if (iconCode == 0)
        u8g2.drawGlyph(48, 44, 0x47); // Exit (Croix)
    else if (iconCode == 1)
        u8g2.drawGlyph(48, 44, 0x48); // Settings (Engrenage)
    else if (iconCode == 2)
        u8g2.drawGlyph(48, 44, 0x4a); // Style (Curseurs)
    else if (iconCode == 3)
    {
        u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
        u8g2.drawGlyph(48, 44, 0x45);
    } // Sun
    else if (iconCode == 4)
        u8g2.drawGlyph(48, 44, 0x43); // Arrow Right (Ecran)
    else if (iconCode == 5)
        u8g2.drawGlyph(48, 44, 0x4b); // Speed (Chrono)

    // Barre de sélection en bas
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 50, 128, 14);
    u8g2.setDrawColor(0); // Texte noir sur blanc
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(62, currentMenu[menuCursor].text);
    u8g2.setDrawColor(1); // Rétablir la couleur normale

    // Flèches Haut/Bas
    u8g2.setFont(u8g2_font_open_iconic_arrow_1x_t);
    if (menuCursor > 0)
        u8g2.drawGlyph(116, 28, 0x40);
    if (menuCursor < menuSize - 1)
        u8g2.drawGlyph(116, 40, 0x43);
}

void drawEditScreen(String title, String valueStr, String instruction)
{
    u8g2.setFont(u8g2_font_helvR10_tr);
    drawStringCenter(16, title);
    u8g2.setFont(u8g2_font_helvR18_tr);
    drawStringCenter(38, valueStr);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(60, instruction);
}

void drawConfigScreen()
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(14, "MODE CONFIG");
    u8g2.drawLine(0, 18, 128, 18);
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(30, "WiFi: " + String(ssid));
    drawStringCenter(42, "MDP: 12345678");
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(60, "192.168.4.1");
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

void draw_InfoText(String title, double value, String unit)
{
    draw_StatusBar(title);
    // Typographie Géante
    u8g2.setFont(u8g2_font_helvB24_tr);
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

    // Graphique agrandi qui utilise l'espace du bas
    int chartX = 32, chartY = 16, chartWidth = AREA_CHART_HISTORY, chartHeight = 44, baseY = chartY + chartHeight;
    u8g2.drawFrame(chartX, chartY, chartWidth, chartHeight);

    double range = maxValue - minValue;
    for (int i = 0; i < chartWidth; i++)
    {
        int historyIdx = history.currentIndex + i;
        if (!history.initialized && i >= history.currentIndex)
            break;
        historyIdx = historyIdx % AREA_CHART_HISTORY;
        double val = constrain(history.values[historyIdx], minValue, maxValue);
        int pixelHeight = (int)(chartHeight * ((val - minValue) / range));
        u8g2.drawLine(chartX + i, baseY, chartX + i, baseY - pixelHeight);
    }

    // Cible (Ligne pointillée)
    if (targetValue > -999.0)
    {
        double t_val = constrain(targetValue, minValue, maxValue);
        int t_height = (int)(chartHeight * ((t_val - minValue) / range));
        int ty = baseY - t_height;
        for (int tx = chartX; tx < chartX + chartWidth; tx += 4)
            u8g2.drawPixel(tx, ty);
    }

    int maxLabelY = chartY + 8, minLabelY = baseY, valCenterY = chartY + (chartHeight / 2) + 4, alignBorderX = chartX - 2;
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringRight(alignBorderX, maxLabelY, alignSign(formatDecimal(maxValue, 1)));
    drawStringRight(alignBorderX, minLabelY, alignSign(formatDecimal(minValue, 1)));
    drawStringRight(alignBorderX, valCenterY, alignSign(formatDecimal(newValue, 1)));
}

void draw_LinearGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    draw_StatusBar(label);

    // Valeur Centrale Actuelle (très lisible)
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(36, String(value, 1) + " " + unit);

    // Jauge segmentée allongée et abaissée
    int barX = 4, barY = 44, barW = 120, barH = 12;
    u8g2.drawFrame(barX, barY, barW, barH);

    float val = constrain(value, minValue, maxValue);
    int segments = 23;
    int activeSegs = (int)(((val - minValue) / (maxValue - minValue)) * segments);
    for (int i = 0; i < activeSegs; i++)
        u8g2.drawBox(barX + 3 + (i * 5), barY + 3, 4, barH - 6);

    // Textes Min/Max tout en bas !
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 64, String(minValue, 1));
    drawStringRight(128, 64, String(maxValue, 1));

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

    // Cadran repensé : Immense, repose en bas de l'écran
    int cx = 64, cy = 68, r = 52;

    // Traits du cadran (Ticks)
    for (int i = 0; i <= 10; i++)
    {
        float a = PI - (i * PI / 10.0);
        int r_inner = r - ((i % 5 == 0) ? 8 : 4);
        u8g2.drawLine(cx + cos(a) * r, cy - sin(a) * r, cx + cos(a) * r_inner, cy - sin(a) * r_inner);
    }

    // Aiguille épaisse
    float val = constrain(value, minValue, maxValue);
    float angle = PI - ((val - minValue) / (maxValue - minValue)) * PI;
    int nx = cx + cos(angle) * (r - 10);
    int ny = cy - sin(angle) * (r - 10);
    u8g2.drawTriangle(nx, ny, cx + cos(angle + 0.15) * 8, cy - sin(angle + 0.15) * 8, cx + cos(angle - 0.15) * 8, cy - sin(angle - 0.15) * 8);

    // Masquage central PROPRE
    u8g2.setDrawColor(0);
    u8g2.drawBox(20, 48, 88, 20); // Dégage l'espace pour le texte géant
    u8g2.setDrawColor(1);

    // Valeur textuelle énorme à l'intérieur
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(64, String(value, 1) + " " + unit);

    // Min / Max relégués dans les coins (Y=64)
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 64, String(minValue, 1));
    drawStringRight(128, 64, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        float t_val = constrain(targetValue, minValue, maxValue);
        float t_angle = PI - ((t_val - minValue) / (maxValue - minValue)) * PI;
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
        drawStringCenter(30, "MAP : " + String((int)smMAP.val) + " kPa");
        drawStringCenter(54, "MAF : " + String(smMAF.val, 1) + " g/s");
        break;
    case 1:
        if (BOOST_SCREEN == 0)
            draw_InfoText("Boost", smBoost.val, "Bar");
        else if (BOOST_SCREEN == 1)
            draw_AreaChartWithHistory(turboHistory, smBoost.val, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else if (BOOST_SCREEN == 2)
            draw_RoundGauge(smBoost.val, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else
            draw_LinearGauge(smBoost.val, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        break;
    case 2:
        if (IAT_SCREEN == 0)
            draw_InfoText("Temp IAT", smIAT.val, "C");
        else if (IAT_SCREEN == 1)
            draw_AreaChartWithHistory(iatHistory, smIAT.val, -20.0, 60.0, "Temp IAT", "C");
        else if (IAT_SCREEN == 2)
            draw_RoundGauge(smIAT.val, -20.0, 60.0, "Temp IAT", "C");
        else
            draw_LinearGauge(smIAT.val, -20.0, 60.0, "Temp IAT", "C");
        break;
    case 3:
        if (ENGLOAD_SCREEN == 0)
            draw_InfoText("Charge", smLoad.val, "%");
        else if (ENGLOAD_SCREEN == 1)
            draw_AreaChartWithHistory(loadHistory, smLoad.val, 0, 100, "Charge", "%");
        else if (ENGLOAD_SCREEN == 2)
            draw_RoundGauge(smLoad.val, 0, 100, "Charge", "%");
        else
            draw_LinearGauge(smLoad.val, 0, 100, "Charge", "%");
        break;
    case 4:
        if (COOLANT_SCREEN == 0)
            draw_InfoText("Temp LdR", smCoolant.val, "C");
        else if (COOLANT_SCREEN == 1)
            draw_AreaChartWithHistory(coolantHistory, smCoolant.val, 40.0, 120.0, "Temp LdR", "C");
        else if (COOLANT_SCREEN == 2)
            draw_RoundGauge(smCoolant.val, 40.0, 120.0, "Temp LdR", "C");
        else
            draw_LinearGauge(smCoolant.val, 40.0, 120.0, "Temp LdR", "C");
        break;
    case 5:
        draw_StatusBar("DASHBOARD");
        u8g2.setFont(u8g2_font_helvB10_tr);
        drawStringLeft(0, 30, "BST: " + String(smBoost.val, 1) + "b");
        drawStringLeft(68, 30, "IAT: " + String(smIAT.val, 0) + "C");
        drawStringLeft(0, 54, "LDR: " + String(smCoolant.val, 0) + "C");
        drawStringLeft(68, 54, "RPM: " + String((int)smRPM.val));
        break;
    case 6:
        draw_StatusBar("CHRONO");
        u8g2.setFont(u8g2_font_helvR12_tr);
        drawStringCenter(24, "0 - " + String(TARGET_SPEED) + " km/h");
        u8g2.setFont(u8g2_font_helvB18_tr);
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
        draw_InfoText("Speed", smSpeed.val, "km/h");
        break;
    case 8:
        draw_StatusBar("OBD BLE");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(0, 24);
        u8g2.print("Status: " + bleStatusStr);
        u8g2.setCursor(0, 34);
        u8g2.print("Packets RX: " + String(packetsReceived));
        u8g2.setCursor(0, 44);
        u8g2.print("Init Step: " + String(elmInitStep) + "/5");
        u8g2.setCursor(0, 54);
        u8g2.print("Buf: " + elmBuffer);
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
    server.on("/save", HTTP_POST, []()
              {
      if (server.hasArg("brightness")) OLED_BRIGHTNESS = map(server.arg("brightness").toInt(), 0, 100, 0, 255);
      if (server.hasArg("ticks")) TICK_LINE_GAUGE = server.arg("ticks").toInt();
      if (server.hasArg("max_speed")) TARGET_SPEED = server.arg("max_speed").toInt();
      if (server.hasArg("boost_min")) TURBO_MIN_BAR = server.arg("boost_min").toFloat();
      if (server.hasArg("boost_max")) TURBO_MAX_BAR = server.arg("boost_max").toFloat();
      if (server.hasArg("boost_gauge_type")) BOOST_SCREEN = server.arg("boost_gauge_type").toInt();
      if (server.hasArg("iat_gauge_type")) IAT_SCREEN = server.arg("iat_gauge_type").toInt();
      if (server.hasArg("engload_gauge_type")) ENGLOAD_SCREEN = server.arg("engload_gauge_type").toInt();
      if (server.hasArg("coolant_gauge_type")) COOLANT_SCREEN = server.arg("coolant_gauge_type").toInt();
      
      OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS, 0, 255); setOledBrightness(OLED_BRIGHTNESS);
      saveValues(); server.sendHeader("Location", "/"); server.send(303); });

    server.on("/reset", HTTP_GET, []()
              { server.send(200, "text/plain", "Resetting..."); delay(1000); ESP.restart(); });
    server.on("/generate_204", HTTP_GET, []()
              { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
    server.onNotFound([]()
                      { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });

    ElegantOTA.begin(&server);
    ElegantOTA.onStart([]()
                       { ota_updating = true; u8g2.clearBuffer(); u8g2.setFont(u8g2_font_helvR12_tr); drawStringCenter(35, "OTA UPDATING"); u8g2.sendBuffer(); });
    server.begin();
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

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

    u8g2.clearBuffer();
    u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(56, "Demarrage...");
    u8g2.sendBuffer();
    delay(1500);

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

    bool upPressed = btnUp.pressed();
    bool downPressed = btnDown.pressed();
    bool okPressed = btnOk.pressed();
    bool menuPressed = btnMenu.pressed();

    if (menuPressed)
    {
        if (currentState == STATE_CONFIG || currentState == STATE_CONNECTING)
            restart_ESP();
        else if (currentState == STATE_GAUGES)
        {
            buildMenu();
            currentState = STATE_MENU;
        }
        else if (currentState == STATE_STYLE_MENU)
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
            slideDirection = dir; // 1 (Next, slide left), -1 (Prev, slide right)
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

        // Application de la Moyenne Mobile Exponentielle pour lisser les données
        smBoost.update(turboPressureState);
        smIAT.update(intakeTemp);
        smLoad.update(engineLoad);
        smCoolant.update(coolantTemp);
        smSpeed.update(currentSpeed);
        smRPM.update(dashRPM);
        smMAP.update(mapPressure);
        smMAF.update(mafPressure);

        u8g2.clearBuffer();
        if (currentState == STATE_CONNECTING)
            drawConnectingScreen();
        else if (currentState == STATE_GAUGES)
        {
            draw_GaugeScreen(screenIndex);

            // MOTEUR DE TRANSITION GLISSANTE SUR BUFFER U8G2
            if (isTransitioning)
            {
                slideOffset += 24; // Vitesse du slide (5 frames environ)
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
                        { // Swipe Next (Glisse à gauche)
                            memcpy(&temp[pageStart], &oldScreenBuffer[pageStart + slideOffset], 128 - slideOffset);
                            memcpy(&temp[pageStart + 128 - slideOffset], &currentScreen[pageStart], slideOffset);
                        }
                        else
                        { // Swipe Prev (Glisse à droite)
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
        else if (currentState == STATE_EDIT_MIN)
            drawEditScreen("Edit Turbo Min", String(TURBO_MIN_BAR, 1), "U/D: Edit | OK: Save");
        else if (currentState == STATE_EDIT_MAX)
            drawEditScreen("Edit Turbo Max", String(TURBO_MAX_BAR, 1), "U/D: Edit | OK: Save");
        else if (currentState == STATE_EDIT_SPEED)
            drawEditScreen("Target Speed", String(TARGET_SPEED), "U/D: Edit | OK: Save");
        else if (currentState == STATE_EDIT_BRIGHTNESS)
            drawEditScreen("Brightness", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)) + " %", "U/D: Edit | OK: Save");

        u8g2.sendBuffer();
    }
    yield();
}