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

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 3
#define BTN_MENU 6

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

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);
const int centerX = 64;
const int screenNumbers = 9;
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7, TURBO_MAX_BAR = 1.5;

float mapPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0, engineRPM = 0.0;
float coolantTemp = 0.0, turboPressureState = 0.0, targetBoost = -1000.0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashRPM = 0, dashLoad = 0;

bool timerRunning = false, timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0, currentSpeed = 0.0;

#define MAX_MENU_ITEMS 10
struct MenuItem
{
    const char *text;
    int action;
    int icon; // 0=None, 1=Return, 2=Gear, 3=Sun, 4=Speed/Chrono, 5=Gauge, 6=Eye
};

MenuItem currentMenu[MAX_MENU_ITEMS];
int menuSize = 0, menuCursor = 0;

#define ACT_CLOSE 1
#define ACT_OPEN_STYLE_MENU 2
#define ACT_EDIT_MIN 3
#define ACT_EDIT_MAX 4
#define ACT_EDIT_SPEED 5
#define ACT_EDIT_BRIGHTNESS 6
#define ACT_ENTER_CONFIG 7
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
                return state; // Retourne true seulement lors de l'appui
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
static boolean doConnect = false, connected = false, doScan = false, scanIsRunning = false;
static BLERemoteCharacteristic *pTxCharacteristic = nullptr, *pRxCharacteristic = nullptr;
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
        long A = strtol(response.substring(idx + 4, idx + 6).c_str(), NULL, 16);
        long B = (response.length() >= idx + 8) ? strtol(response.substring(idx + 6, idx + 8).c_str(), NULL, 16) : 0;

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
            turboPressureState = max(0.0, (A - 100.0) * 0.01);
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
            targetBoost = max(0.0, (((A * 256.0) + B) * 0.03125) * 0.01 - 1.0);
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
        return (dashStep == 0) ? 0x0B : (dashStep == 1) ? 0x0F
                                    : (dashStep == 2)   ? 0x05
                                                        : 0x0C;
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
    if (doConnect)
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
                parseOBDResponse(elmBuffer, currentExpectedPID);
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
    else if (doScan && !scanIsRunning)
    {
        scanIsRunning = true;
        BLEDevice::getScan()->start(2, scanCompleteCB, false);
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

String generateWebPage()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
        return "<html><body><h3>File Web Manquant! Flashez le LittleFS!</h3></body></html>";
    String html = file.readString();
    file.close();

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
    BOOST_SCREEN = constrain(cfg.boost_screen_type, 0, 3);
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = constrain(cfg.engload_screen_type, 0, 3);
    COOLANT_SCREEN = constrain(cfg.coolant_screen_type, 0, 3);
    IAT_SCREEN = constrain(cfg.intake_temp_screen_type, 0, 3);
    TICK_LINE_GAUGE = (cfg.tick_line_gauge > 0) ? cfg.tick_line_gauge : 2;
    TARGET_SPEED = (cfg.target_speed >= 10 && cfg.target_speed <= 300) ? cfg.target_speed : 100;
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

// Générateur Vectoriel sur Mesure - Fini les bugs de mémoire !
void drawVectorIcon(int x, int y, int type)
{
    u8g2.setDrawColor(1);
    if (type == 1)
    { // Flèche Retour (<-)
        u8g2.drawLine(x + 8, y, x + 24, y);
        u8g2.drawLine(x + 8, y, x + 14, y - 6);
        u8g2.drawLine(x + 8, y, x + 14, y + 6);
    }
    else if (type == 2)
    { // Engrenage
        u8g2.drawCircle(x + 16, y, 6);
        for (int i = 0; i < 8; i++)
        {
            float a = i * (PI / 4);
            u8g2.drawLine(x + 16 + cos(a) * 6, y + sin(a) * 6, x + 16 + cos(a) * 10, y + sin(a) * 10);
        }
    }
    else if (type == 3)
    { // Soleil (Luminosité)
        u8g2.drawCircle(x + 16, y, 5);
        for (int i = 0; i < 8; i++)
        {
            float a = i * (PI / 4);
            u8g2.drawPixel(x + 16 + cos(a) * 8, y + sin(a) * 8);
            u8g2.drawPixel(x + 16 + cos(a) * 9, y + sin(a) * 9);
        }
    }
    else if (type == 4)
    { // Chrono
        u8g2.drawCircle(x + 16, y, 10);
        u8g2.drawLine(x + 16, y, x + 16, y - 6);
        u8g2.drawLine(x + 16, y, x + 20, y);
        u8g2.drawLine(x + 14, y - 12, x + 18, y - 12); // Bouton haut
    }
    else if (type == 5)
    { // Jauge (Arc)
        u8g2.drawCircle(x + 16, y + 4, 10, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
        u8g2.drawLine(x + 16, y + 4, x + 22, y - 2); // Aiguille
    }
    else if (type == 6)
    { // Oeil (Screen/Vue)
        u8g2.drawEllipse(x + 16, y, 12, 6);
        u8g2.drawCircle(x + 16, y, 3);
    }
}

void buildMenu()
{
    menuSize = 0;
    if (screenIndex >= 1 && screenIndex <= 4)
        currentMenu[menuSize++] = {"Style Jauge", ACT_OPEN_STYLE_MENU, 5};
    for (int i = 0; i < screenNumbers; i++)
        currentMenu[menuSize++] = {screenNames[i], ACT_GO_SCREEN_0 + i, 6};
    if (screenIndex == 1)
    {
        currentMenu[menuSize++] = {"Min Turbo", ACT_EDIT_MIN, 2};
        currentMenu[menuSize++] = {"Max Turbo", ACT_EDIT_MAX, 2};
    }
    if (screenIndex == 6)
        currentMenu[menuSize++] = {"Chrono Target", ACT_EDIT_SPEED, 4};

    currentMenu[menuSize++] = {"Luminosite", ACT_EDIT_BRIGHTNESS, 3};
    currentMenu[menuSize++] = {"Mode Config (Wifi)", ACT_ENTER_CONFIG, 2};
    currentMenu[menuSize++] = {"Quitter Menu", ACT_CLOSE, 1};
    menuCursor = 0;
}

void buildStyleMenu()
{
    menuSize = 0;
    currentMenu[menuSize++] = {"Mode Texte", ACT_SET_STYLE_TEXT, 5};
    currentMenu[menuSize++] = {"Mode Graphique", ACT_SET_STYLE_GRAPH, 5};
    currentMenu[menuSize++] = {"Mode Cadran", ACT_SET_STYLE_DIAL, 5};
    currentMenu[menuSize++] = {"Mode Barre", ACT_SET_STYLE_BAR, 5};
    currentMenu[menuSize++] = {"Retour", ACT_BACK_TO_MENU, 1};

    int currentType = (screenIndex == 1) ? BOOST_SCREEN : (screenIndex == 2) ? IAT_SCREEN
                                                      : (screenIndex == 3)   ? ENGLOAD_SCREEN
                                                                             : COOLANT_SCREEN;
    menuCursor = constrain(currentType, 0, 3);
}

void drawMenuScreen()
{
    // Entête Menu
    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(10, (currentState == STATE_STYLE_MENU) ? "STYLE" : "MENU");

    // Icone vectorielle Centrale
    drawVectorIcon(48, 30, currentMenu[menuCursor].icon);

    // Barre de sélection Inversée en bas
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 50, 128, 14);
    u8g2.setDrawColor(0); // Texte Noir sur fond Blanc
    u8g2.setFont(u8g2_font_helvR10_tr);
    drawStringCenter(61, String(currentMenu[menuCursor].text));
    u8g2.setDrawColor(1); // Retour à la normale

    // Flèches HAUT / BAS pour le carrousel
    u8g2.setFont(u8g2_font_open_iconic_arrow_1x_t);
    if (menuCursor > 0)
        u8g2.drawGlyph(116, 28, 0x40); // Flèche Haut
    if (menuCursor < menuSize - 1)
        u8g2.drawGlyph(116, 40, 0x43); // Flèche Bas
}

void drawEditScreen(String title, String valueStr)
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(16, title);
    u8g2.setFont(u8g2_font_helvR18_tr);
    drawStringCenter(42, valueStr);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(62, "U/D: Edit | OK: Save");
}

void draw_ScreenNumber(uint8_t index)
{
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringRight(126, 8, String(index + 1) + "/" + String(screenNumbers));
}

void draw_InfoText(String title, double value, String unit)
{
    u8g2.setFont(u8g2_font_helvR10_tr);
    drawStringLeft(0, 10, title);
    draw_ScreenNumber(screenIndex);

    // Typographie IMMENSE puisque le bas est libre
    u8g2.setFont(u8g2_font_helvB24_tr);
    String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
    drawStringCenter(48, valStr + " " + unit);
}

#define AREA_CHART_HISTORY 96
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

void draw_AreaChartWithHistory(AreaChartData &history, double newValue, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    double valToStore = constrain(newValue, minValue, maxValue);
    history.values[history.currentIndex] = valToStore;
    history.currentIndex = (history.currentIndex + 1) % AREA_CHART_HISTORY;
    if (history.currentIndex == 0)
        history.initialized = true;

    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, label);
    draw_ScreenNumber(screenIndex);

    // Graphique agrandi qui utilise le bas de l'écran
    int chartX = 32, chartY = 16, chartWidth = AREA_CHART_HISTORY, chartHeight = 46, baseY = chartY + chartHeight;
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
        int ty = baseY - (int)(chartHeight * ((constrain(targetValue, minValue, maxValue) - minValue) / range));
        for (int tx = chartX; tx < chartX + chartWidth; tx += 4)
            u8g2.drawPixel(tx, ty);
    }

    int alignBorderX = chartX - 2;
    drawStringRight(alignBorderX, chartY + 8, alignSign(formatDecimal(maxValue, 1)));
    drawStringRight(alignBorderX, baseY, alignSign(formatDecimal(minValue, 1)));
    drawStringRight(alignBorderX, chartY + (chartHeight / 2) + 4, alignSign(formatDecimal(newValue, 1)));
}

void draw_LinearGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, label);
    draw_ScreenNumber(screenIndex);

    // Valeur centrale très lisible
    u8g2.setFont(u8g2_font_helvR18_tr);
    drawStringCenter(32, String(value, 1) + " " + unit);

    // Barre descendue vers le bas de l'écran
    int barX = 4, barY = 44, barW = 120, barH = 10;
    u8g2.drawFrame(barX, barY, barW, barH);

    float val = constrain(value, minValue, maxValue);
    int activeSegs = (int)(((val - minValue) / (maxValue - minValue)) * 23);
    for (int i = 0; i < activeSegs; i++)
        u8g2.drawBox(barX + 2 + (i * 5), barY + 2, 4, barH - 4);

    // Textes Min/Max tout en bas dans les coins
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 64, String(minValue, 1));
    drawStringRight(128, 64, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        int t_x = barX + 2 + (int)(((constrain(targetValue, minValue, maxValue) - minValue) / (maxValue - minValue)) * (barW - 4));
        u8g2.drawTriangle(t_x, barY - 1, t_x - 3, barY - 5, t_x + 3, barY - 5);
    }
}

void draw_RoundGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, label);
    draw_ScreenNumber(screenIndex);

    // Cadran GÉANT (Prend toute la largeur de l'écran)
    int cx = 64, cy = 64, r = 50;

    // Traits du cadran (Ticks)
    for (int i = 0; i <= 10; i++)
    {
        float a = PI - (i * PI / 10.0);
        int r_inner = r - ((i % 5 == 0) ? 8 : 4);
        u8g2.drawLine(cx + cos(a) * r, cy - sin(a) * r, cx + cos(a) * r_inner, cy - sin(a) * r_inner);
    }

    // Aiguille épaisse style compte-tours
    float val = constrain(value, minValue, maxValue);
    float angle = PI - ((val - minValue) / (maxValue - minValue)) * PI;
    int nx = cx + cos(angle) * (r - 10);
    int ny = cy - sin(angle) * (r - 10);
    u8g2.drawTriangle(nx, ny, cx + cos(angle + 0.15) * 12, cy - sin(angle + 0.15) * 12, cx + cos(angle - 0.15) * 12, cy - sin(angle - 0.15) * 12);

    // Masquage central (Pour écrire la valeur sans chevaucher l'aiguille)
    u8g2.setDrawColor(0);
    u8g2.drawBox(20, 42, 88, 22);
    u8g2.setDrawColor(1);

    // Valeur textuelle au centre du cadran
    u8g2.setFont(u8g2_font_helvB14_tr);
    drawStringCenter(58, String(value, 1) + " " + unit);

    // Min / Max relégués tout en bas dans les coins (Y=64)
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 64, String(minValue, 1));
    drawStringRight(128, 64, String(maxValue, 1));

    // Curseur Cible (Petit rond sur le bord)
    if (targetValue > -999.0)
    {
        float t_angle = PI - ((constrain(targetValue, minValue, maxValue) - minValue) / (maxValue - minValue)) * PI;
        u8g2.drawCircle(cx + cos(t_angle) * (r + 2), cy - sin(t_angle) * (r + 2), 2);
    }
}

void draw_GaugeScreen(uint8_t index)
{
    switch (index)
    {
    case 0:
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 8, "AIR SENSORS");
        draw_ScreenNumber(screenIndex);
        u8g2.setFont(u8g2_font_helvR14_tr);
        drawStringCenter(30, "MAP: " + String((int)mapPressure) + " kPa");
        drawStringCenter(54, "MAF: " + String(mafPressure, 1) + " g/s");
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
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 8, "DASHBOARD");
        draw_ScreenNumber(screenIndex);
        u8g2.setFont(u8g2_font_helvR10_tr);
        drawStringLeft(0, 30, "BST: " + String(dashBoost, 1) + "b");
        drawStringLeft(68, 30, "IAT: " + String(dashIAT, 0) + "C");
        drawStringLeft(0, 54, "LDR: " + String(dashCoolant, 0) + "C");
        drawStringLeft(68, 54, "RPM: " + String((int)dashRPM));
        break;
    case 6:
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 8, "CHRONO");
        draw_ScreenNumber(screenIndex);
        u8g2.setFont(u8g2_font_helvR12_tr);
        drawStringCenter(24, "0 - " + String(TARGET_SPEED) + " km/h");
        u8g2.setFont(u8g2_font_helvB18_tr);
        if (timerRunning)
            drawStringCenter(50, String((millis() - speedTimerStart) / 1000.0, 2) + " s");
        else if (lastTimerValue > 0)
            drawStringCenter(50, String(lastTimerValue, 2) + " s");
        else
        {
            u8g2.setFont(u8g2_font_helvB14_tr);
            drawStringCenter(50, "READY");
        }
        break;
    case 7:
        draw_InfoText("Vitesse", currentSpeed, "km/h");
        break;
    case 8:
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 8, "OBD DEBUG");
        draw_ScreenNumber(screenIndex);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(0, 26);
        u8g2.print("Status: " + bleStatusStr);
        u8g2.setCursor(0, 36);
        u8g2.print("Packets: " + String(packetsReceived));
        u8g2.setCursor(0, 46);
        u8g2.print("Init Step: " + String(elmInitStep) + "/5");
        u8g2.setCursor(0, 56);
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
    delay(2000); // FIX BOOTLOOP: Pause de sécurité pour la tension

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
        else if (currentState != STATE_GAUGES)
            currentState = STATE_GAUGES;
    }

    if (upPressed || downPressed)
    {
        int dir = upPressed ? -1 : 1;
        if (currentState == STATE_GAUGES)
        {
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
        {
            TURBO_MIN_BAR = constrain(TURBO_MIN_BAR + (dir * -0.1), -1.0, 0.5);
        }
        else if (currentState == STATE_EDIT_MAX)
        {
            TURBO_MAX_BAR = constrain(TURBO_MAX_BAR + (dir * -0.1), 0.5, 3.0);
        }
        else if (currentState == STATE_EDIT_SPEED)
        {
            TARGET_SPEED = constrain(TARGET_SPEED + (dir * -10), 40, 200);
        }
        else if (currentState == STATE_EDIT_BRIGHTNESS)
        {
            OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS + (dir * -25), 0, 255);
            setOledBrightness(OLED_BRIGHTNESS);
        }
    }

    if (okPressed)
    {
        if (currentState == STATE_MENU || currentState == STATE_STYLE_MENU)
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
            else if (action == ACT_BACK_TO_MENU)
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

    static unsigned long lastDrawTime = 0;
    if (millis() - lastDrawTime > 60)
    {
        lastDrawTime = millis();
        u8g2.clearBuffer();
        if (currentState == STATE_CONNECTING)
        {
            u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
            u8g2.setDrawColor(0);
            u8g2.drawBox(0, 44, 128, 20);
            u8g2.setDrawColor(1);
            u8g2.setFont(u8g2_font_helvR08_tr);
            drawStringCenter(52, bleStatusStr);
            drawStringCenter(62, connected ? "Init Step: " + String(elmInitStep) + "/5" : "Searching OBD...");
        }
        else if (currentState == STATE_GAUGES)
            draw_GaugeScreen(screenIndex);
        else if (currentState == STATE_CONFIG)
        {
            u8g2.setFont(u8g2_font_helvB10_tr);
            drawStringCenter(14, "MODE CONFIG");
            u8g2.drawLine(0, 18, 128, 18);
            u8g2.setFont(u8g2_font_helvR08_tr);
            drawStringCenter(34, "WiFi: " + String(ssid));
            drawStringCenter(46, "MDP: 12345678");
            u8g2.setFont(u8g2_font_5x7_tr);
            drawStringCenter(62, "192.168.4.1");
        }
        else if (currentState == STATE_MENU || currentState == STATE_STYLE_MENU)
            drawMenuScreen();
        else if (currentState == STATE_EDIT_MIN)
            drawEditScreen("Edit Min", String(TURBO_MIN_BAR, 1));
        else if (currentState == STATE_EDIT_MAX)
            drawEditScreen("Edit Max", String(TURBO_MAX_BAR, 1));
        else if (currentState == STATE_EDIT_SPEED)
            drawEditScreen("Target Speed", String(TARGET_SPEED));
        else if (currentState == STATE_EDIT_BRIGHTNESS)
            drawEditScreen("Brightness", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)) + " %");
        u8g2.sendBuffer();
    }
    yield();
}