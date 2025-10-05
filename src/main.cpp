#include <Wire.h>
#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <SSD1306Wire.h>

bool debug = false;
String version = "v0.1";

// ==== OLED ====
SSD1306Wire display(0x3C, 21, 22); // SDA=21, SCL=22

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

#define BUTTON_PIN 32

ELM327 myELM327;

uint8_t address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

#define BT_DISCOVER_TIME 500000
esp_spp_sec_t sec_mask = ESP_SPP_SEC_NONE; // or ESP_SPP_SEC_ENCRYPT|ESP_SPP_SEC_AUTHENTICATE to request pincode confirmation
esp_spp_role_t role = ESP_SPP_ROLE_MASTER; // or ESP_SPP_ROLE_MASTER

float turbo_kpa = 0.0;

// ==== Draw bottom text ====
void draw_BotomText(String text)
{
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(centerX, 52, text);
}

// ==== Display error ====
void displayError(const String &msg)
{
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, msg);
    display.display();
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
    draw_BotomText("CANuSEE " + version);
    display.display();
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
    draw_BotomText("Starting...");
    display.display();

    delay(1000);
    if (!debug)
    {
        // ==== Connect to Classic Bluetooth ELM327 ====
        if (!SerialBT.begin("elmFauwzk", true))
        { // true = master mode
            draw_BotomText("INIT FAIL");
            display.display();
            while (1)
                ;
        }

        SerialBT.setPin(ELM327_BT_PIN);
        delay(2000); // wait for connection
        // Connect to the paired device by MAC address
        if (!SerialBT.connect(address, sec_mask, role))
        {
            draw_BotomText("CONNECT FAIL");
            display.display();
            while (1)
                ;
        }
        draw_BotomText("BT Connected");
        display.display();
        if (!myELM327.begin(SerialBT, true, 2000))
        {
            draw_BotomText("ELM327 FAIL");
            display.display();
            while (true)
                ;
        }
        draw_BotomText("OBD2 Connected");
        display.display();
    }

    delay(500);
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, "All up!");
    display.display();
    delay(750);
}

void loop()
{
    display.clear();
    if (debug)
    {
        turbo_kpa = random(80, 280);
    }
    else
    {
        turbo_kpa = myELM327.manifoldPressure();
    }

    if (myELM327.nb_rx_state == ELM_SUCCESS || debug)
    {
        turbo_kpa -= 100.0; // convert absolute → relative
        draw_InfoText("Boost", turbo_kpa, "kPa");
    }
    else if (myELM327.nb_rx_state != ELM_GETTING_MSG)
    {
        myELM327.printError();
    }
    if (debug)
    {
        delay(500);
    }
    else
    {
        delay(10);
    }
}
