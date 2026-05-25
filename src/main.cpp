#include <Wire.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <DNSServer.h>
#include "epd_bitmap_logo_3008.h"
#include <ElegantOTA.h>
#include "version.h"

// ==== WiFi Config ====
const char *ssid = "CANuSEE_Config";
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// ==== Settings Structure ====
struct Settings
{
    int last_screen;
    int boost_screen_type;
    float turbo_min;
    float turbo_max;
    int engload_screen_type;
    int battery_screen_type;
    int coolant_screen_type;
    int intake_temp_screen_type;
    int tick_line_gauge;
};

#define EEPROM_SIZE sizeof(Settings)
Settings cfg;

int BOOST_SCREEN = 0;
int ENGLOAD_SCREEN = 0;
int BATTERY_SCREEN = 0;
int COOLANT_SCREEN = 0;
int IAT_SCREEN = 0;
int TICK_LINE_GAUGE = 2;

// ==== State Machine ====
enum AppState
{
    STATE_GAUGES,
    STATE_MENU,
    STATE_EDIT_MIN,
    STATE_EDIT_MAX
};
AppState currentState = STATE_GAUGES;

// ==== Dashboard refresh state ====
uint8_t dashStep = 0;
unsigned long dashLastUpdate = 0;
const unsigned long dashDelay = 10;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashLoad = 0;

String version_string = "CANuSEE " FW_VERSION;

// ==== OLED (U8g2) - FIX: Changed U8G2_R0 to U8G2_R2 for 180° rotation ====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE, 22, 21);

// ==== ELM327 Classic Bluetooth ====
BluetoothSerial SerialBT;
#define ELM327_BT_PIN "1234"
ELM327 myELM327;
uint8_t elm_address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// ==== Coordinates & State ====
const int centerX = 64;
const int screenNumbers = 8;
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7;
float TURBO_MAX_BAR = 1.5;

#define BUTTON_PIN 32
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE;
esp_spp_role_t role = ESP_SPP_ROLE_MASTER;

float atmoPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0;
float coolantTemp = 0.0, batteryVoltage = 0.0, turboPressureState = 0.0;

// ==== Dynamic Menu Variables ====
#define MAX_MENU_ITEMS 14
String menuText[MAX_MENU_ITEMS];
int menuAction[MAX_MENU_ITEMS];
int menuSize = 0;
int menuCursor = 0;

#define ACT_CLOSE 0
#define ACT_TOGGLE_STYLE 1
#define ACT_EDIT_MIN 2
#define ACT_EDIT_MAX 3
#define ACT_RESET_DTC 4
#define ACT_GO_SCREEN_0 10

const char *screenNames[] = {"MAF", "Boost", "IAT", "Load", "Battery", "Coolant", "DTC", "Dash"};

// ==== Text Alignment Helpers ====
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

// ==== HTML Generator ====
String generateWebPage()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
        return "<html><body><h3>File not found</h3></body></html>";
    String html = file.readString();
    file.close();
    return html;
}

// ==== EEPROM Functions ====
void saveValues()
{
    cfg.last_screen = screenIndex;
    cfg.boost_screen_type = BOOST_SCREEN;
    cfg.turbo_min = TURBO_MIN_BAR;
    cfg.turbo_max = TURBO_MAX_BAR;
    cfg.engload_screen_type = ENGLOAD_SCREEN;
    cfg.battery_screen_type = BATTERY_SCREEN;
    cfg.coolant_screen_type = COOLANT_SCREEN;
    cfg.intake_temp_screen_type = IAT_SCREEN;
    cfg.tick_line_gauge = TICK_LINE_GAUGE;
    EEPROM.put(0, cfg);
    EEPROM.commit();
}

void loadValues()
{
    EEPROM.get(0, cfg);
    screenIndex = (cfg.last_screen >= 0 && cfg.last_screen < screenNumbers) ? cfg.last_screen : 0;
    BOOST_SCREEN = (cfg.boost_screen_type >= 0 && cfg.boost_screen_type < 2) ? cfg.boost_screen_type : 0;
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = (cfg.engload_screen_type >= 0 && cfg.engload_screen_type < 2) ? cfg.engload_screen_type : 0;
    BATTERY_SCREEN = (cfg.battery_screen_type >= 0 && cfg.battery_screen_type < 2) ? cfg.battery_screen_type : 0;
    COOLANT_SCREEN = (cfg.coolant_screen_type >= 0 && cfg.coolant_screen_type < 2) ? cfg.coolant_screen_type : 0;
    IAT_SCREEN = (cfg.intake_temp_screen_type >= 0 && cfg.intake_temp_screen_type < 2) ? cfg.intake_temp_screen_type : 0;
    TICK_LINE_GAUGE = (cfg.tick_line_gauge > 0) ? cfg.tick_line_gauge : 2;
}

void restart_ESP()
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawStringCenter(35, "REBOOTING!");
    u8g2.sendBuffer();
    delay(1000);
    ESP.restart();
}

// ==== UI Draw Helpers ====
void draw_BottomText(String text)
{
    u8g2.setFont(u8g2_font_helvB08_tr);
    u8g2.setDrawColor(1);
    u8g2.drawFrame(0, 54, 128, 10);
    u8g2.setDrawColor(0);
    u8g2.drawBox(1, 55, 126, 8);
    u8g2.setDrawColor(1);
    drawStringCenter(63, text);
}

void displayInfo(String msg)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(30, "Info:");
    draw_BottomText(msg);
    u8g2.sendBuffer();
}

void displayError(String msg)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(30, "ERROR!");
    draw_BottomText(msg);
    u8g2.sendBuffer();
}

void draw_ScreenNumber(uint8_t index)
{
    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringLeft(0, 63, String(index + 1) + "/" + String(screenNumbers));
}

void draw_InfoText(String title, double value, String unit)
{
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawStringCenter(16, title);
    u8g2.setFont(u8g2_font_helvB18_tr);
    String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
    drawStringCenter(44, valStr + " " + unit);
}

// ==== Dynamic Menu Builder ====
void buildMenu()
{
    menuSize = 0;
    menuText[menuSize] = "Exit Menu";
    menuAction[menuSize++] = ACT_CLOSE;

    // 1. Toggle Display Style Option
    if (screenIndex != 0 && screenIndex != 6 && screenIndex != 7)
    {
        String s = "Type: Text";
        if ((screenIndex == 1 && BOOST_SCREEN) ||
            (screenIndex == 2 && IAT_SCREEN) ||
            (screenIndex == 3 && ENGLOAD_SCREEN) ||
            (screenIndex == 4 && BATTERY_SCREEN) ||
            (screenIndex == 5 && COOLANT_SCREEN))
        {
            s = "Type: Gauge";
        }
        menuText[menuSize] = s;
        menuAction[menuSize++] = ACT_TOGGLE_STYLE;
    }

    // 2. Min/Max specific to Boost
    if (screenIndex == 1)
    {
        menuText[menuSize] = "Min: " + String(TURBO_MIN_BAR, 1);
        menuAction[menuSize++] = ACT_EDIT_MIN;
        menuText[menuSize] = "Max: " + String(TURBO_MAX_BAR, 1);
        menuAction[menuSize++] = ACT_EDIT_MAX;
    }

    // 3. Action specific to DTC
    if (screenIndex == 6)
    {
        menuText[menuSize] = "Reset DTCs";
        menuAction[menuSize++] = ACT_RESET_DTC;
    }

    // 4. List all screens
    for (int i = 0; i < 8; i++)
    {
        menuText[menuSize] = "-> " + String(screenNames[i]);
        menuAction[menuSize++] = ACT_GO_SCREEN_0 + i;
    }
    menuCursor = 0;
}

// ==== Drawing Screens (Menu & Edit) ====
void drawMenuScreen()
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(12, "MENU");
    u8g2.drawLine(0, 14, 128, 14);

    u8g2.setFont(u8g2_font_helvB08_tr);
    int visibleItems = 3;
    int startIdx = 0;
    if (menuCursor >= visibleItems)
    {
        startIdx = menuCursor - visibleItems + 1;
    }

    for (int i = 0; i < visibleItems; i++)
    {
        int itemIdx = startIdx + i;
        if (itemIdx >= menuSize)
            break;

        int yPos = 28 + (i * 12);
        if (itemIdx == menuCursor)
        {
            u8g2.setDrawColor(1);
            u8g2.drawBox(2, yPos - 10, 124, 12);
            u8g2.setDrawColor(0);
        }
        else
        {
            u8g2.setDrawColor(1);
        }
        drawStringCenter(yPos, menuText[itemIdx]);
        u8g2.setDrawColor(1);
    }
    draw_BottomText("Short: Scroll | Long: OK");
}

void drawEditScreen(String title, float value)
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(20, title);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(45, String(value, 1));
    draw_BottomText("Short: +0.1 | Long: Save");
}

// ==== History Buffers & Gauge Drawing ====
#define AREA_CHART_HISTORY 105
struct AreaChartData
{
    double values[AREA_CHART_HISTORY];
    uint8_t currentIndex;
    bool initialized;
};
AreaChartData turboHistory = {{0}, 0, false};
AreaChartData loadHistory = {{0}, 0, false};
AreaChartData batteryHistory = {{0}, 0, false};
AreaChartData coolantHistory = {{0}, 0, false};
AreaChartData iatHistory = {{0}, 0, false};

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

void draw_AreaChartWithHistory(AreaChartData &history, double newValue, double minValue, double maxValue, String label, String unit)
{
    double valToStore = constrain(newValue, minValue, maxValue);
    history.values[history.currentIndex] = valToStore;
    history.currentIndex = (history.currentIndex + 1) % AREA_CHART_HISTORY;
    if (history.currentIndex == 0)
        history.initialized = true;

    int chartX = 20, chartY = 12, chartWidth = AREA_CHART_HISTORY, chartHeight = 35, baseY = chartY + chartHeight, labelX = 0;
    u8g2.drawFrame(chartX, chartY, chartWidth, chartHeight);
    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(10, label);

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

    int maxLabelY = chartY + 8, minLabelY = baseY;
    drawStringLeft(labelX, maxLabelY, alignSign(formatDecimal(maxValue, 1)));
    drawStringLeft(labelX, minLabelY, alignSign(formatDecimal(minValue, 1)));
    drawStringLeft(labelX, (maxLabelY + minLabelY) / 2 + 4, alignSign(formatDecimal(newValue, 1)));
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
}

// ==== OBD Fetching and Screens ====
// FIX: Trigger ELM327 request outside the success check so data actively updates
void draw_GaugeScreen(uint8_t index)
{
    float tempVal;

    switch (index)
    {
    case 0: // MAF
        tempVal = myELM327.manifoldPressure();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            mafPressure = tempVal;
        draw_InfoText("Pression MAF", mafPressure, "kPa");
        break;

    case 1: // Boost
        tempVal = myELM327.manifoldPressure();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            turboPressureState = (tempVal - 100) * 0.01;
        if (BOOST_SCREEN == 0)
            draw_InfoText("Pression Turbo", turboPressureState, "Bar");
        else
            draw_AreaChartWithHistory(turboHistory, turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Pression Turbo", "Bar");
        break;

    case 2: // IAT
        tempVal = myELM327.intakeAirTemp();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            intakeTemp = tempVal;
        if (IAT_SCREEN == 0)
            draw_InfoText("Temp admission", intakeTemp, "°C");
        else
            draw_AreaChartWithHistory(iatHistory, intakeTemp, -20.0, 60.0, "Temp admission", "°C");
        break;

    case 3: // Load
        tempVal = myELM327.engineLoad();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            engineLoad = tempVal;
        if (ENGLOAD_SCREEN == 0)
            draw_InfoText("Charge moteur", engineLoad, "%");
        else
            draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge moteur", "%");
        break;

    case 4: // Battery
        tempVal = myELM327.batteryVoltage();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            batteryVoltage = tempVal - 2.0;
        if (BATTERY_SCREEN == 0)
            draw_InfoText("Tension Bat", batteryVoltage, "V");
        else
            draw_AreaChartWithHistory(batteryHistory, batteryVoltage, 9.0, 15.0, "Tension Bat", "V");
        break;

    case 5: // Coolant
        tempVal = myELM327.engineCoolantTemp();
        if (myELM327.nb_rx_state == ELM_SUCCESS)
            coolantTemp = tempVal;
        if (COOLANT_SCREEN == 0)
            draw_InfoText("Temp LdR", coolantTemp, "°C");
        else
            draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "°C");
        break;

    case 6: // DTC
        myELM327.currentDTCCodes(false);
        u8g2.setFont(u8g2_font_helvB12_tr);
        drawStringCenter(16, "DTC Codes:");
        u8g2.setFont(u8g2_font_helvB18_tr);
        if (myELM327.DTC_Response.codesFound == 0)
            drawStringCenter(40, "No DTC Codes");
        else
            drawStringCenter(40, String(myELM327.DTC_Response.codesFound));
        draw_BottomText(version_string);
        draw_ScreenNumber(screenIndex);
        break;

    case 7: // Dash
        if (millis() - dashLastUpdate >= dashDelay)
        {
            dashLastUpdate = millis();
            switch (dashStep)
            {
            case 0:
                tempVal = myELM327.manifoldPressure();
                if (myELM327.nb_rx_state == ELM_SUCCESS)
                {
                    dashBoost = (tempVal - 100) * 0.01;
                    dashStep++;
                }
                break;
            case 1:
                tempVal = myELM327.intakeAirTemp();
                if (myELM327.nb_rx_state == ELM_SUCCESS)
                {
                    dashIAT = tempVal;
                    dashStep++;
                }
                break;
            case 2:
                tempVal = myELM327.engineCoolantTemp();
                if (myELM327.nb_rx_state == ELM_SUCCESS)
                {
                    dashCoolant = tempVal;
                    dashStep++;
                }
                break;
            case 3:
                tempVal = myELM327.engineLoad();
                if (myELM327.nb_rx_state == ELM_SUCCESS)
                {
                    dashLoad = tempVal;
                    dashStep = 0;
                }
                break;
            }
        }
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 12, "BOOST:");
        drawStringLeft(0, 22, String(dashBoost, 2) + " bar");
        drawStringLeft(64, 12, "IAT:");
        drawStringLeft(64, 22, String(dashIAT, 1) + " C");
        drawStringLeft(0, 34, "COOL:");
        drawStringLeft(0, 44, String(dashCoolant, 1) + " C");
        drawStringLeft(64, 34, "LOAD:");
        drawStringLeft(64, 44, String(dashLoad, 1) + " %");
        draw_BottomText(version_string);
        draw_ScreenNumber(screenIndex);
        break;
    }
}

// ==== Network & Captive Portal ====
void startCaptivePortal()
{
    WiFi.softAP(ssid, NULL);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    server.onNotFound([]()
                      { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
}

void startServer()
{
    startCaptivePortal();
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", generateWebPage()); });
    ElegantOTA.begin(&server);
    server.begin();
}

// ==== Setup ====
void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    delay(500);

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
    draw_BottomText(version_string);
    u8g2.sendBuffer();
    delay(1500);

    EEPROM.begin(EEPROM_SIZE);
    loadValues();
    if (!LittleFS.begin())
    {
        displayInfo("FS Error!");
        delay(2000);
        restart_ESP();
    }

    displayInfo("BT Init...");
    if (!SerialBT.begin("CANuSEE", true))
    {
        delay(2000);
        restart_ESP();
    }
    SerialBT.setPin(ELM327_BT_PIN);

    displayInfo("Connecting...");
    if (!SerialBT.connect(elm_address, sec_mask, role))
    {
        if (!SerialBT.connect("ELMULATOR"))
        {
            delay(2000);
            restart_ESP();
        }
    }

    displayInfo("ELM327 Init...");
    if (!myELM327.begin(SerialBT, false, 2000))
    {
        delay(2000);
        restart_ESP();
    }
    myELM327.sendCommand(SET_ISO_BAUD_10400);
    myELM327.sendCommand(ALLOW_LONG_MESSAGES);

    startServer();
}

// ==== Main Loop ====
void loop()
{
    server.handleClient();
    ElegantOTA.loop();
    dnsServer.processNextRequest();

    static int buttonState = HIGH;
    static int lastButtonState = HIGH;
    static unsigned long lastDebounceTime = 0;
    static unsigned long pressStartTime = 0;
    static bool longPressTriggered = false;

    int reading = digitalRead(BUTTON_PIN);
    if (reading != lastButtonState)
    {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > 50)
    {
        if (reading != buttonState)
        {
            buttonState = reading;
            if (buttonState == LOW)
            {
                pressStartTime = millis();
                longPressTriggered = false;
            }
            else
            {
                if (!longPressTriggered)
                {
                    // --- SHORT PRESS ACTIONS ---
                    if (currentState == STATE_GAUGES)
                    {
                        screenIndex = (screenIndex + 1) % 8;
                        saveValues();
                    }
                    else if (currentState == STATE_MENU)
                    {
                        menuCursor = (menuCursor + 1) % menuSize;
                    }
                    else if (currentState == STATE_EDIT_MIN)
                    {
                        TURBO_MIN_BAR += 0.1;
                        if (TURBO_MIN_BAR > 0.5)
                            TURBO_MIN_BAR = -1.0;
                    }
                    else if (currentState == STATE_EDIT_MAX)
                    {
                        TURBO_MAX_BAR += 0.1;
                        if (TURBO_MAX_BAR > 3.0)
                            TURBO_MAX_BAR = 0.5;
                    }
                }
            }
        }
        else if (buttonState == LOW && !longPressTriggered)
        {
            if ((millis() - pressStartTime) > 800)
            {
                longPressTriggered = true;

                // --- LONG PRESS ACTIONS ---
                if (currentState == STATE_GAUGES)
                {
                    buildMenu();
                    currentState = STATE_MENU;
                }
                else if (currentState == STATE_MENU)
                {
                    int action = menuAction[menuCursor];

                    if (action == ACT_CLOSE)
                    {
                        currentState = STATE_GAUGES;
                    }
                    else if (action == ACT_TOGGLE_STYLE)
                    {
                        if (screenIndex == 1)
                            BOOST_SCREEN = !BOOST_SCREEN;
                        if (screenIndex == 2)
                            IAT_SCREEN = !IAT_SCREEN;
                        if (screenIndex == 3)
                            ENGLOAD_SCREEN = !ENGLOAD_SCREEN;
                        if (screenIndex == 4)
                            BATTERY_SCREEN = !BATTERY_SCREEN;
                        if (screenIndex == 5)
                            COOLANT_SCREEN = !COOLANT_SCREEN;
                        saveValues();
                        buildMenu(); // Refresh the menu text
                    }
                    else if (action == ACT_EDIT_MIN)
                    {
                        currentState = STATE_EDIT_MIN;
                    }
                    else if (action == ACT_EDIT_MAX)
                    {
                        currentState = STATE_EDIT_MAX;
                    }
                    else if (action == ACT_RESET_DTC)
                    {
                        displayInfo("Resetting...");
                        delay(1000);
                        myELM327.resetDTC();
                        currentState = STATE_GAUGES;
                    }
                    else if (action >= ACT_GO_SCREEN_0)
                    {
                        screenIndex = action - ACT_GO_SCREEN_0;
                        saveValues();
                        currentState = STATE_GAUGES;
                    }
                }
                else if (currentState == STATE_EDIT_MIN || currentState == STATE_EDIT_MAX)
                {
                    saveValues();
                    buildMenu();
                    currentState = STATE_MENU; // Go back to menu when done editing
                }
            }
        }
    }
    lastButtonState = reading;

    // ==== Screen Drawing Router ====
    u8g2.clearBuffer();
    if (currentState == STATE_GAUGES)
    {
        draw_GaugeScreen(screenIndex);
    }
    else if (currentState == STATE_MENU)
    {
        drawMenuScreen();
    }
    else if (currentState == STATE_EDIT_MIN)
    {
        drawEditScreen("Edit Turbo Min", TURBO_MIN_BAR);
    }
    else if (currentState == STATE_EDIT_MAX)
    {
        drawEditScreen("Edit Turbo Max", TURBO_MAX_BAR);
    }
    u8g2.sendBuffer();

    delay(10);
}