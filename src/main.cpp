#include <Wire.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <SSD1306Wire.h>

// ==== EEPROM Setup ====
#define EEPROM_SIZE 1

// ==== Debug Mode ====
bool debug = false;

// ==== Version ====
String version = "v0.5";
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

// ==== Bluetooth setup ====
#define BT_DISCOVER_TIME 500000
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE; // or ESP_SPP_SEC_ENCRYPT|ESP_SPP_SEC_AUTHENTICATE to request pincode confirmation
esp_spp_role_t role = ESP_SPP_ROLE_MASTER; // or ESP_SPP_ROLE_MASTER

// ==== Variables for OBD-II data ====
double atmo_kpa = 0.0;
double maf_kpa = 0.0;
double coolant_temp = 0.0;
double intake_temp = 0.0;
double engine_load = 0.0;
double battery_voltage = 0.0;
double oil_temp = 0.0;
double turbo_pressure = 0.0;

// ==== Variables to hold last valid readings ====
double atmoPressure = 0.0;
double mafPressure = 0.0;
double intakeTemp = 0.0;
double engineLoad = 0.0;
double coolantTemp = 0.0;
double batteryVoltage = 0.0;
double fuelLevel = 0.0;
double oilTemp = 0.0;
double turboPressure = 0.0;

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
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(centerX, 0, title);
  display.setFont(ArialMT_Plain_24);
  display.drawString(centerX, 20, String(value) + " " + unit);
  draw_BottomText(version_string);
  draw_ScreenNumber(screenIndex);
  display.display();
}

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
    batteryVoltage = battery_voltage - 2.0; // Adjust for alternator voltage drop
  }
  draw_InfoText("Tension Bat", batteryVoltage, "V");
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

void draw_TurboPressureScreen()
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
    draw_TurboPressureScreen();
    break;
  case 2:
    draw_IntakeTempScreen();
    break;
  case 3:
    draw_EngineLoadScreen();
    break;
  case 4:
    draw_BatteryVoltageScreen();
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
  draw_BottomText(msg);
  display.display();
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
  display.displayOn();
  for (int i = 0; i < steps; i++)
  {
    display.setBrightness(i * 25);
    delay(20);
  }
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
  display.drawString(4, 0, "Fauwzk"); // Top left
  display.drawString(32, 16, "Engineering");
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
  delay(500);

  // Read the last screen index from EEPROM
  screenIndex = EEPROM.read(0);
  if (screenIndex >= screenNumbers)
  {
    screenIndex = 0; // Reset to 0 if out of bounds
  }
  draw_BottomText("Last screen: " + String(screenIndex + 1) + "/" + String(screenNumbers));
  display.display();

  // ==== Connect to Classic Bluetooth ELM327 ====
  if (!SerialBT.begin("elmFauwzk", true))
  { // true = master mode
    draw_BottomText("BT INIT FAIL");
    display.invertDisplay();
    display.display();
    delay(1000);
    restart_ESP();
  }
  draw_BottomText("BT Init done");
  display.display();
  SerialBT.setPin(ELM327_BT_PIN);
  delay(2000); // wait for connection

  // Connect to the paired device by MAC address
  draw_BottomText("BT Connecting...");
  display.display();
  if (!SerialBT.connect(elm_address, sec_mask, role))
  {
    displayError("BT Conn FAIL");
    delay(1000);
    restart_ESP();
  }
  draw_BottomText("BT Connected");
  display.display();

  // ==== ELM327 init ====
  draw_BottomText("ELM327 Init...");
  display.display();
  if (!myELM327.begin(SerialBT, true, 2000))
  {
    displayError("ELM327 INIT FAIL");
    delay(1000);
    restart_ESP();
  }
  draw_BottomText("ELM327 Connected");
  display.display();

  delay(500);

  // ==== Initial screen ====
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "All up!");
  display.display();
  delay(750);
  display.normalDisplay();
}

// ==== Main loop ====
void loop()
{
  // Clear display for new frame
  display.clear();

  // ==== Check button press ====
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
