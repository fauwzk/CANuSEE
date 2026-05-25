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

// ==== Web Server ====
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

// ==== EEPROM Setup ====
#define EEPROM_SIZE sizeof(Settings)
Settings cfg;

int BOOST_SCREEN = 0;   // 0 = text, 1 = gauge
int ENGLOAD_SCREEN = 0; // 0 = text, 1 = gauge
int BATTERY_SCREEN = 0; // 0 = text, 1 = gauge
int COOLANT_SCREEN = 0; // 0 = text, 1 = gauge
int IAT_SCREEN = 0;     // 0 = text, 1 = gauge
int TICK_LINE_GAUGE = 2;

// ==== Dashboard refresh state ====
uint8_t dashStep = 0;
unsigned long dashLastUpdate = 0;
const unsigned long dashDelay = 10;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashLoad = 0;

String version_string = "CANuSEE " FW_VERSION;

// ==== OLED (U8g2) ====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);

// ==== ELM327 Classic Bluetooth ====
BluetoothSerial SerialBT;
#define ELM_PORT SerialBT
#define DEBUG_PORT Serial
#define ELM327_BT_PIN "1234"
ELM327 myELM327;
uint8_t elm_address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// ==== Display center coordinates ====
const int centerX = 64;
const int screenNumbers = 8;
uint8_t screenIndex = 0;
uint8_t ScreenTypes = 2;
unsigned long lastSwitch = 0;

// ==== Default Turbo gauge limits ====
float TURBO_MIN_BAR = -0.7;
float TURBO_MAX_BAR = 1.5;

// ==== Debounce ====
#define BUTTON_PIN 32
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;
const unsigned long longPressDuration = 1000;

// ==== Bluetooth setup ====
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE;
esp_spp_role_t role = ESP_SPP_ROLE_MASTER;

// ==== Variables for OBD-II data ====
float atmo_kpa, maf_kpa, coolant_temp, intake_temp, engine_load, battery_voltage, turbo_pressure;
float atmoPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0;
float coolantTemp = 0.0, batteryVoltage = 0.0, turboPressureState = 0.0;

// ==== U8g2 Text Alignment Helpers ====
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

    html.replace("%MIN%", String(TURBO_MIN_BAR));
    html.replace("%MAX%", String(TURBO_MAX_BAR));
    html.replace("%VERSION%", version_string);
    html.replace("%SELECTED_BOOST_TEXT%", (BOOST_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_BOOST_GAUGE%", (BOOST_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_LOAD_TEXT%", (ENGLOAD_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_LOAD_GAUGE%", (ENGLOAD_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_VOLTAGE_TEXT%", (BATTERY_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_VOLTAGE_GAUGE%", (BATTERY_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_COOLANT_TEXT%", (COOLANT_SCREEN == 0) ? "selected" : "");
    html.replace("%SELECTED_COOLANT_GAUGE%", (COOLANT_SCREEN == 1) ? "selected" : "");
    html.replace("%SELECTED_IAT_TEXT%", (IAT_SCREEN == 0) ? "selected" : "");
    html.replace("%TICKS%", String(TICK_LINE_GAUGE));
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
    BOOST_SCREEN = (cfg.boost_screen_type >= 0 && cfg.boost_screen_type < ScreenTypes) ? cfg.boost_screen_type : 0;
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = (cfg.engload_screen_type >= 0 && cfg.engload_screen_type < ScreenTypes) ? cfg.engload_screen_type : 0;
    BATTERY_SCREEN = (cfg.battery_screen_type >= 0 && cfg.battery_screen_type < ScreenTypes) ? cfg.battery_screen_type : 0;
    COOLANT_SCREEN = (cfg.coolant_screen_type >= 0 && cfg.coolant_screen_type < ScreenTypes) ? cfg.coolant_screen_type : 0;
    IAT_SCREEN = (cfg.intake_temp_screen_type >= 0 && cfg.intake_temp_screen_type < ScreenTypes) ? cfg.intake_temp_screen_type : 0;
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
    u8g2.setFont(u8g2_font_helvB08_tr); // ~10px
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
    u8g2.setFont(u8g2_font_helvB18_tr); // ~24px
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
    u8g2.setFont(u8g2_font_helvB12_tr); // ~16px
    drawStringCenter(16, title);
    u8g2.setFont(u8g2_font_helvB18_tr); // ~24px

    String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
    drawStringCenter(44, valStr + " " + unit);
}

// ==== History Buffers & Formatters ====
#define AREA_CHART_HISTORY 105
struct AreaChartData
{
    double values[AREA_CHART_HISTORY];
    uint8_t currentIndex;
    bool initialized;
};

String alignSign(String value)
{
    if (!value.startsWith("-"))
        return " " + value;
    return value;
}

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

AreaChartData turboHistory = {{0}, 0, false};
AreaChartData loadHistory = {{0}, 0, false};
AreaChartData batteryHistory = {{0}, 0, false};
AreaChartData coolantHistory = {{0}, 0, false};
AreaChartData iatHistory = {{0}, 0, false};

void addValueToHistory(AreaChartData &history, double value, double minValue, double maxValue)
{
    if (value < minValue)
        value = minValue;
    if (value > maxValue)
        value = maxValue;
    history.values[history.currentIndex] = value;
    history.currentIndex = (history.currentIndex + 1) % AREA_CHART_HISTORY;
    if (history.currentIndex == 0)
        history.initialized = true;
}

void draw_AreaChartWithHistory(AreaChartData &history, double newValue, double minValue, double maxValue, String label, String unit)
{
    addValueToHistory(history, newValue, minValue, maxValue);

    int chartX = 20, chartY = 12, chartWidth = AREA_CHART_HISTORY, chartHeight = 35;
    int baseY = chartY + chartHeight;
    int labelX = 0;

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

        double val = history.values[historyIdx];
        val = constrain(val, minValue, maxValue);

        double normalizedVal = (val - minValue) / range;
        int pixelHeight = (int)(chartHeight * normalizedVal);
        int pixelY = baseY - pixelHeight;

        u8g2.drawLine(chartX + i, baseY, chartX + i, pixelY);
    }

    int maxLabelY = chartY + 8;
    int minLabelY = baseY;
    drawStringLeft(labelX, maxLabelY, alignSign(formatDecimal(maxValue, 1)));
    drawStringLeft(labelX, minLabelY, alignSign(formatDecimal(minValue, 1)));

    int valueCenterY = (maxLabelY + minLabelY) / 2 + 4;
    drawStringLeft(labelX, valueCenterY, alignSign(formatDecimal(newValue, 1)));

    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
}

void draw_LineGauge(double value, double minValue, double maxValue, String label, String unit)
{
    double barValue = constrain(value, minValue, maxValue);
    int barX = 14, barY = 14, barWidth = 100, barHeight = 10;
    double range = maxValue - minValue;
    double fillPercent = (barValue - minValue) / range;
    int fillWidth = (int)(barWidth * fillPercent);

    u8g2.drawFrame(barX, barY, barWidth, barHeight);
    u8g2.drawBox(barX, barY, fillWidth, barHeight);

    int tickCount = TICK_LINE_GAUGE;
    int tickHeight = 2;
    int labelOffsetY = barY + barHeight + 10;

    for (int i = 0; i <= tickCount; i++)
    {
        int tickX = (i == 0) ? barX : barX + (barWidth * i) / tickCount - 1;
        u8g2.drawLine(tickX, barY + barHeight, tickX, barY + barHeight + tickHeight);

        if (i == 0 || i == tickCount || i % 2 == 0)
        {
            double tickValue = minValue + (range * i / tickCount);
            u8g2.setFont(u8g2_font_helvB08_tr);
            int w = u8g2.getStrWidth(String(tickValue, 1).c_str());
            u8g2.setCursor(tickX - (w / 2), labelOffsetY);
            u8g2.print(String(tickValue, 1));
        }
    }

    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(10, label);
    drawStringCenter(labelOffsetY + 12, String(value, 1) + " " + unit);

    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
}

// ==== OBD Fetching and Screens ====

void draw_MAFScreen()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        mafPressure = myELM327.manifoldPressure();
    draw_InfoText("Pression MAF", mafPressure, "kPa");
}

void draw_BoostScreens()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        turboPressureState = (myELM327.manifoldPressure() - 100) * 0.01;
    if (BOOST_SCREEN == 0)
        draw_InfoText("Pression Turbo", turboPressureState, "Bar");
    else
        draw_AreaChartWithHistory(turboHistory, turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Pression Turbo", "Bar");
}

void draw_IntakeTempScreen()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        intakeTemp = myELM327.intakeAirTemp();
    if (IAT_SCREEN == 0)
        draw_InfoText("Temp admission", intakeTemp, "°C");
    else
        draw_AreaChartWithHistory(iatHistory, intakeTemp, -20.0, 60.0, "Temp admission", "°C");
}

void draw_EngLoadScreens()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        engineLoad = myELM327.engineLoad();
    if (ENGLOAD_SCREEN == 0)
        draw_InfoText("Charge moteur", engineLoad, "%");
    else
        draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge moteur", "%");
}

void draw_BatteryVoltageScreens()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        batteryVoltage = myELM327.batteryVoltage() - 2.0;
    if (BATTERY_SCREEN == 0)
        draw_InfoText("Tension Bat", batteryVoltage, "V");
    else
        draw_AreaChartWithHistory(batteryHistory, batteryVoltage, 9.0, 15.0, "Tension Bat", "V");
}

void draw_CoolantTempScreens()
{
    if (myELM327.nb_rx_state == ELM_SUCCESS)
        coolantTemp = myELM327.engineCoolantTemp();
    if (COOLANT_SCREEN == 0)
        draw_InfoText("Temp LdR", coolantTemp, "°C");
    else
        draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "°C");
}

void draw_dtcCodes()
{
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
}

void updateDashboardSequential()
{
    if (millis() - dashLastUpdate < dashDelay)
        return;
    dashLastUpdate = millis();
    switch (dashStep)
    {
    case 0:
        if (myELM327.nb_rx_state == ELM_SUCCESS)
        {
            dashBoost = (myELM327.manifoldPressure() - 100) * 0.01;
            dashStep++;
        }
        break;
    case 1:
        if (myELM327.nb_rx_state == ELM_SUCCESS)
        {
            dashIAT = myELM327.intakeAirTemp();
            dashStep++;
        }
        break;
    case 2:
        if (myELM327.nb_rx_state == ELM_SUCCESS)
        {
            dashCoolant = myELM327.engineCoolantTemp();
            dashStep++;
        }
        break;
    case 3:
        if (myELM327.nb_rx_state == ELM_SUCCESS)
        {
            dashLoad = myELM327.engineLoad();
            dashStep = 0;
        }
        break;
    }
}

void drawDashboardScreen()
{
    u8g2.setFont(u8g2_font_helvB08_tr);
    int col1 = 0, col2 = 64;
    int row1 = 12, row2 = 34; // Baseline shifted down

    drawStringLeft(col1, row1, "BOOST:");
    drawStringLeft(col1, row1 + 10, String(dashBoost, 2) + " bar");
    drawStringLeft(col2, row1, "IAT:");
    drawStringLeft(col2, row1 + 10, String(dashIAT, 1) + " C");

    drawStringLeft(col1, row2, "COOLANT:");
    drawStringLeft(col1, row2 + 10, String(dashCoolant, 1) + " C");
    drawStringLeft(col2, row2, "ENG LOAD:");
    drawStringLeft(col2, row2 + 10, String(dashLoad, 1) + " %");

    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
}

void draw_GaugeScreen(uint8_t index)
{
    switch (index)
    {
    case 0:
        draw_MAFScreen();
        break;
    case 1:
        draw_BoostScreens();
        break;
    case 2:
        draw_IntakeTempScreen();
        break;
    case 3:
        draw_EngLoadScreens();
        break;
    case 4:
        draw_BatteryVoltageScreens();
        break;
    case 5:
        draw_CoolantTempScreens();
        break;
    case 6:
        draw_dtcCodes();
        break;
    case 7:
        updateDashboardSequential();
        drawDashboardScreen();
        break;
    default:
        displayError("Screen Error");
        delay(1000);
        restart_ESP();
        break;
    }
}

void resetDTCs()
{
    displayInfo("Resetting DTCs...");
    myELM327.resetDTC();
    delay(1000);
}

// ==== Visual Effects ====
void fadeTransition(uint8_t nextScreen)
{
    for (int i = 0; i < 10; i++)
    {
        u8g2.setContrast(255 - (i * 25));
        delay(20);
    }
    u8g2.setPowerSave(1);
    u8g2.clearBuffer();
    draw_GaugeScreen(nextScreen);
    u8g2.sendBuffer();
    u8g2.setPowerSave(0);
    for (int i = 0; i < 10; i++)
    {
        u8g2.setContrast(i * 25);
        delay(20);
    }
    u8g2.setContrast(255);
}

void drawHeartbeatSpinner()
{
    static uint8_t spinnerIndex = 0;
    static unsigned long lastUpdate = 0;
    const unsigned long spinnerInterval = 200;
    const char spinnerChars[] = {'|', '/', '-', '\\'};

    if (millis() - lastUpdate >= spinnerInterval)
    {
        lastUpdate = millis();
        int x = 122, y = 63;
        u8g2.setDrawColor(0);
        u8g2.drawBox(x, 55, 6, 8);
        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(x, y, String(spinnerChars[spinnerIndex]));
        spinnerIndex = (spinnerIndex + 1) % 4;
    }
}

// ==== Captive Portal & Web ====
void startCaptivePortal()
{
    WiFi.softAP(ssid, NULL);
    IPAddress myIP = WiFi.softAPIP();
    dnsServer.start(DNS_PORT, "*", myIP);
    server.onNotFound([]()
                      {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", ""); });
    server.begin();
}

void startServer()
{
    startCaptivePortal();
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", generateWebPage()); });

    server.on("/save", HTTP_POST, []()
              {
    if (server.hasArg("boost_min") && server.hasArg("ticks")) {
      TURBO_MIN_BAR = server.arg("boost_min").toFloat();
      TURBO_MAX_BAR = server.arg("boost_max").toFloat();
      BOOST_SCREEN = server.arg("boost_gauge_type").toInt();
      ENGLOAD_SCREEN = server.arg("engload_gauge_type").toInt();
      BATTERY_SCREEN = server.arg("voltage_gauge_type").toInt();
      COOLANT_SCREEN = server.arg("coolant_gauge_type").toInt();
      TICK_LINE_GAUGE = server.arg("ticks").toInt();
      saveValues();
      server.send(200, "text/html", "<html><body><h3>Saved!</h3><a href='/'>Back</a></body></html>");
    } else {
      server.send(400, "text/plain", "Missing parameters");
    } });

    server.on("/nextpage", HTTP_GET, []()
              {
    screenIndex = (screenIndex + 1) % screenNumbers;
    fadeTransition(screenIndex);
    cfg.last_screen = screenIndex;
    EEPROM.put(0, cfg);
    EEPROM.commit();
    server.send(200, "text/html", "<html><body><h3>Page Changed!</h3><a href='/'>Back</a></body></html>"); });

    server.on("/reset", HTTP_GET, []()
              {
    server.send(200, "text/html", "<html><body><h3>Device Resetting...</h3></body></html>");
    delay(1000);
    restart_ESP(); });

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
    u8g2.setContrast(0);
    u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
    draw_BottomText(version_string);
    u8g2.sendBuffer();

    for (int b = 0; b <= 255; b += 25)
    {
        u8g2.setContrast(b);
        delay(30);
    }
    delay(500);

    EEPROM.begin(EEPROM_SIZE);
    loadValues();

    if (!LittleFS.begin())
    {
        displayInfo("FS Error!");
        delay(2000);
        restart_ESP();
    }

    // Setup mode override via button
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        displayInfo("Setup Mode");
        delay(2000);
        startServer();
        while (true)
        {
            if (digitalRead(BUTTON_PIN) == LOW)
            {
                displayInfo("Exiting Setup");
                delay(1000);
                restart_ESP();
            }
            server.handleClient();
            ElegantOTA.loop();
            dnsServer.processNextRequest();
            drawHeartbeatSpinner();
            u8g2.sendBuffer();
            delay(10);
        }
    }

    // Bluetooth Init
    displayInfo("BT Init...");
    if (!SerialBT.begin("CANuSEE", true))
    {
        displayError("BT INIT FAIL");
        delay(10000);
        restart_ESP();
    }
    SerialBT.setPin(ELM327_BT_PIN);

    displayInfo("BT Connecting...");
    if (!SerialBT.connect(elm_address, sec_mask, role))
    {
        if (!SerialBT.connect("ELMULATOR"))
        {
            displayError("BT Conn FAIL");
            delay(5000);
            restart_ESP();
        }
    }

    displayInfo("ELM327 Init...");
    if (!myELM327.begin(SerialBT, false, 2000))
    {
        displayError("ELM FAIL");
        delay(10000);
        restart_ESP();
    }

    myELM327.sendCommand(SET_ISO_BAUD_10400);
    myELM327.sendCommand(ALLOW_LONG_MESSAGES);

    startServer();
    fadeTransition(screenIndex);
}

// ==== Main Loop ====
void loop()
{
    server.handleClient();
    ElegantOTA.loop();
    dnsServer.processNextRequest();

    static bool buttonPressed = false;
    static unsigned long buttonPressTime = 0;
    static bool longPressHandled = false;

    bool buttonState = (digitalRead(BUTTON_PIN) == LOW); // active LOW

    if (buttonState && !buttonPressed)
    {
        buttonPressed = true;
        buttonPressTime = millis();
        longPressHandled = false;
    }

    if (!buttonState && buttonPressed)
    {
        unsigned long pressDuration = millis() - buttonPressTime;
        buttonPressed = false;

        if (pressDuration < longPressDuration && (millis() - lastButtonPress) > debounceDelay)
        {
            // SHORT PRESS (Next Screen)
            lastButtonPress = millis();
            myELM327.response = 0;
            delay(100);
            screenIndex = (screenIndex + 1) % screenNumbers;
            fadeTransition(screenIndex);
            cfg.last_screen = screenIndex;
            EEPROM.put(0, cfg);
            EEPROM.commit();
            lastSwitch = millis();
        }
    }

    if (buttonPressed && !longPressHandled)
    {
        if (millis() - buttonPressTime > longPressDuration)
        {
            // LONG PRESS (Toggle specific mode)
            if (screenIndex == 1)
            {
                BOOST_SCREEN = (BOOST_SCREEN + 1) % ScreenTypes;
                cfg.boost_screen_type = BOOST_SCREEN;
            }
            else if (screenIndex == 3)
            {
                ENGLOAD_SCREEN = (ENGLOAD_SCREEN + 1) % ScreenTypes;
                cfg.engload_screen_type = ENGLOAD_SCREEN;
            }
            else if (screenIndex == 4)
            {
                BATTERY_SCREEN = (BATTERY_SCREEN + 1) % ScreenTypes;
                cfg.battery_screen_type = BATTERY_SCREEN;
            }
            else if (screenIndex == 5)
            {
                COOLANT_SCREEN = (COOLANT_SCREEN + 1) % ScreenTypes;
                cfg.coolant_screen_type = COOLANT_SCREEN;
            }
            else if (screenIndex == 6)
            {
                resetDTCs();
            }
            else
            {
                displayInfo("Rebooting...");
                delay(1000);
                restart_ESP();
            }

            EEPROM.put(0, cfg);
            EEPROM.commit();
            longPressHandled = true;

            // Force instant redraw of toggled screen
            if (screenIndex != 6)
            {
                u8g2.clearBuffer();
                draw_GaugeScreen(screenIndex);
                u8g2.sendBuffer();
            }
        }
    }

    u8g2.clearBuffer();
    draw_GaugeScreen(screenIndex);
    u8g2.sendBuffer();

    delay(10);
}