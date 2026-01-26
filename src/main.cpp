#include <Wire.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <DNSServer.h>

// 'logo 3008', 128x64px
const unsigned char epd_bitmap_logo_3008[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x0f, 0x7e, 0xf0, 0x0f, 0xfc, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x8f, 0xff, 0xf9, 0x1f, 0xfe, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xcf, 0xff, 0xff, 0x3f, 0x0f, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xe3, 0xc3, 0x1f, 0x7c, 0x07, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xf3, 0xc1, 0x0f, 0x7c, 0xdf, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xf7, 0x80, 0x0f, 0x78, 0xfe, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xf7, 0xc0, 0x07, 0x3c, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xf7, 0xc0, 0x0f, 0xbc, 0x8f, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xf7, 0xe0, 0x0f, 0x9f, 0x07, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xf3, 0xff, 0xff, 0x8f, 0x87, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xe0, 0xff, 0xfe, 0x87, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xe0, 0x3f, 0xc0, 0x3f, 0xfc, 0x01, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Array of all bitmaps for convenience. (Total bytes used to store images in PROGMEM = 1040)
const int epd_bitmap_allArray_LEN = 1;
const unsigned char *epd_bitmap_allArray[1] = {
    epd_bitmap_logo_3008};

// ==== WiFi Config ====
const char *ssid = "CANuSEE_Config";

// ==== Web Server ====
WebServer server(80);

// ==== DNS Server ====
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
  int tick_line_gauge;
};

// ==== EEPROM Setup ====
#define EEPROM_SIZE sizeof(Settings)

Settings cfg;

int BOOST_SCREEN = 0;   // 0 = text, 1 = gauge
int ENGLOAD_SCREEN = 0; // 0 = text, 1 = gauge
int BATTERY_SCREEN = 0; // 0 = text, 1 = gauge
int COOLANT_SCREEN = 0; // 0 = text, 1 = gauge
int TICK_LINE_GAUGE = 2;

// ==== Dashboard refresh state ====
uint8_t dashStep = 0;
unsigned long dashLastUpdate = 0;
const unsigned long dashDelay = 10; // small delay between requests

// Stored dashboard values
float dashBoost = 0;
float dashIAT = 0;
float dashCoolant = 0;
float dashLoad = 0;

// ==== Version ====
String version = "v0.9";
String version_string = "CANuSEE " + version;

// ==== OLED ====
SSD1306Wire display(0x3C, 21, 22); // SDA=21, SCL=22

// ==== ELM327 Classic Bluetooth ====
BluetoothSerial SerialBT;
#define ELM_PORT SerialBT
#define DEBUG_PORT Serial
#define ELM327_BT_PIN "1234"
ELM327 myELM327;
uint8_t elm_address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// ==== Display center coordinates ====
const int centerX = 64;
const int centerY = 55;

// ==== Screen control ====
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
const unsigned long longPressDuration = 1000; // 2 seconds for long press

// ==== Bluetooth setup ====
#define BT_DISCOVER_TIME 500000
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE; // or ESP_SPP_SEC_ENCRYPT|ESP_SPP_SEC_AUTHENTICATE to request pincode confirmation
esp_spp_role_t role = ESP_SPP_ROLE_MASTER; // or ESP_SPP_ROLE_MASTER

// ==== Variables for OBD-II data ====
float atmo_kpa;
float maf_kpa;
float coolant_temp;
float intake_temp;
float engine_load;
float battery_voltage;
float oil_temp;
float turbo_pressure;

// ==== Variables to hold last valid readings ====
float atmoPressure = 0.0;
float mafPressure = 0.0;
float intakeTemp = 0.0;
float engineLoad = 0.0;
float coolantTemp = 0.0;
float batteryVoltage = 0.0;
float fuelLevel = 0.0;
float oilTemp = 0.0;
float turboPressure = 0.0;

uint8_t data[8];

void sendCAN_ELM(uint16_t id, uint8_t len, uint8_t *data)
{
  char cmd[64];
  char *ptr = cmd;

  // ID sur 3 octets hex
  sprintf(ptr, "%03X %02X", id, len);
  ptr += strlen(ptr);

  // Ajout des données
  for (uint8_t i = 0; i < len; i++)
  {
    sprintf(ptr, " %02X", data[i]);
    ptr += strlen(ptr);
  }

  // Envoi vers ELM327
  myELM327.sendCommand(cmd);
}

void BTN_MENU(void)
{
  data[0] = 0x40;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;
  sendCAN_ELM(0x3E5, 6, data);
  delay(1);
}

void BTN_UP(void)
{
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x40;
  sendCAN_ELM(0x3E5, 6, data);
  delay(1);
}

String generateWebPage()
{
  File file = LittleFS.open("/index.html", "r");
  if (!file)
  {
    return "<html><body><h3>File not found</h3></body></html>";
  }

  String html = file.readString();
  file.close();

  // Replace placeholders
  html.replace("%MIN%", String(TURBO_MIN_BAR));
  html.replace("%MAX%", String(TURBO_MAX_BAR));
  html.replace("%VERSION%", version);
  html.replace("%SELECTED_BOOST_TEXT%", (BOOST_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_BOOST_GAUGE%", (BOOST_SCREEN == 1) ? "selected" : "");
  html.replace("%SELECTED_LOAD_TEXT%", (ENGLOAD_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_LOAD_GAUGE%", (ENGLOAD_SCREEN == 1) ? "selected" : "");
  html.replace("%SELECTED_VOLTAGE_TEXT%", (BATTERY_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_VOLTAGE_GAUGE%", (BATTERY_SCREEN == 1) ? "selected" : "");
  html.replace("%SELECTED_COOLANT_TEXT%", (COOLANT_SCREEN == 0) ? "selected" : "");
  html.replace("%SELECTED_COOLANT_GAUGE%", (COOLANT_SCREEN == 1) ? "selected" : "");
  html.replace("%TICKS%", String(TICK_LINE_GAUGE));
  return html;
}

void saveValues()
{
  cfg.last_screen = screenIndex;
  cfg.boost_screen_type = BOOST_SCREEN;
  cfg.turbo_min = TURBO_MIN_BAR;
  cfg.turbo_max = TURBO_MAX_BAR;
  cfg.engload_screen_type = ENGLOAD_SCREEN;
  cfg.battery_screen_type = BATTERY_SCREEN;
  cfg.coolant_screen_type = COOLANT_SCREEN;
  cfg.tick_line_gauge = TICK_LINE_GAUGE;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadValues()
{
  EEPROM.get(0, cfg);
  screenIndex = cfg.last_screen;
  if (screenIndex >= screenNumbers)
  {
    screenIndex = 0; // Reset to 0 if out of bounds
  }
  BOOST_SCREEN = cfg.boost_screen_type;
  if (BOOST_SCREEN < 0 || BOOST_SCREEN >= ScreenTypes)
  {
    BOOST_SCREEN = 0; // Reset to 0 if out of bounds
  }
  TURBO_MIN_BAR = cfg.turbo_min;
  TURBO_MAX_BAR = cfg.turbo_max;
  ENGLOAD_SCREEN = cfg.engload_screen_type;
  if (ENGLOAD_SCREEN < 0 || ENGLOAD_SCREEN >= ScreenTypes)
  {
    ENGLOAD_SCREEN = 0; // Reset to 0 if out of bounds
  }
  BATTERY_SCREEN = cfg.battery_screen_type;
  if (BATTERY_SCREEN < 0 || BATTERY_SCREEN >= ScreenTypes)
  {
    BATTERY_SCREEN = 0; // Reset to 0 if out of bounds
  }
  COOLANT_SCREEN = cfg.coolant_screen_type;
  if (COOLANT_SCREEN < 0 || COOLANT_SCREEN >= ScreenTypes)
  {
    COOLANT_SCREEN = 0; // Reset to 0 if out of bounds
  }
  TICK_LINE_GAUGE = cfg.tick_line_gauge;
  Serial.println("Settings loaded from EEPROM:");
  Serial.printf("Last Screen: %d\n", screenIndex);
  Serial.printf("Boost Screen Type: %d\n", BOOST_SCREEN);
  Serial.printf("Turbo Min: %.2f\n", TURBO_MIN_BAR);
  Serial.printf("Turbo Max: %.2f\n", TURBO_MAX_BAR);
  Serial.printf("EngLoad Screen Type: %d\n", ENGLOAD_SCREEN);
  Serial.printf("Battery Screen Type: %d\n", BATTERY_SCREEN);
  Serial.printf("Coolant Screen Type: %d\n", COOLANT_SCREEN);
  Serial.printf("Tick Line Gauge: %d\n", TICK_LINE_GAUGE);
}

// ==== Restart ESP ====
void restart_ESP()
{
  display.normalDisplay();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "REBOOTING!");
  display.display();
  delay(1000);
  ESP.restart();
}

// ==== Draw bottom text ====
void draw_BottomText(String text)
{
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawRect(0, 54, 128, 10);
  display.setColor(BLACK);
  display.fillRect(0, 54, 128, 10);
  display.setColor(WHITE);
  display.drawString(centerX, 52, text);
}

// ==== Display info ====
void displayInfo(String msg)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 25, "Info:");
  draw_BottomText(msg);
  display.display();
}

// ==== Draw screen number ====
void draw_ScreenNumber(uint8_t index)
{
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 52, String(index + 1) + "/" + String(screenNumbers));
}

// ==== Draw info text screen ====
void draw_InfoText(String title, double value, String unit)
{
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(centerX, 0, title);
  display.setFont(ArialMT_Plain_24);
  if (value == (int)value)
  {
    display.drawString(centerX, 20, String((int)value) + " " + unit);
  }
  else
  {
    display.drawString(centerX, 20, String(value) + " " + unit);
  }
  display.display();
}

// ==== Tight & Clear Speedometer Gauge with Values ====
void draw_SpeedoGauge(double value, double minValue, double maxValue, String label, String unit)
{
  const int cx = 64;                       // Center X
  const int cy = 48;                       // Center Y (fitting screen)
  const int outerRadius = 38;              // Smaller radius for tighter gauge
  const int innerRadius = outerRadius - 8; // Thickness
  const int startAngle = 135;              // Left arc end
  const int endAngle = 45;                 // Right arc end

  // Clamp value
  if (value < minValue)
    value = minValue;
  if (value > maxValue)
    value = maxValue;

  // ==== Draw arc outline ====
  for (int a = startAngle; a >= endAngle; a -= 2) // finer outline
  {
    int xo = cx + outerRadius * cos(a * DEG_TO_RAD);
    int yo = cy - outerRadius * sin(a * DEG_TO_RAD);
    int xi = cx + innerRadius * cos(a * DEG_TO_RAD);
    int yi = cy - innerRadius * sin(a * DEG_TO_RAD);
    display.drawLine(xi, yi, xo, yo);
  }

  // ==== Tick marks with values ====
  int tickCount = TICK_LINE_GAUGE;
  int tickLength = 3;
  for (int i = 0; i <= tickCount; i++)
  {
    double tAngle = startAngle - i * (startAngle - endAngle) / tickCount;
    int x1 = cx + (outerRadius - 1) * cos(tAngle * DEG_TO_RAD);
    int y1 = cy - (outerRadius - 1) * sin(tAngle * DEG_TO_RAD);
    int x2 = cx + (outerRadius - 1 - tickLength) * cos(tAngle * DEG_TO_RAD);
    int y2 = cy - (outerRadius - 1 - tickLength) * sin(tAngle * DEG_TO_RAD);
    display.drawLine(x1, y1, x2, y2);

    // Draw numeric labels every second tick (or first/last)
    if (i == 0 || i == tickCount || i % 2 == 0)
    {
      double tickValue = minValue + (maxValue - minValue) * i / tickCount;
      int labelRadius = outerRadius + 8; // slightly outside the arc
      int lx = cx + labelRadius * cos(tAngle * DEG_TO_RAD);
      int ly = cy - labelRadius * sin(tAngle * DEG_TO_RAD);
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(lx, ly, String(tickValue, 0));
    }
  }

  // ==== Draw needle ====
  double range = maxValue - minValue;
  double needleAngle = startAngle - (value - minValue) / range * (startAngle - endAngle);
  int needleLength = outerRadius - 6;
  int nx = cx + needleLength * cos(needleAngle * DEG_TO_RAD);
  int ny = cy - needleLength * sin(needleAngle * DEG_TO_RAD);
  display.drawLine(cx, cy, nx, ny);

  // ==== Center hub ====
  for (int r = 0; r < 2; r++)
  {
    for (int a = 0; a < 360; a += 15)
    {
      int px = cx + r * cos(a * DEG_TO_RAD);
      int py = cy - r * sin(a * DEG_TO_RAD);
      display.setPixel(px, py);
    }
  }

  // ==== Label & current value ====
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(cx, 0, label); // top label
  display.setFont(ArialMT_Plain_16);
  display.drawString(cx, cy + outerRadius + 8, String(value) + " " + unit); // numeric

  // ==== Optional bottom info ====
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
}

// ==== Draw line gauge with graduations ====
// Draws a horizontal line gauge that fills up as the value increases
void draw_LineGauge(double value, double minValue, double maxValue, String label, String unit)
{
  double barValue = value;
  // Clamp value
  if (barValue < minValue)
    barValue = minValue;
  if (barValue > maxValue)
    barValue = maxValue;

  // Layout parameters
  int barX = 14;      // left position
  int barY = 14;      // top position
  int barWidth = 100; // total width of the bar
  int barHeight = 10; // height of the bar

  // Calculate fill width
  double range = maxValue - minValue;
  double fillPercent = (barValue - minValue) / range;
  int fillWidth = (int)(barWidth * fillPercent);

  // ==== Draw bar outline ====
  display.drawRect(barX, barY, barWidth, barHeight);

  // ==== Draw fill ====
  display.fillRect(barX, barY, fillWidth, barHeight);

  // ==== Draw graduations ====
  int tickCount = TICK_LINE_GAUGE; // number of intermediate marks (between min and max)
  int tickHeight = 2;
  int labelOffsetY = barY + barHeight + 2;

  for (int i = 0; i <= tickCount; i++)
  {
    int tickX;
    if (i == 0)
    {
      tickX = (barX + (barWidth * i) / tickCount);
    }
    else
    {
      tickX = (barX + (barWidth * i) / tickCount) - 1; // -1 to center the tick
    }
    display.drawLine(tickX, barY + barHeight, tickX, barY + barHeight + tickHeight);

    // Optional: add numeric label every second tick
    if (i == 0 || i == tickCount || i % 2 == 0)
    {
      double tickValue = minValue + (range * i / tickCount);
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(tickX, labelOffsetY, String(tickValue, 1));
    }
  }

  // ==== Draw label and current value ====
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(centerX, 0, label);
  display.drawString(centerX, labelOffsetY + 10, String(value) + " " + unit);

  // ==== Optional bottom info ====
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
}

void resetDTCs()
{
  displayInfo("Resetting DTCs...");
  myELM327.resetDTC();
  while (myELM327.nb_rx_state != ELM_SUCCESS)
  {
    displayInfo("Resetting DTCs...");
    display.display();
  }
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    displayInfo("DTCs Resetted!");
    delay(1000);
  }
  else
  {
    displayInfo("DTC Reset Failed!");
    delay(1000);
  }
}

// ==== Get Atmospheric Pressure ====
void get_AtmosphericPressure()
{
  if (0 == 1)
  {
    atmo_kpa = myELM327.absBaroPressure();
    while (myELM327.nb_rx_state != ELM_SUCCESS)
    {
      displayInfo("Getting\nPressure...");
      display.display();
    }
    if (myELM327.nb_rx_state == ELM_SUCCESS)
    {
      atmoPressure = atmo_kpa;
    }
    else
    {
      atmoPressure = 0.0;
    }
  }
  else
  {
    atmo_kpa = 100.0;
    atmoPressure = atmo_kpa;
  }
}

void updateDashboardSequential()
{
  if (millis() - dashLastUpdate < dashDelay)
    return;
  dashLastUpdate = millis();

  switch (dashStep)
  {
  // ---- STEP 0 : BOOST ----
  case 0:
    turbo_pressure = myELM327.manifoldPressure();
    if (myELM327.nb_rx_state == ELM_SUCCESS)
    {
      turbo_pressure = (turbo_pressure - 100) * 0.01;
      dashBoost = turbo_pressure;
      dashStep++;
    }
    break;

  // ---- STEP 1 : Intake temp ----
  case 1:
    intake_temp = myELM327.intakeAirTemp();
    if (myELM327.nb_rx_state == ELM_SUCCESS)
    {
      dashIAT = intake_temp;
      dashStep++;
    }
    break;

  // ---- STEP 2 : Coolant ----
  case 2:
    coolant_temp = myELM327.engineCoolantTemp();
    if (myELM327.nb_rx_state == ELM_SUCCESS)
    {
      dashCoolant = coolant_temp;
      dashStep++;
    }
    break;

  // ---- STEP 3 : Engine load ----
  case 3:
    engine_load = myELM327.engineLoad();
    if (myELM327.nb_rx_state == ELM_SUCCESS)
    {
      dashLoad = engine_load;
      dashStep = 0; // restart cycle
    }
    break;
  }
}

void drawDashboardScreen()
{
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  // Column X positions
  int col1 = 0;
  int col2 = 64; // adjust if needed for your screen width

  // Row Y positions
  int row1 = 0;
  int row2 = 20;

  // ---- Top left ----
  display.drawString(col1, row1, "BOOST:");
  display.drawString(col1, row1 + 10, String(dashBoost, 2) + " bar");

  // ---- Top right ----
  display.drawString(col2, row1, "IAT:");
  display.drawString(col2, row1 + 10, String(dashIAT, 1) + " C");

  // ---- Bottom left ----
  display.drawString(col1, row2, "COOLANT:");
  display.drawString(col1, row2 + 10, String(dashCoolant, 1) + " C");

  // ---- Bottom right ----
  display.drawString(col2, row2, "ENG LOAD:");
  display.drawString(col2, row2 + 10, String(dashLoad, 1) + " %");

  // ---- Keep bottom bar exactly the same ----
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);

  display.display();
}

void draw_MAFScreen()
{
  maf_kpa = myELM327.manifoldPressure();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    mafPressure = maf_kpa;
  }
  draw_InfoText("Pression MAF", mafPressure, "kPa");
}

void draw_IntakeTempScreen()
{
  intake_temp = myELM327.intakeAirTemp();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    intakeTemp = intake_temp;
  }
  draw_InfoText("Temp admission", intakeTemp, "°C");
}

void draw_EngineLoadTextScreen()
{
  engine_load = myELM327.engineLoad();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    engineLoad = engine_load;
  }
  draw_InfoText("Charge moteur", engineLoad, "%");
}

void draw_EngineLoadGaugeScreen()
{
  engine_load = myELM327.engineLoad();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    engineLoad = engine_load;
  }
  draw_LineGauge(engineLoad, 0, 100, "Charge moteur", "%");
}

void draw_EngLoadScreens()
{
  if (ENGLOAD_SCREEN == 0)
  {
    draw_EngineLoadTextScreen();
  }
  else
  {
    draw_EngineLoadGaugeScreen();
  }
}

void draw_BatteryVoltageTextScreen()
{
  battery_voltage = myELM327.batteryVoltage();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    batteryVoltage = battery_voltage - 2.0; // Adjust for alternator voltage drop
  }
  draw_InfoText("Tension Bat", batteryVoltage, "V");
}

void draw_BatteryVoltageGaugeScreen()
{
  battery_voltage = myELM327.batteryVoltage();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    batteryVoltage = battery_voltage - 2.0; // Adjust for alternator voltage drop
  }
  draw_LineGauge(batteryVoltage, 9.0, 15.0, "Tension Bat", "V");
}

void draw_BatteryVoltageScreens()
{
  if (BATTERY_SCREEN == 0)
  {
    draw_BatteryVoltageTextScreen();
  }
  else
  {
    draw_BatteryVoltageGaugeScreen();
  }
}

void draw_CoolantTempTextScreen()
{
  coolant_temp = myELM327.engineCoolantTemp();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    coolantTemp = coolant_temp;
  }
  draw_InfoText("Temp LdR", coolantTemp, "°C");
}

void draw_CoolantTempGaugeScreen()
{
  coolant_temp = myELM327.engineCoolantTemp();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    coolantTemp = coolant_temp;
  }
  draw_LineGauge(coolantTemp, 40.0, 120.0, "Temp LdR", "°C");
}

void draw_CoolantTempScreens()
{
  if (COOLANT_SCREEN == 0)
  {
    draw_CoolantTempTextScreen();
  }
  else
  {
    draw_CoolantTempGaugeScreen();
  }
}

void draw_TurboPressureLineScreen()
{
  turbo_pressure = myELM327.manifoldPressure();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    turbo_pressure = turbo_pressure - 100;  // Gauge pressure = absolute - atmospheric
    turbo_pressure = turbo_pressure * 0.01; // Convert kPa to bar
    turboPressure = turbo_pressure;
  }
  draw_LineGauge(turboPressure, TURBO_MIN_BAR, TURBO_MAX_BAR, "Pression Turbo", "Bar");
}

void draw_TurboPressureTextScreen()
{ // Turbo pressure calculation:
  turbo_pressure = myELM327.manifoldPressure();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    turbo_pressure = turbo_pressure - 100;  // Gauge pressure = absolute - atmospheric
    turbo_pressure = turbo_pressure * 0.01; // Convert kPa to bar
    turboPressure = turbo_pressure;
  }
  draw_InfoText("Pression Turbo", turboPressure, "Bar");
}

void draw_BoostScreens()
{
  if (BOOST_SCREEN == 0)
  {
    draw_TurboPressureTextScreen();
  }
  else
  {
    draw_TurboPressureLineScreen();
  }
}

void draw_dtcCodes_nonBlocking()
{

  myELM327.currentDTCCodes(false); // false = non bloquant si supporté
  myELM327.monitorStatus();
  uint8_t codesFound = myELM327.DTC_Response.codesFound;
  // --- Affichage ---
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(centerX, 0, "DTC Codes:");

  display.setFont(ArialMT_Plain_10);
  if (codesFound == 0)
  {
    display.drawString(centerX, 20, "No DTC Codes");
  }
  else
  {
    display.setFont(ArialMT_Plain_24);
    display.drawString(centerX, 20, String(codesFound));
  }

  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
  display.display();
}

void draw_dtcCodes()
{
  myELM327.currentDTCCodes(true);
  // --- Configuration de base de l'affichage ---
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(centerX, 0, "DTC Codes:");

  // --- Police plus petite pour la liste ---
  display.setFont(ArialMT_Plain_10);

  uint8_t codesFound = myELM327.DTC_Response.codesFound;

  if (codesFound == 0)
  {
    display.drawString(centerX, 20, "No DTC Codes");
  }
  else
  {
    display.setFont(ArialMT_Plain_24);
    display.drawString(centerX, 20, String(codesFound));
  }

  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
  display.display();
}

// ==== Display error ====
void displayError(String msg)
{
  display.clear();
  display.invertDisplay();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 25, "ERROR!");
  draw_BottomText(msg);
  display.display();
}

void draw_NoDataScreen()
{
  displayError("Screen Error");
  delay(1000);
  restart_ESP();
}

// ==== Draw gauge screen based on index ====
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
    draw_NoDataScreen();
    break;
  }
}

// ==== Fade transition effect ====
void fadeTransition(uint8_t nextScreen)
{
  const int steps = 10;
  for (int i = 0; i < steps; i++)
  {
    display.setBrightness(255 - (i * 25));
    delay(20);
  }
  display.displayOff();
  display.clear();
  draw_GaugeScreen(nextScreen);
  display.clear();
  display.displayOn();
  for (int i = 0; i < steps; i++)
  {
    display.setBrightness(i * 25);
    delay(20);
  }
}

void drawHeartbeatSpinner()
{
  static uint8_t spinnerIndex = 0;
  static unsigned long lastUpdate = 0;

  const unsigned long spinnerInterval = 200; // vitesse du spinner (ms)
  const char spinnerChars[] = {'|', '/', '-', '\\'};

  if (millis() - lastUpdate >= spinnerInterval)
  {
    lastUpdate = millis();

    // Calculer position bas à droite
    int x = 122; // colonne proche du bord droit (OLED 128px)
    int y = 54;  // ligne du bas (OLED 64px, bas rectangle à 54)

    // Effacer le caractère précédent
    display.setColor(BLACK);
    display.fillRect(x, y, 6, 10); // petite zone du spinner
    display.setColor(WHITE);

    // Dessiner le spinner
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(x, y, String(spinnerChars[spinnerIndex]));

    // Passer au caractère suivant
    spinnerIndex = (spinnerIndex + 1) % 4;
  }
}

// ==== Start Captive Portal ====
void startCaptivePortal()
{
  WiFi.softAP(ssid, NULL);
  IPAddress myIP = WiFi.softAPIP();
  Serial.println("WiFi AP started: " + myIP.toString());
  // Serveur DNS pour captive portal
  dnsServer.start(DNS_PORT, "*", myIP); // redirige tout vers l'ESP

  server.onNotFound([]()
                    {
server.sendHeader("Location", "/");
server.send(302, "text/plain", ""); });
  server.begin();
}

// ==== Setup function ====
void setup()
{
  // ==== Basic setup ====
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // ==== OLED init ====
  display.init();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(4, 8, "Fauwzk"); // Top left
  display.drawString(32, 24, "Engineering");
  delay(1000);
  display.clear();
  display.drawXbm(0, 0, 128, 64, epd_bitmap_logo_3008);
  draw_BottomText(version_string);
  display.display();
  delay(1000);
  draw_BottomText("Starting...");
  display.display();
  delay(1000);

  // ==== EEPROM init ====
  draw_BottomText("EEPROM Init");
  display.display();
  EEPROM.begin(EEPROM_SIZE);
  draw_BottomText("EEPROM Init done");
  display.display();

  // ==== Load settings from EEPROM ====
  draw_BottomText("Loading Settings...");
  display.display();
  loadValues();

  draw_BottomText("Mounting FS...");
  display.display();
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed!");
    displayInfo("FS Error!");
    restart_ESP();
  }
  draw_BottomText("FS mounted");
  display.display();
  Serial.println("LittleFS mounted successfully");

  draw_BottomText("Last screen: " + String(screenIndex + 1) + "/" + String(screenNumbers));
  display.display();

  // ==== Connect to Classic Bluetooth ELM327 ====
  if (!SerialBT.begin("CANuSEE", true))
  { // true = master mode
    draw_BottomText("BT INIT FAIL");
    display.invertDisplay();
    display.display();
    delay(10000);
    restart_ESP();
  }
  draw_BottomText("BT Init done");
  display.display();
  SerialBT.setPin(ELM327_BT_PIN);

  // Connect to the paired device by MAC address
  draw_BottomText("BT Connecting...");
  display.display();
  if (!SerialBT.connect(elm_address, sec_mask, role))
  {
    displayError("BT Conn FAIL");
    delay(10000);
    restart_ESP();
  }
  draw_BottomText("BT Connected");
  display.display();

  // ==== ELM327 init ====
  draw_BottomText("ELM327 Init...");
  display.display();
  if (!myELM327.begin(SerialBT, false, 2000))
  {
    displayError("ELM327 INIT FAIL");
    delay(10000);
    restart_ESP();
  }
  draw_BottomText("ELM327 Connected");
  display.display();

  draw_BottomText("ELM327 Config...");
  display.display();
  myELM327.sendCommand(SET_ISO_BAUD_10400);
  myELM327.sendCommand(ALLOW_LONG_MESSAGES);
  draw_BottomText("ELM327 Config done");
  display.display();

  startCaptivePortal();
  // ==== Web Server Routes ====
  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html", generateWebPage()); });

  server.on("/btn_up", HTTP_GET, []()
            { BTN_UP();
              server.send(200, "text/html",
                          "<html><body><h3>Button Up Pressed!</h3><a href='/'>Back</a></body></html>"); });

  server.on("/btn_menu", HTTP_GET, []()
            { BTN_MENU();
              server.send(200, "text/html",
                          "<html><body><h3>Button Menu Pressed!</h3><a href='/'>Back</a></body></html>"); });

  server.on("/save", HTTP_POST, []()
            {
    if (server.hasArg("boost_min") && server.hasArg("boost_max") && server.hasArg("boost_gauge_type") &&
        server.hasArg("engload_gauge_type") && server.hasArg("voltage_gauge_type") &&
        server.hasArg("coolant_gauge_type") && server.hasArg("ticks"))
    {
      TURBO_MIN_BAR = server.arg("boost_min").toFloat();
      TURBO_MAX_BAR = server.arg("boost_max").toFloat();
      BOOST_SCREEN = server.arg("boost_gauge_type").toInt();
      ENGLOAD_SCREEN = server.arg("engload_gauge_type").toInt();
      BATTERY_SCREEN = server.arg("voltage_gauge_type").toInt();
      COOLANT_SCREEN = server.arg("coolant_gauge_type").toInt();
      TICK_LINE_GAUGE = server.arg("ticks").toInt();
      saveValues();
      server.send(200, "text/html",
                  "<html><body><h3>Saved!</h3><a href='/'>Back</a></body></html>");
    }
    else
    {
      server.send(400, "text/plain", "Missing parameters");
    } });

  server.on("/nextpage", HTTP_GET, []()
            {
    screenIndex = (screenIndex + 1) % screenNumbers;
    fadeTransition(screenIndex);
    cfg.last_screen = screenIndex;
    EEPROM.put(0, cfg);
    EEPROM.commit();
    server.send(200, "text/html",
                "<html><body><h3>Page Changed!</h3><a href='/'>Back</a></body></html>"); });

  server.on("/reset", HTTP_GET, []()
            {
    server.send(200, "text/html",
                "<html><body><h3>Device Resetting...</h3></body></html>");
    delay(1000);
    restart_ESP(); });

  server.begin();
  fadeTransition(screenIndex);
}

// ==== Main loop ====
void loop()
{
  server.handleClient();
  dnsServer.processNextRequest();

  static bool buttonPressed = false;
  static unsigned long buttonPressTime = 0;
  static bool longPressHandled = false;

  // ==== Check button state ====
  bool buttonState = (digitalRead(BUTTON_PIN) == LOW); // active LOW

  if (buttonState && !buttonPressed)
  {
    // Button just pressed
    buttonPressed = true;
    buttonPressTime = millis();
    longPressHandled = false;
  }

  if (!buttonState && buttonPressed)
  {
    // Button just released
    unsigned long pressDuration = millis() - buttonPressTime;
    buttonPressed = false;

    if (pressDuration < longPressDuration && (millis() - lastButtonPress) > debounceDelay)
    {
      // ==== SHORT PRESS ====
      lastButtonPress = millis();
      myELM327.response = 0;
      delay(100);
      // Transition vers l’écran suivant une fois les données prêtes
      fadeTransition((screenIndex + 1) % screenNumbers);
      screenIndex = (screenIndex + 1) % screenNumbers;
      cfg.last_screen = screenIndex;
      EEPROM.put(0, cfg);
      EEPROM.commit();
      lastSwitch = millis();
    }
  }

  // ==== Handle long press ====
  if (buttonPressed && !longPressHandled)
  {
    if (millis() - buttonPressTime > longPressDuration)
    {
      if (screenIndex == 1)
      {
        // ==== LONG PRESS ACTION ====
        BOOST_SCREEN = (BOOST_SCREEN + 1) % ScreenTypes;
        cfg.boost_screen_type = BOOST_SCREEN;
        EEPROM.put(0, cfg);
        EEPROM.commit();
        display.display();
        delay(1000);
        longPressHandled = true;
      }
      else if (screenIndex == 3)
      {
        ENGLOAD_SCREEN = (ENGLOAD_SCREEN + 1) % ScreenTypes;
        cfg.engload_screen_type = ENGLOAD_SCREEN;
        EEPROM.put(0, cfg);
        EEPROM.commit();
        display.display();
        delay(1000);
        longPressHandled = true;
      }
      else if (screenIndex == 4)
      {
        BATTERY_SCREEN = (BATTERY_SCREEN + 1) % ScreenTypes;
        cfg.battery_screen_type = BATTERY_SCREEN;
        EEPROM.put(0, cfg);
        EEPROM.commit();
        display.display();
        delay(1000);
        longPressHandled = true;
      }
      else if (screenIndex == 5)
      {
        COOLANT_SCREEN = (COOLANT_SCREEN + 1) % ScreenTypes;
        cfg.coolant_screen_type = COOLANT_SCREEN;
        EEPROM.put(0, cfg);
        EEPROM.commit();
        display.display();
        delay(1000);
        longPressHandled = true;
      }

      else if (screenIndex == 6)
      {
        resetDTCs();
      }
      else
      {
        // ==== LONG PRESS ACTION ====
        displayInfo("Rebooting...");
        display.display();
        delay(1000);
        restart_ESP();
      }
    }
  }
  display.clear();
  draw_GaugeScreen(screenIndex);
  display.display();

  delay(10);
}

// J'ai fini mon code.