#include <Wire.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <FS.h>

// ==== WiFi Config ====
const char *ssid = "CANuSEE_Config";

// ==== Web Server ====
WebServer server(80);

// ==== EEPROM Setup ====
#define EEPROM_SIZE 4
#define EEPROM_LAST_SCREEN 0
#define EEPROM_BOOST_SCREEN_TYPE 1
#define EEPROM_TURBO_MIN_ADDR 2
#define EEPROM_TURBO_MAX_ADDR 3

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

// ==== Display center coordinates ====
const int centerX = 64;
const int centerY = 55;

// ==== Screen control ====
const int screenNumbers = 7;
uint8_t screenIndex = 0;
uint8_t boostScreenType = 0; // 0 = text, 1 = gauge
uint8_t boostScreenTypes = 2;
unsigned long lastSwitch = 0;
const int itemsPerCol = 3; // lignes par colonne (2 colonnes visibles)

// ==== Turbo gauge limits ====
float TURBO_MIN_BAR = -0.4;
float TURBO_MAX_BAR = 1.6;

// ==== Debounce ====
#define BUTTON_PIN 32
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;
const unsigned long longPressDuration = 2000; // 2 seconds for long press

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
  html.replace("%SELECTED_TEXT%", (boostScreenType == 0) ? "selected" : "");
  html.replace("%SELECTED_GAUGE%", (boostScreenType == 1) ? "selected" : "");

  return html;
}

void saveTurboLimits()
{
  EEPROM.put(EEPROM_TURBO_MIN_ADDR, TURBO_MIN_BAR);
  EEPROM.put(EEPROM_TURBO_MAX_ADDR, TURBO_MAX_BAR);
  EEPROM.commit();
}

void loadTurboLimits()
{
  EEPROM.get(EEPROM_TURBO_MIN_ADDR, TURBO_MIN_BAR);
  EEPROM.get(EEPROM_TURBO_MAX_ADDR, TURBO_MAX_BAR);

  // Default if uninitialized or invalid
  if (isnan(TURBO_MIN_BAR) || isnan(TURBO_MAX_BAR) || TURBO_MAX_BAR <= TURBO_MIN_BAR)
  {
    TURBO_MIN_BAR = -0.4;
    TURBO_MAX_BAR = 1.6;
    saveTurboLimits();
  }
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

// ==== Draw line gauge with graduations ====
// Draws a horizontal line gauge that fills up as the value increases
void draw_LineGauge(double value, double minValue, double maxValue, String label, String unit)
{
  // Clamp value
  if (value < minValue)
    value = minValue;
  if (value > maxValue)
    value = maxValue;

  // Layout parameters
  int barX = 14;      // left position
  int barY = 14;      // top position
  int barWidth = 100; // total width of the bar
  int barHeight = 10; // height of the bar

  // Calculate fill width
  double range = maxValue - minValue;
  double fillPercent = (value - minValue) / range;
  int fillWidth = (int)(barWidth * fillPercent);

  // ==== Draw bar outline ====
  display.drawRect(barX, barY, barWidth, barHeight);

  // ==== Draw fill ====
  display.fillRect(barX, barY, fillWidth, barHeight);

  // ==== Draw graduations ====
  int tickCount = 5; // number of intermediate marks (between min and max)
  int tickHeight = 4;
  int labelOffsetY = barY + barHeight + 8;

  for (int i = 0; i <= tickCount; i++)
  {
    int tickX = barX + (barWidth * i) / tickCount;
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
  display.drawString(centerX, barY + barHeight + 20, String(value, 2) + " " + unit);

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
    displayInfo("DTCs Reset!");
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
    // --- Réglages de mise en page ---
    const int colWidth = display.getWidth() / 2; // largeur d'une colonne
    const int startY = 18;                       // position verticale de départ
    const int lineHeight = 10;                   // hauteur d'une ligne
    const int visibleItems = itemsPerCol * 2;    // total visible à l'écran

    static int scrollOffset = 0; // index de défilement
    static unsigned long lastScrollTime = 0;
    const unsigned long scrollInterval = 2000; // délai entre défilements (ms)

    // --- Défilement automatique ---
    if (millis() - lastScrollTime > scrollInterval)
    {
      scrollOffset++;
      if (scrollOffset > (codesFound - visibleItems))
        scrollOffset = 0;
      lastScrollTime = millis();
    }

    // --- Affichage des codes visibles ---
    for (uint8_t i = 0; i < visibleItems; i++)
    {
      int index = i + scrollOffset;
      if (index >= codesFound)
        break;

      int col = i / itemsPerCol; // 0 = gauche, 1 = droite
      int row = i % itemsPerCol;

      int x = (col == 0) ? colWidth / 2 : (colWidth + colWidth / 2);
      int y = startY + (row * lineHeight);

      String codeText = String(index + 1) + ". " + String(myELM327.DTC_Response.codes[index]);
      display.drawString(x, y, codeText);
    }
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
    if (boostScreenType == 0)
    {
      draw_TurboPressureTextScreen();
    }
    else
    {
      draw_TurboPressureLineScreen();
    }
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
  case 6:
    draw_dtcCodes();
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
  display.displayOn();
  for (int i = 0; i < steps; i++)
  {
    display.setBrightness(i * 25);
    delay(20);
  }
}

void drawSpinner()
{
  unsigned long waitStart = millis();
  const unsigned long waitTimeout = 3000; // 3 secondes max
  uint8_t spinnerIndex = 0;
  const char spinnerChars[] = {'|', '/', '-', '\\'}; // animation spinner

  while (myELM327.nb_rx_state == ELM_GETTING_MSG && millis() - waitStart < waitTimeout)
  {
    // Affiche une animation fluide dans le bas de l’écran
    String waitText = "Waiting data ";
    waitText += spinnerChars[spinnerIndex];
    draw_BottomText(waitText);
    display.display();

    spinnerIndex = (spinnerIndex + 1) % 4;
    delay(120); // vitesse de rotation du spinner
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
  delay(500);

  // Read the last screen index from EEPROM
  screenIndex = EEPROM.read(EEPROM_LAST_SCREEN);
  if (screenIndex >= screenNumbers)
  {
    screenIndex = 0; // Reset to 0 if out of bounds
  }
  draw_BottomText("Last screen: " + String(screenIndex + 1) + "/" + String(screenNumbers));
  display.display();
  delay(1000);

  boostScreenType = EEPROM.read(EEPROM_BOOST_SCREEN_TYPE);
  if (boostScreenType > boostScreenTypes - 1)
  {
    boostScreenType = 0; // Reset to 0 if out of bounds
  }
  draw_BottomText("Boost screen type: " + String(boostScreenType));
  display.display();
  delay(1000);

  // ==== Connect to Classic Bluetooth ELM327 ====
  if (!SerialBT.begin("CANuSEE", true))
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

  draw_BottomText("ELM327 Config...");
  display.display();
  myELM327.sendCommand(SET_ISO_BAUD_10400);
  myELM327.sendCommand(ALLOW_LONG_MESSAGES);
  draw_BottomText("ELM327 Config done");
  display.display();
  delay(500);

  // ==== Initial screen ====
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "All up!");
  display.display();
  delay(750);
  display.normalDisplay();

  // ==== Load Turbo Limits from EEPROM ====
  loadTurboLimits();

  // ==== WiFi Access Point Setup ====
  WiFi.softAP(ssid, NULL);
  Serial.println("WiFi AP started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  displayInfo("WiFi: " + WiFi.softAPIP().toString());
  display.display();
  delay(1000);

  // ==== Web Server Routes ====
  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html", generateWebPage()); });

  server.on("/save", HTTP_POST, []()
            {
  if (server.hasArg("min") && server.hasArg("max"))
  {
    TURBO_MIN_BAR = server.arg("min").toFloat();
    TURBO_MAX_BAR = server.arg("max").toFloat();
    saveTurboLimits();
    server.send(200, "text/html",
                "<html><body><h3>✅ Saved!</h3><a href='/'>Back</a></body></html>");
    displayInfo("Saved boost:\n" + String(TURBO_MIN_BAR) + " to " + String(TURBO_MAX_BAR));
    display.display();
  }
  else
  {
    server.send(400, "text/plain", "Missing parameters");
  } });

  server.begin();
  Serial.println("Web server started");
}

// ==== Main loop ====
void loop()
{
  server.handleClient();

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
      EEPROM.write(EEPROM_LAST_SCREEN, screenIndex);
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
        boostScreenType = (boostScreenType + 1) % boostScreenTypes;
        EEPROM.write(EEPROM_BOOST_SCREEN_TYPE, boostScreenType);
        EEPROM.commit();
        display.display();
        delay(1000);
        longPressHandled = true;
      }
      else if (screenIndex == 6)
      {
        resetDTCs();
        // If on DTC screen, reset DTC codes
        longPressHandled = true;
        if (buttonPressed)
        {
          restart_ESP();
        }
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

  // ==== Draw current gauge ====
  display.clear();
  draw_GaugeScreen(screenIndex);
  display.display();

  delay(10);
}