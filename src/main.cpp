#include <Wire.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <SSD1306Wire.h>

#define EEPROM_SIZE 1

bool debug = false;
String version = "v0.5";
String version_string = "CANuSEE " + version;

// ==== OLED ====
SSD1306Wire display(0x3C, 21, 22); // SDA=21, SCL=22

// ==== ELM327 Classic Bluetooth ====
BluetoothSerial SerialBT;
#define ELM_PORT SerialBT
#define DEBUG_PORT Serial
#define ELM327_BT_PIN "1234"

// ==== Gauge setup ====
const int centerX = 64;
const int centerY = 55;
const int radius = 28;
const int minAngle = -120;
const int maxAngle = 120;

// ==== Screen control ====
const int screenNumbers = 6;
uint8_t screenIndex = 0;
unsigned long lastSwitch = 0;

// ==== Debounce ====
#define BUTTON_PIN 32
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;

ELM327 myELM327;

uint8_t elm_address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

#define BT_DISCOVER_TIME 500000
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE; // or ESP_SPP_SEC_ENCRYPT|ESP_SPP_SEC_AUTHENTICATE to request pincode confirmation
esp_spp_role_t role = ESP_SPP_ROLE_MASTER; // or ESP_SPP_ROLE_MASTER

float turbo_kpa = 0.0;
float coolant_temp = 0.0;
float intake_temp = 0.0;
float engine_load = 0.0;
float battery_voltage = 0.0;
float oil_temp = 0.0;

float boostPressure = 0.0;
float intakeTemp = 0.0;
float engineLoad = 0.0;
float coolantTemp = 0.0;
float batteryVoltage = 0.0;
float fuelLevel = 0.0;
float oilTemp = 0.0;

void restart_ESP()
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 25, "REBOOTING");
  display.display();
  delay(1000);
  ESP.restart();
}

void draw_ScreenNumber(uint8_t index)
{
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 52, "Screen " + String(index + 1) + "/" + String(screenNumbers));
}
// ==== Draw bottom text ====
void draw_BotomText(String text)
{
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawRect(0, 54, 128, 10);
  display.setColor(BLACK);
  display.fillRect(0, 54, 128, 10);
  display.setColor(WHITE);
  display.drawString(centerX, 52, text);
}

// ==== Draw semicircular gauge ====
void draw_InfoText(String title, float value, String unit)
{
  // Labels
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(centerX, 0, title);
  display.setFont(ArialMT_Plain_24);
  display.drawString(centerX, 20, String(value, 0) + " " + unit);
  draw_BotomText(version_string);
  draw_ScreenNumber(screenIndex);
  display.display();
}

void draw_BoostScreen()
{
  turbo_kpa = myELM327.manifoldPressure();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    boostPressure = turbo_kpa;
  }
  draw_InfoText("Boost", boostPressure, "kPa");
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

void draw_EngineLoadScreen()
{
  engine_load = myELM327.engineLoad();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    engineLoad = engine_load;
  }
  draw_InfoText("Charge moteur", engineLoad, "%");
}

void draw_BatteryVoltageScreen()
{
  battery_voltage = myELM327.batteryVoltage();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    batteryVoltage = battery_voltage;
  }
  draw_InfoText("Tension Bat", batteryVoltage, "V");
}

void draw_OilTempScreen()
{
  oil_temp = myELM327.oilTemp(); // Placeholder, replace with actual oil temp PID if available
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    oilTemp = oil_temp;
  }
  draw_InfoText("Temp Huile", oilTemp, "°C");
}

void draw_CoolantTempScreen()
{
  coolant_temp = myELM327.engineCoolantTemp();
  if (myELM327.nb_rx_state == ELM_SUCCESS)
  {
    coolantTemp = coolant_temp;
  }
  draw_InfoText("Temp LdR", coolantTemp, "°C");
}

void draw_NoDataScreen()
{
  draw_InfoText("No Data", 0, "");
}

void draw_GaugeScreen(uint8_t index)
{
  switch (index)
  {
  case 0:
    draw_BoostScreen();
    break;
  case 1:
    draw_IntakeTempScreen();
    break;
  case 2:
    draw_EngineLoadScreen();
    break;
  case 3:
    draw_BatteryVoltageScreen();
    break;
  case 4:
    draw_OilTempScreen();
    break;
  case 5:
    draw_CoolantTempScreen();
    break;
  default:
    draw_NoDataScreen();
    break;
  }
}

// ==== Display error ====
void displayError(String msg)
{
  display.clear();
  display.invertDisplay();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 25, "ERROR!");
  draw_BotomText(msg);
  display.display();
}

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
  display.displayOn();
  for (int i = 0; i < steps; i++)
  {
    display.setBrightness(i * 25);
    delay(20);
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // ==== OLED init ====
  display.init();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(4, 0, "Fauwzk"); // Top left
  display.drawString(32, 16, "Engineering");
  draw_BotomText(version_string);
  display.display();
  delay(1000);
  draw_BotomText("Starting...");
  display.display();
  delay(1000);

  draw_BotomText("EEPROM Init");
  display.display();
  // ==== EEPROM init ====
  EEPROM.begin(EEPROM_SIZE);
  draw_BotomText("EEPROM Init done");
  display.display();
  delay(500);

  // Read the last screen index from EEPROM
  screenIndex = EEPROM.read(0);
  if (screenIndex >= screenNumbers)
  {
    screenIndex = 0; // Reset to 0 if out of bounds
  }
  draw_BotomText("Last screen: " + String(screenIndex + 1) + "/" + String(screenNumbers));
  display.display();

  // ==== Connect to Classic Bluetooth ELM327 ====
  if (!SerialBT.begin("elmFauwzk", true))
  { // true = master mode
    draw_BotomText("BT INIT FAIL");
    display.invertDisplay();
    display.display();
    delay(1000);
    restart_ESP();
  }
  draw_BotomText("BT Scan");
  display.display();
  SerialBT.setPin(ELM327_BT_PIN);
  delay(2000); // wait for connection
  // Connect to the paired device by MAC address
  if (!SerialBT.connect(elm_address, sec_mask, role))
  {
    displayError("BT Conn FAIL");
    delay(1000);
    restart_ESP();
  }
  draw_BotomText("BT Initialized");
  display.display();

  if (!myELM327.begin(SerialBT, true, 2000))
  {
    displayError("ELM327 INIT FAIL");
    delay(1000);
    restart_ESP();
  }
  draw_BotomText("ELM327 Connected");
  display.display();

  delay(500);
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.invertDisplay();
  display.drawString(64, 25, "All up!");
  display.display();
  delay(750);
  display.normalDisplay();
}

void loop()
{
  display.clear();

  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPress) > debounceDelay)
  {
    lastButtonPress = millis();
    fadeTransition((screenIndex + 1) % screenNumbers);
    screenIndex = (screenIndex + 1) % screenNumbers;
    EEPROM.write(0, screenIndex);
    EEPROM.commit();
    lastSwitch = millis();
  }

  // ==== Draw current gauge ====
  draw_GaugeScreen(screenIndex);
  display.display();

  delay(10);
}
