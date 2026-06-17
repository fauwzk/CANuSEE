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

// ==== Driver CAN (TWAI) natif ESP32 ====
#include "driver/twai.h"

// ==== Pins Configuration (ESP32-C3 Super Mini) ====
#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 6
#define BTN_MENU 3

#define CAN_RX_PIN 10
#define CAN_TX_PIN 7

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
  int target_speed;
  int brightness;
};

#define EEPROM_SIZE sizeof(Settings)
Settings cfg;

int BOOST_SCREEN = 0;
int ENGLOAD_SCREEN = 0;
int BATTERY_SCREEN = 0;
int COOLANT_SCREEN = 0;
int IAT_SCREEN = 0;
int TICK_LINE_GAUGE = 2;
int TARGET_SPEED = 100;
int OLED_BRIGHTNESS = 255;

// ==== State Machine ====
enum AppState
{
  STATE_GAUGES,
  STATE_MENU,
  STATE_EDIT_MIN,
  STATE_EDIT_MAX,
  STATE_EDIT_SPEED,
  STATE_EDIT_BRIGHTNESS,
  STATE_CONFIG
};
AppState currentState = STATE_GAUGES;

// ==== OTA State ====
bool ota_updating = false;

// ==== Dashboard refresh state ====
uint8_t dashStep = 0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashLoad = 0;

String version_string = "CANuSEE " FW_VERSION;

// ==== OLED (U8g2) - Pins 21 (SCL) et 20 (SDA) ====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);

// ==== Coordinates & State ====
const int centerX = 64;
const int screenNumbers = 11;
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7;
float TURBO_MAX_BAR = 1.5;

// ==== Raw CAN State ====
uint32_t lastRawId = 0;
uint8_t lastRawDlc = 0;
uint8_t lastRawData[8] = {0};

// ==== Variables Capteurs ====
float atmoPressure = 0.0, mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0;
float coolantTemp = 0.0, batteryVoltage = 0.0, turboPressureState = 0.0;

// ==== Variables DTC ====
String dtcCodes[15];
int dtcCountFound = 0;

// ==== Timer State ====
bool timerRunning = false;
bool timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0;
float currentSpeed = 0.0;

// ==== Dynamic Menu Variables ====
#define MAX_MENU_ITEMS 24
String menuText[MAX_MENU_ITEMS];
int menuAction[MAX_MENU_ITEMS];
int menuSize = 0;
int menuCursor = 0;

#define ACT_CLOSE 0
#define ACT_TOGGLE_STYLE 1
#define ACT_EDIT_MIN 2
#define ACT_EDIT_MAX 3
#define ACT_RESET_DTC 4
#define ACT_EDIT_SPEED 5
#define ACT_EDIT_BRIGHTNESS 6
#define ACT_ENTER_CONFIG 7
#define ACT_GO_SCREEN_0 10

const char *screenNames[] = {"MAF", "Boost", "IAT", "Load", "Battery", "Coolant", "DTC", "Dash", "Timer", "Speed", "Raw"};

// ==== Boutons Struct ====
struct Button
{
  uint8_t pin;
  bool state;
  bool lastState;
  unsigned long lastDebounceTime;

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

Button btnUp = {BTN_UP, false, false, 0};
Button btnDown = {BTN_DOWN, false, false, 0};
Button btnOk = {BTN_OK, false, false, 0};
Button btnMenu = {BTN_MENU, false, false, 0};

// ==== CAN Polling Variables ====
const uint8_t OBD_PIDS[] = {0x01, 0x04, 0x05, 0x0B, 0x0D, 0x0F, 0x42};
const uint8_t NUM_PIDS = sizeof(OBD_PIDS) / sizeof(OBD_PIDS[0]);
uint8_t currentPidIndex = 0;
unsigned long lastCanRequest = 0;

// ------------------------------------------------------------------------------------------------

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

// ==== HTML Generator ====
String generateWebPage()
{
  File file = LittleFS.open("/index.html", "r");
  if (!file)
    return "<html><body><h3>File not found</h3></body></html>";
  String html = file.readString();
  file.close();

  html.reserve(html.length() + 1024);
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
  html.replace("%SELECTED_IAT_GAUGE%", (IAT_SCREEN == 1) ? "selected" : "");
  html.replace("%TICKS%", String(TICK_LINE_GAUGE));
  html.replace("%MAX_SPEED%", String(TARGET_SPEED));

  int brightnessPct = map(OLED_BRIGHTNESS, 0, 255, 0, 100);
  html.replace("%BRIGHTNESS_PCT%", String(brightnessPct));
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
  cfg.target_speed = TARGET_SPEED;
  cfg.brightness = OLED_BRIGHTNESS;
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

void draw_BottomText(String text)
{
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 48, 128, 16);
  u8g2.setDrawColor(1);

  if (text == version_string)
  {
    drawStringCenter(60, "CANuSEE");
    drawStringRight(126, 60, String(FW_VERSION));
  }
  else
  {
    drawStringCenter(60, text);
  }
}

void displayInfo(String msg)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvR18_tr);
  drawStringCenter(30, "Info:");
  draw_BottomText(msg);
  u8g2.sendBuffer();
}

void draw_ScreenNumber(uint8_t index)
{
  u8g2.setFont(u8g2_font_5x7_tr);
  drawStringLeft(2, 60, String(index + 1) + "/" + String(screenNumbers));
}

void draw_InfoText(String title, double value, String unit)
{
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
  u8g2.setFont(u8g2_font_helvR12_tr);
  drawStringCenter(16, title);
  u8g2.setFont(u8g2_font_helvR18_tr);
  String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
  drawStringCenter(40, valStr + " " + unit);
}

// ==== Dynamic Menu Builder ====
void buildMenu()
{
  menuSize = 0;
  menuText[menuSize] = "Exit Menu";
  menuAction[menuSize++] = ACT_CLOSE;

  menuText[menuSize] = "Mode Config";
  menuAction[menuSize++] = ACT_ENTER_CONFIG;

  menuText[menuSize] = "Brightness";
  menuAction[menuSize++] = ACT_EDIT_BRIGHTNESS;

  if (screenIndex != 0 && screenIndex != 6 && screenIndex != 7 && screenIndex != 8)
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

  if (screenIndex == 1)
  {
    menuText[menuSize] = "Min: " + String(TURBO_MIN_BAR, 1);
    menuAction[menuSize++] = ACT_EDIT_MIN;
    menuText[menuSize] = "Max: " + String(TURBO_MAX_BAR, 1);
    menuAction[menuSize++] = ACT_EDIT_MAX;
  }

  if (screenIndex == 6)
  {
    menuText[menuSize] = "Reset DTCs";
    menuAction[menuSize++] = ACT_RESET_DTC;
  }

  if (screenIndex == 8)
  {
    menuText[menuSize] = "Target: " + String(TARGET_SPEED);
    menuAction[menuSize++] = ACT_EDIT_SPEED;
  }

  for (int i = 0; i < screenNumbers; i++)
  {
    menuText[menuSize] = "-> " + String(screenNames[i]);
    menuAction[menuSize++] = ACT_GO_SCREEN_0 + i;
  }
  menuCursor = 0;
}

void drawMenuScreen()
{
  u8g2.setFont(u8g2_font_helvR10_tr);
  drawStringCenter(12, "MENU");
  u8g2.drawLine(0, 14, 128, 14);

  u8g2.setFont(u8g2_font_helvR08_tr);
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

    int yPos = 24 + (i * 11);
    if (itemIdx == menuCursor)
    {
      u8g2.setDrawColor(1);
      u8g2.drawBox(2, yPos - 9, 124, 11);
      u8g2.setDrawColor(0);
    }
    else
      u8g2.setDrawColor(1);

    drawStringCenter(yPos, menuText[itemIdx]);
    u8g2.setDrawColor(1);
  }
  draw_BottomText("U/D:Sel OK:Do M:Exit");
}

void drawEditScreen(String title, String valueStr, String instruction)
{
  u8g2.setFont(u8g2_font_helvR10_tr);
  drawStringCenter(16, title);
  u8g2.setFont(u8g2_font_helvR18_tr);
  drawStringCenter(38, valueStr);
  draw_BottomText(instruction);
}

void drawConfigScreen()
{
  u8g2.setFont(u8g2_font_helvB10_tr);
  drawStringCenter(14, "MODE CONFIG");
  u8g2.drawLine(0, 18, 128, 18);
  u8g2.setFont(u8g2_font_helvR08_tr);
  drawStringCenter(30, "WiFi: " + String(ssid));
  drawStringCenter(42, "IP: 192.168.4.1");
  draw_BottomText("Appui Menu pour Quitter");
}

// ==== History Buffers & Gauge Drawing ====
#define AREA_CHART_HISTORY 94
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

  int chartX = 32;
  int chartY = 12;
  int chartWidth = AREA_CHART_HISTORY;
  int chartHeight = 32;
  int baseY = chartY + chartHeight;

  u8g2.drawFrame(chartX, chartY, chartWidth, chartHeight);
  u8g2.setFont(u8g2_font_helvR08_tr);
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

  int maxLabelY = chartY + 8;
  int minLabelY = baseY;
  int valCenterY = chartY + (chartHeight / 2) + 4;
  int alignBorderX = chartX - 2;

  drawStringRight(alignBorderX, maxLabelY, alignSign(formatDecimal(maxValue, 1)));
  drawStringRight(alignBorderX, minLabelY, alignSign(formatDecimal(minValue, 1)));
  drawStringRight(alignBorderX, valCenterY, alignSign(formatDecimal(newValue, 1)));

  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
}

// ==== CAN OBD2 Functions ====
void requestOBDPID(uint8_t pid)
{
  twai_message_t message = {0};
  message.identifier = 0x7DF;
  message.extd = 0;
  message.data_length_code = 8;
  message.data[0] = 0x02;
  message.data[1] = 0x01;
  message.data[2] = pid;
  for (int i = 3; i < 8; i++)
    message.data[i] = 0xAA;

  twai_transmit(&message, pdMS_TO_TICKS(10));
}

void clearDTC()
{
  twai_message_t message = {0};
  message.identifier = 0x7DF;
  message.extd = 0;
  message.data_length_code = 8;
  message.data[0] = 0x01;
  message.data[1] = 0x04;
  for (int i = 2; i < 8; i++)
    message.data[i] = 0xAA;

  twai_transmit(&message, pdMS_TO_TICKS(10));
}

void fetchOBDData()
{
  twai_status_info_t status_info;
  twai_get_status_info(&status_info);

  if (status_info.state == TWAI_STATE_BUS_OFF)
  {
    twai_initiate_recovery();
    return;
  }
  if (status_info.state == TWAI_STATE_STOPPED)
  {
    twai_start();
    return;
  }
  if (status_info.state != TWAI_STATE_RUNNING)
  {
    return;
  }

  // 1. On ralentit la cadence à 250ms pour ne pas saturer le calculateur
  if (millis() - lastCanRequest > 250)
  {
    requestOBDPID(OBD_PIDS[currentPidIndex]);
    currentPidIndex = (currentPidIndex + 1) % NUM_PIDS;
    lastCanRequest = millis();
  }

  twai_message_t message;
  while (twai_receive(&message, 0) == ESP_OK)
  {
    // --- Sauvegarde pour l'écran RAW ---
    lastRawId = message.identifier;
    lastRawDlc = message.data_length_code;
    for (int i = 0; i < 8; i++)
    {
      lastRawData[i] = (i < lastRawDlc) ? message.data[i] : 0;
    }

    if (message.identifier >= 0x7E8 && message.identifier <= 0x7EF)
    {
      if (message.data[1] == 0x41)
      {
        uint8_t pid = message.data[2];
        float A = message.data[3];
        float B = message.data[4];

        switch (pid)
        {
        case 0x01:
          dtcCountFound = message.data[3] & 0x7F;
          break;
        case 0x04:
          engineLoad = (A * 100.0) / 255.0;
          dashLoad = engineLoad;
          break;
        case 0x05:
          coolantTemp = A - 40;
          dashCoolant = coolantTemp;
          break;
        case 0x0B:
          mafPressure = A;
          turboPressureState = (A - 100) * 0.01;
          dashBoost = turboPressureState;
          break;
        case 0x0D:
          currentSpeed = A;
          if (screenIndex == 8)
          {
            if (currentSpeed <= 0)
            {
              timerReady = true;
              timerRunning = false;
            }
            else if (timerReady && !timerRunning && currentSpeed > 0)
            {
              timerRunning = true;
              speedTimerStart = millis();
            }
            else if (timerRunning && currentSpeed >= TARGET_SPEED)
            {
              timerRunning = false;
              timerReady = false;
              lastTimerValue = (millis() - speedTimerStart) / 1000.0;
            }
          }
          break;
        case 0x0F:
          intakeTemp = A - 40;
          dashIAT = intakeTemp;
          break;
        case 0x42:
          batteryVoltage = ((A * 256) + B) / 1000.0;
          break;
        }
      }
    }
  }
}

void draw_GaugeScreen(uint8_t index)
{
  switch (index)
  {
  case 0:
    draw_InfoText("Pression MAF", mafPressure, "kPa");
    break;
  case 1:
    if (BOOST_SCREEN == 0)
      draw_InfoText("Pression Turbo", turboPressureState, "Bar");
    else
      draw_AreaChartWithHistory(turboHistory, turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Pression Turbo", "Bar");
    break;
  case 2:
    if (IAT_SCREEN == 0)
      draw_InfoText("Temp admission", intakeTemp, "°C");
    else
      draw_AreaChartWithHistory(iatHistory, intakeTemp, -20.0, 60.0, "Temp admission", "°C");
    break;
  case 3:
    if (ENGLOAD_SCREEN == 0)
      draw_InfoText("Charge moteur", engineLoad, "%");
    else
      draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge moteur", "%");
    break;
  case 4:
    if (BATTERY_SCREEN == 0)
      draw_InfoText("Tension Bat", batteryVoltage, "V");
    else
      draw_AreaChartWithHistory(batteryHistory, batteryVoltage, 9.0, 15.0, "Tension Bat", "V");
    break;
  case 5:
    if (COOLANT_SCREEN == 0)
      draw_InfoText("Temp LdR", coolantTemp, "°C");
    else
      draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "°C");
    break;
  case 6:
  {
    u8g2.setFont(u8g2_font_helvR10_tr);
    drawStringCenter(12, "Defauts: " + String(dtcCountFound));
    if (dtcCountFound == 0)
    {
      u8g2.setFont(u8g2_font_helvR12_tr);
      drawStringCenter(36, "Aucun defaut");
    }
    else
    {
      u8g2.setFont(u8g2_font_helvR10_tr);
      drawStringCenter(36, "Lecture texte CAN");
      drawStringCenter(46, "Non dispo (ISO-TP)");
    }
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
  }
  break;
  case 7:
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 12, "BOOST:");
    drawStringLeft(0, 22, String(dashBoost, 2) + " bar");
    drawStringLeft(64, 12, "IAT:");
    drawStringLeft(64, 22, String(dashIAT, 1) + " C");
    drawStringLeft(0, 32, "COOL:");
    drawStringLeft(0, 42, String(dashCoolant, 1) + " C");
    drawStringLeft(64, 32, "LOAD:");
    drawStringLeft(64, 42, String(dashLoad, 1) + " %");
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
    break;
  case 8:
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
    u8g2.setFont(u8g2_font_helvR12_tr);
    drawStringCenter(14, "0 - " + String(TARGET_SPEED) + " km/h");
    u8g2.setFont(u8g2_font_helvR18_tr);
    if (timerReady && !timerRunning && (timerRunning ? ((millis() - speedTimerStart) / 1000.0) : lastTimerValue) == 0.0)
      drawStringCenter(36, "READY");
    else
      drawStringCenter(36, String(timerRunning ? ((millis() - speedTimerStart) / 1000.0) : lastTimerValue, 2) + " s");
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(46, "Speed: " + String((int)currentSpeed) + " km/h");
    break;
  case 9:
    draw_InfoText("Speed", currentSpeed, "km/h");
    break;
  case 10: // ÉCRAN DIAGNOSTIC : RAW DATA ET ERREURS
  {
    u8g2.setFont(u8g2_font_helvR10_tr);
    drawStringCenter(10, "RAW CAN DATA");

    twai_status_info_t status;
    twai_get_status_info(&status);

    if (status.state == TWAI_STATE_BUS_OFF)
    {
      u8g2.setFont(u8g2_font_helvR08_tr);
      drawStringCenter(26, "ETAT: BUS-OFF (Crash)");
      drawStringCenter(38, "Reconnexion...");
      drawStringCenter(48, "Inverser TX/RX !");
    }
    else if (status.state == TWAI_STATE_STOPPED)
    {
      u8g2.setFont(u8g2_font_helvR08_tr);
      drawStringCenter(36, "ETAT: ARRETE");
    }
    else
    {
      u8g2.setFont(u8g2_font_helvR08_tr);
      String errStr = "TX Err: " + String(status.tx_error_counter) + " | RX Err: " + String(status.rx_error_counter);
      drawStringCenter(18, errStr);

      if (lastRawId == 0)
      {
        drawStringCenter(36, "En attente de donnees...");
      }
      else
      {
        String idStr = "ID: 0x" + String(lastRawId, HEX);
        idStr.toUpperCase();
        drawStringLeft(0, 30, idStr + "   DLC: " + String(lastRawDlc));

        String d1 = "", d2 = "";
        for (int i = 0; i < 4; i++)
        {
          if (i < lastRawDlc)
          {
            if (lastRawData[i] < 0x10)
              d1 += "0";
            d1 += String(lastRawData[i], HEX) + " ";
          }
        }
        for (int i = 4; i < 8; i++)
        {
          if (i < lastRawDlc)
          {
            if (lastRawData[i] < 0x10)
              d2 += "0";
            d2 += String(lastRawData[i], HEX) + " ";
          }
        }
        d1.toUpperCase();
        d2.toUpperCase();
        drawStringCenter(40, d1);
        drawStringCenter(50, d2);
      }
    }
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
  }
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
      if (server.hasArg("voltage_gauge_type")) BATTERY_SCREEN = server.arg("voltage_gauge_type").toInt();
      if (server.hasArg("coolant_gauge_type")) COOLANT_SCREEN = server.arg("coolant_gauge_type").toInt();

      OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS, 0, 255);
      setOledBrightness(OLED_BRIGHTNESS);
      saveValues();
      server.sendHeader("Location", "/");
      server.send(303); });
  server.on("/api/state", HTTP_GET, []()
            {
        String json = "{";
        json += "\"screen\":" + String(screenIndex) + ",";
        json += "\"maf\":" + String(mafPressure) + ",";
        json += "\"boost\":" + String(turboPressureState) + ",";
        json += "\"iat\":" + String(intakeTemp) + ",";
        json += "\"load\":" + String(engineLoad) + ",";
        json += "\"battery\":" + String(batteryVoltage) + ",";
        json += "\"coolant\":" + String(coolantTemp) + ",";
        json += "\"speed\":" + String(currentSpeed) + ",";
        json += "\"boost_mode\":" + String(BOOST_SCREEN) + ",";
        json += "\"min_boost\":" + String(TURBO_MIN_BAR) + ",";
        json += "\"max_boost\":" + String(TURBO_MAX_BAR) + ",";
        json += "\"brightness\":" + String(OLED_BRIGHTNESS);
        json += "}";
        server.send(200, "application/json", json); });

  ElegantOTA.begin(&server);

  ElegantOTA.onStart([]()
                     {
    ota_updating = true; 
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvR12_tr);
    drawStringCenter(35, "OTA UPDATING");
    draw_BottomText("Ne pas debrancher");
    u8g2.sendBuffer(); });

  server.begin();
}

// ==== Setup ====
void setup()
{
  Serial.begin(115200);

  int usbTimeout = 40;
  while (!Serial && usbTimeout > 0)
  {
    delay(100);
    usbTimeout--;
  }

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);

  delay(500);

  if (digitalRead(BTN_OK) == LOW)
  {
    currentState = STATE_CONFIG;
  }

  u8g2.begin();
  u8g2.setBusClock(400000);

  EEPROM.begin(EEPROM_SIZE);
  loadValues();
  setOledBrightness(OLED_BRIGHTNESS);

  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 128, 64, epd_bitmap_logo_3008);
  draw_BottomText(version_string);
  u8g2.sendBuffer();

  delay(1500);

  if (!LittleFS.begin())
  {
    displayInfo("FS Error!");
    delay(2000);
    restart_ESP();
  }

  if (currentState != STATE_CONFIG)
  {
    displayInfo("CAN Init...");

    // NOUVEAU: On force le PULLUP interne sur la broche RX
    pinMode(CAN_RX_PIN, INPUT_PULLUP);

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); // Vitesse standard OBD PSA
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
      if (twai_start() != ESP_OK)
      {
        displayInfo("CAN Start Fail");
        delay(2000);
        restart_ESP();
      }
    }
    else
    {
      displayInfo("CAN Install Fail");
      delay(2000);
      restart_ESP();
    }
  }

  startServer();
}

// ==== Main Loop ====
void loop()
{
  server.handleClient();
  ElegantOTA.loop();
  dnsServer.processNextRequest();

  if (ota_updating)
  {
    yield();
    return;
  }

  if (currentState == STATE_GAUGES)
  {
    fetchOBDData();
  }

  // 2. LECTURE DES 4 BOUTONS
  bool upPressed = btnUp.pressed();
  bool downPressed = btnDown.pressed();
  bool okPressed = btnOk.pressed();
  bool menuPressed = btnMenu.pressed();

  if (menuPressed)
  {
    if (currentState == STATE_CONFIG)
      restart_ESP();
    else if (currentState == STATE_GAUGES)
    {
      buildMenu();
      currentState = STATE_MENU;
    }
    else if (currentState == STATE_MENU || currentState == STATE_EDIT_MIN ||
             currentState == STATE_EDIT_MAX || currentState == STATE_EDIT_SPEED ||
             currentState == STATE_EDIT_BRIGHTNESS)
    {
      currentState = STATE_GAUGES;
    }
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
    else if (currentState == STATE_MENU)
    {
      menuCursor += dir;
      if (menuCursor < 0)
        menuCursor = menuSize - 1;
      if (menuCursor >= menuSize)
        menuCursor = 0;
    }
    else if (currentState == STATE_EDIT_MIN)
    {
      TURBO_MIN_BAR += (dir * -0.1);
      if (TURBO_MIN_BAR > 0.5)
        TURBO_MIN_BAR = 0.5;
      if (TURBO_MIN_BAR < -1.0)
        TURBO_MIN_BAR = -1.0;
    }
    else if (currentState == STATE_EDIT_MAX)
    {
      TURBO_MAX_BAR += (dir * -0.1);
      if (TURBO_MAX_BAR > 3.0)
        TURBO_MAX_BAR = 3.0;
      if (TURBO_MAX_BAR < 0.5)
        TURBO_MAX_BAR = 0.5;
    }
    else if (currentState == STATE_EDIT_SPEED)
    {
      TARGET_SPEED += (dir * -10);
      if (TARGET_SPEED > 200)
        TARGET_SPEED = 200;
      if (TARGET_SPEED < 40)
        TARGET_SPEED = 40;
    }
    else if (currentState == STATE_EDIT_BRIGHTNESS)
    {
      int newVal = OLED_BRIGHTNESS + (dir * -25);
      OLED_BRIGHTNESS = constrain(newVal, 0, 255);
      setOledBrightness(OLED_BRIGHTNESS);
    }
  }

  if (okPressed)
  {
    if (currentState == STATE_MENU)
    {
      int action = menuAction[menuCursor];
      if (action == ACT_CLOSE)
        currentState = STATE_GAUGES;
      else if (action == ACT_ENTER_CONFIG)
        currentState = STATE_CONFIG;
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
        buildMenu();
      }
      else if (action == ACT_EDIT_MIN)
        currentState = STATE_EDIT_MIN;
      else if (action == ACT_EDIT_MAX)
        currentState = STATE_EDIT_MAX;
      else if (action == ACT_EDIT_SPEED)
        currentState = STATE_EDIT_SPEED;
      else if (action == ACT_EDIT_BRIGHTNESS)
        currentState = STATE_EDIT_BRIGHTNESS;
      else if (action == ACT_RESET_DTC)
      {
        displayInfo("Clearing DTC...");
        clearDTC();
        delay(1000);
        currentState = STATE_GAUGES;
      }
      else if (action >= ACT_GO_SCREEN_0)
      {
        screenIndex = action - ACT_GO_SCREEN_0;
        saveValues();
        currentState = STATE_GAUGES;
      }
    }
    else if (currentState == STATE_EDIT_MIN || currentState == STATE_EDIT_MAX ||
             currentState == STATE_EDIT_SPEED || currentState == STATE_EDIT_BRIGHTNESS)
    {
      saveValues();
      buildMenu();
      currentState = STATE_MENU;
    }
  }

  static unsigned long lastDrawTime = 0;
  if (millis() - lastDrawTime > 32)
  {
    lastDrawTime = millis();
    u8g2.clearBuffer();

    if (currentState == STATE_GAUGES)
      draw_GaugeScreen(screenIndex);
    else if (currentState == STATE_CONFIG)
      drawConfigScreen();
    else if (currentState == STATE_MENU)
      drawMenuScreen();
    else if (currentState == STATE_EDIT_MIN)
      drawEditScreen("Edit Turbo Min", String(TURBO_MIN_BAR, 1), "U/D: Edit | OK: Save");
    else if (currentState == STATE_EDIT_MAX)
      drawEditScreen("Edit Turbo Max", String(TURBO_MAX_BAR, 1), "U/D: Edit | OK: Save");
    else if (currentState == STATE_EDIT_SPEED)
      drawEditScreen("Edit Target Speed", String(TARGET_SPEED), "U/D: Edit | OK: Save");
    else if (currentState == STATE_EDIT_BRIGHTNESS)
    {
      int brightPct = map(OLED_BRIGHTNESS, 0, 255, 0, 100);
      drawEditScreen("Brightness", String(brightPct) + " %", "U/D: Edit | OK: Save");
    }

    u8g2.sendBuffer();
  }
  yield();
}