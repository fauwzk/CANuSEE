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
  STATE_EDIT_MIN,
  STATE_EDIT_MAX,
  STATE_EDIT_SPEED,
  STATE_EDIT_BRIGHTNESS,
  STATE_CONFIG
};
AppState currentState = STATE_CONNECTING; // Démarre sur l'écran de connexion
bool ota_updating = false;
String version_string = "CANuSEE " FW_VERSION;

// ==== OLED (U8g2) ====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);
const int centerX = 64;
const int screenNumbers = 9; // Écrans inutiles retirés
uint8_t screenIndex = 0;
float TURBO_MIN_BAR = -0.7, TURBO_MAX_BAR = 1.5;

float mafPressure = 0.0, intakeTemp = 0.0, engineLoad = 0.0, engineRPM = 0.0;
float coolantTemp = 0.0, turboPressureState = 0.0;
float dashBoost = 0, dashIAT = 0, dashCoolant = 0, dashRPM = 0, dashLoad = 0;

bool timerRunning = false, timerReady = false;
unsigned long speedTimerStart = 0;
float lastTimerValue = 0.0, currentSpeed = 0.0;

#define MAX_MENU_ITEMS 24
String menuText[MAX_MENU_ITEMS];
int menuAction[MAX_MENU_ITEMS];
int menuSize = 0, menuCursor = 0;

#define ACT_CLOSE 0
#define ACT_TOGGLE_STYLE 1
#define ACT_EDIT_MIN 2
#define ACT_EDIT_MAX 3
#define ACT_EDIT_SPEED 4
#define ACT_EDIT_BRIGHTNESS 5
#define ACT_ENTER_CONFIG 6
#define ACT_GO_SCREEN_0 10
const char *screenNames[] = {"MAP/Air", "Boost", "IAT", "Load", "Coolant", "Dash", "Timer", "Speed", "BLE"};

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

// Callback non-bloquant de fin de scan BLE
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
    case 0x0B: // Pression MAP (Utilisée comme MAF/Boost)
      mafPressure = A;
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
      currentSpeed = A;
      break;
    case 0x0F:
      intakeTemp = A - 40;
      dashIAT = intakeTemp;
      break;
    }
  }
}

// ==== SMART POLLING : Demande uniquement ce qui est affiché ====
uint8_t getNextSmartPID()
{
  static uint8_t dashStep = 0;
  switch (screenIndex)
  {
  case 0:
    return 0x0B; // MAP/Air
  case 1:
    return 0x0B; // Boost (Calculé à partir du MAP)
  case 2:
    return 0x0F; // IAT
  case 3:
    return 0x04; // Load
  case 4:
    return 0x05; // Coolant
  case 5:        // Dash (Boucle sur les 4 requis)
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
    return 0x0C; // Par défaut, RPM pour garder la connexion active
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
          currentState = STATE_GAUGES; // Démarrage terminé, on passe aux jauges !
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
      BLEDevice::getScan()->start(2, scanCompleteCB, false); // Scanner Asynchrone !
    }
  }
}
// ==========================================

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

// Nettoyage Web UI (Valeurs mortes remplacées par vide)
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
  html.replace("%SELECTED_COOLANT_TEXT%", (COOLANT_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_COOLANT_GAUGE%", (COOLANT_SCREEN == 1) ? "selected" : "");
  html.replace("%SELECTED_IAT_TEXT%", (IAT_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_IAT_GAUGE%", (IAT_SCREEN == 1) ? "selected" : "");

  html.replace("%SELECTED_VOLTAGE_TEXT%", "");
  html.replace("%SELECTED_VOLTAGE_GAUGE%", "");
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
  BOOST_SCREEN = (cfg.boost_screen_type >= 0 && cfg.boost_screen_type < 2) ? cfg.boost_screen_type : 0;
  TURBO_MIN_BAR = cfg.turbo_min;
  TURBO_MAX_BAR = cfg.turbo_max;
  ENGLOAD_SCREEN = (cfg.engload_screen_type >= 0 && cfg.engload_screen_type < 2) ? cfg.engload_screen_type : 0;
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
    drawStringCenter(60, text);
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

void drawConnectingScreen()
{
  u8g2.setFont(u8g2_font_helvR12_tr);
  drawStringCenter(16, "CONNECTING");
  u8g2.drawLine(0, 20, 128, 20);

  u8g2.setFont(u8g2_font_helvR08_tr);
  drawStringCenter(36, bleStatusStr);

  if (connected)
  {
    drawStringCenter(48, "Init Step: " + String(elmInitStep) + "/5");
  }

  draw_BottomText(version_string);
}

void buildMenu()
{
  menuSize = 0;
  menuText[menuSize] = "Exit Menu";
  menuAction[menuSize++] = ACT_CLOSE;
  menuText[menuSize] = "Mode Config";
  menuAction[menuSize++] = ACT_ENTER_CONFIG;
  menuText[menuSize] = "Brightness";
  menuAction[menuSize++] = ACT_EDIT_BRIGHTNESS;

  // Seules les 5 premières jauges ont un mode graphique
  if (screenIndex >= 1 && screenIndex <= 4)
  {
    String s = "Type: Text";
    if ((screenIndex == 1 && BOOST_SCREEN) || (screenIndex == 2 && IAT_SCREEN) ||
        (screenIndex == 3 && ENGLOAD_SCREEN) || (screenIndex == 4 && COOLANT_SCREEN))
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
  { // Index 6 is Timer now
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
  int startIdx = (menuCursor >= visibleItems) ? menuCursor - visibleItems + 1 : 0;

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

#define AREA_CHART_HISTORY 94
struct AreaChartData
{
  double values[AREA_CHART_HISTORY];
  uint8_t currentIndex;
  bool initialized;
};
AreaChartData turboHistory = {{0}, 0, false};
AreaChartData loadHistory = {{0}, 0, false};
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

  int chartX = 32, chartY = 12, chartWidth = AREA_CHART_HISTORY, chartHeight = 32, baseY = chartY + chartHeight;
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

  int maxLabelY = chartY + 8, minLabelY = baseY, valCenterY = chartY + (chartHeight / 2) + 4, alignBorderX = chartX - 2;
  drawStringRight(alignBorderX, maxLabelY, alignSign(formatDecimal(maxValue, 1)));
  drawStringRight(alignBorderX, minLabelY, alignSign(formatDecimal(minValue, 1)));
  drawStringRight(alignBorderX, valCenterY, alignSign(formatDecimal(newValue, 1)));

  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
}

void draw_GaugeScreen(uint8_t index)
{
  switch (index)
  {
  case 0:
    draw_InfoText("Pression MAP/Air", mafPressure, "kPa");
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
      draw_InfoText("Charge Moteur", engineLoad, "%");
    else
      draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge", "%");
    break;
  case 4:
    if (COOLANT_SCREEN == 0)
      draw_InfoText("Temp LdR", coolantTemp, "°C");
    else
      draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "°C");
    break;
  case 5:
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 12, "BOOST:");
    drawStringLeft(0, 22, String(dashBoost, 2) + " bar");
    drawStringLeft(64, 12, "IAT:");
    drawStringLeft(64, 22, String(dashIAT, 1) + " C");
    drawStringLeft(0, 32, "COOL:");
    drawStringLeft(0, 42, String(dashCoolant, 1) + " C");
    drawStringLeft(64, 32, "RPM:");
    drawStringLeft(64, 42, String((int)dashRPM) + " tr");
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);
    break;
  case 6:
    draw_BottomText(version_string);
    draw_ScreenNumber(screenIndex);

    // 1. En-tête
    u8g2.setFont(u8g2_font_helvR12_tr);
    drawStringCenter(14, "0 - " + String(TARGET_SPEED) + " km/h");

    // 2. Logique d'affichage du chrono
    u8g2.setFont(u8g2_font_helvR18_tr);

    if (timerRunning)
    {
      // On est en train de courir : affiche le temps réel
      float currentTime = (millis() - speedTimerStart) / 1000.0;
      drawStringCenter(36, String(currentTime, 2) + " s");
    }
    else if (lastTimerValue > 0)
    {
      // La course est finie : affiche le dernier score
      drawStringCenter(36, String(lastTimerValue, 2) + " s");
    }
    else
    {
      // État initial (READY)
      drawStringCenter(36, "READY");
    }

    // 3. Vitesse actuelle
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(46, "Speed: " + String((int)currentSpeed) + " km/h");
    break;
  case 7:
    draw_InfoText("Speed", currentSpeed, "km/h");
    break;
  case 8:
  {
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(8, "BLE OBDBLE STATUS");
    u8g2.drawLine(0, 12, 128, 12);

    u8g2.setCursor(0, 22);
    u8g2.print("Status: " + bleStatusStr);
    u8g2.setCursor(0, 32);
    u8g2.print("Packets RX: " + String(packetsReceived));
    u8g2.setCursor(0, 42);
    u8g2.print("Init Step: " + String(elmInitStep) + "/5");
    u8g2.setCursor(0, 52);
    u8g2.print("Buf: ");
    u8g2.print(elmBuffer);

    draw_ScreenNumber(screenIndex);
  }
  break;
  }
}

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
      if (server.hasArg("coolant_gauge_type")) COOLANT_SCREEN = server.arg("coolant_gauge_type").toInt();
      OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS, 0, 255); setOledBrightness(OLED_BRIGHTNESS);
      saveValues(); server.sendHeader("Location", "/"); server.send(303); });
  server.on("/api/state", HTTP_GET, []()
            {
      String json = "{\"screen\":" + String(screenIndex) + ",\"maf\":" + String(mafPressure) + ",\"boost\":" + String(turboPressureState) + 
                    ",\"iat\":" + String(intakeTemp) + ",\"load\":" + String(engineLoad) + 
                    ",\"coolant\":" + String(coolantTemp) + ",\"speed\":" + String(currentSpeed) + ",\"boost_mode\":" + String(BOOST_SCREEN) + 
                    ",\"min_boost\":" + String(TURBO_MIN_BAR) + ",\"max_boost\":" + String(TURBO_MAX_BAR) + ",\"brightness\":" + String(OLED_BRIGHTNESS) + "}";
      server.send(200, "application/json", json); });
  ElegantOTA.begin(&server);
  ElegantOTA.onStart([]()
                     { ota_updating = true; u8g2.clearBuffer(); u8g2.setFont(u8g2_font_helvR12_tr); drawStringCenter(35, "OTA UPDATING"); draw_BottomText("Ne pas debrancher"); u8g2.sendBuffer(); });
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);
  delay(500);

  if (digitalRead(BTN_MENU) == LOW)
    currentState = STATE_CONFIG;

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

  startServer();

  if (currentState != STATE_CONFIG)
  {
    BLEDevice::init("CANuSEE");
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    doScan = true;
  }
}

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

  // Le moteur BLE tourne toujours en arrière plan (Connecting OU Gauges)
  if (currentState == STATE_GAUGES || currentState == STATE_CONNECTING)
    processBLE();

  bool upPressed = btnUp.pressed();
  bool downPressed = btnDown.pressed();
  bool okPressed = btnOk.pressed();
  bool menuPressed = btnMenu.pressed();

  if (menuPressed)
  {
    if (currentState == STATE_CONFIG)
      restart_ESP();
    else if (currentState == STATE_CONNECTING)
      currentState = STATE_CONFIG;
    else if (currentState == STATE_GAUGES)
    {
      buildMenu();
      currentState = STATE_MENU;
    }
    else if (currentState == STATE_MENU || currentState == STATE_EDIT_MIN || currentState == STATE_EDIT_MAX || currentState == STATE_EDIT_SPEED || currentState == STATE_EDIT_BRIGHTNESS)
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
      else if (action >= ACT_GO_SCREEN_0)
      {
        screenIndex = action - ACT_GO_SCREEN_0;
        saveValues();
        currentState = STATE_GAUGES;
      }
    }
    else if (currentState == STATE_EDIT_MIN || currentState == STATE_EDIT_MAX || currentState == STATE_EDIT_SPEED || currentState == STATE_EDIT_BRIGHTNESS)
    {
      saveValues();
      buildMenu();
      currentState = STATE_MENU;
    }
  }

  // ==== RAFFRAICHISSEMENT D'ÉCRAN À 16 FPS (60ms) ====
  // Cela permet à l'ESP32 de traiter le BLE à 100% de ses capacités entre deux images
  static unsigned long lastDrawTime = 0;
  if (millis() - lastDrawTime > 60)
  {
    lastDrawTime = millis();
    u8g2.clearBuffer();

    if (currentState == STATE_CONNECTING)
      drawConnectingScreen();
    else if (currentState == STATE_GAUGES)
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
      drawEditScreen("Brightness", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)) + " %", "U/D: Edit | OK: Save");

    u8g2.sendBuffer();
  }
  yield();
}