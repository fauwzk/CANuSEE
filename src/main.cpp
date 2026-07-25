// =========================================================
// CANuSEE — point d'entrée (setup/loop) : boutons, machine à
// états UI et orchestration des modules ci-dessous.
//   - app_state    : état courant de l'interface (AppState)
//   - config       : réglages persistés en EEPROM
//   - obd_ble      : scan/connexion BLE + protocole ELM327/OBD
//   - display      : dessin de tous les écrans OLED
//   - menu         : construction des menus
//   - network_ota  : portail de config WiFi + mise à jour firmware
// =========================================================
#include <Arduino.h>
#include <LittleFS.h>

#include "epd_bitmap_logo_3008.h"
#include "app_state.h"
#include "config.h"
#include "obd_ble.h"
#include "display.h"
#include "menu.h"
#include "network_ota.h"

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
                return state;
            }
        }
        return false;
    }
};

Button btnUp(BTN_UP);
Button btnDown(BTN_DOWN);
Button btnOk(BTN_OK);
Button btnMenu(BTN_MENU);

void restart_ESP()
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvR12_tr);
    drawStringCenter(35, "REBOOTING!");
    u8g2.sendBuffer();
    delay(1000);
    ESP.restart();
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);
    pinMode(BTN_MENU, INPUT_PULLUP);
    if (digitalRead(BTN_MENU) == LOW)
        currentState = STATE_CONFIG;

    u8g2.begin();
    u8g2.setBusClock(400000);
    configBegin();
    setOledBrightness(OLED_BRIGHTNESS);

    // ---- Animation de démarrage : révélation du logo puis barre de version ----
    for (int h = 0; h <= UI_BASE_Y; h += 2)
    {
        u8g2.clearBuffer();

        u8g2.setClipWindow(0, 0, 128, h);
        u8g2.drawXBM(0, LOGO_OFFSET_Y, 128, 64, epd_bitmap_logo_3008);
        u8g2.setMaxClipWindow();

        if (h < UI_BASE_Y)
        {
            u8g2.setDrawColor(1);
            u8g2.drawLine(0, h, 128, h);
            u8g2.drawLine(0, h + 1, 128, h + 1);
        }
        u8g2.sendBuffer();
        delay(10);
    }

    delay(150);

    for (int y = 64; y >= UI_BASE_Y; y -= 2)
    {
        u8g2.clearBuffer();

        u8g2.drawXBM(0, LOGO_OFFSET_Y, 128, 64, epd_bitmap_logo_3008);

        u8g2.setDrawColor(0);
        u8g2.drawBox(0, y, 128, 64 - y);
        u8g2.setDrawColor(1);
        u8g2.drawLine(0, y, 128, y);

        u8g2.sendBuffer();
        delay(10);
    }

    for (int i = 0; i <= 100; i += 6)
    {
        u8g2.clearBuffer();
        u8g2.drawXBM(0, LOGO_OFFSET_Y, 128, 64, epd_bitmap_logo_3008);

        u8g2.setDrawColor(0);
        u8g2.drawBox(0, UI_BASE_Y, 128, 64 - UI_BASE_Y);
        u8g2.setDrawColor(1);
        u8g2.drawLine(0, UI_BASE_Y, 128, UI_BASE_Y);

        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(4, UI_TEXT_Y, "CANuSEE");

        u8g2.setFont(u8g2_font_5x7_tr);
        drawStringRight(124, UI_TEXT_Y, version_string);

        u8g2.drawFrame(4, UI_BAR_Y, 120, 6);
        int barWidth = (i * 116) / 100;
        if (barWidth > 0)
            u8g2.drawBox(6, UI_BAR_Y + 2, barWidth, 2);

        u8g2.sendBuffer();
        delay(15);
    }

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
        startConfigPortal();
    else
        initBLE();
}

void loop()
{
    // Mode Auto Update : bloquant, dessine sa propre UI de progression.
    if (currentState == STATE_AUTO_UPDATE)
    {
        performAutoUpdate();
        return; // Évite de redessiner l'UI normale par-dessus pendant la mise à jour.
    }

    if (currentState == STATE_CONFIG)
        configPortalLoop();

    if (currentState == STATE_GAUGES || currentState == STATE_CONNECTING)
        processBLE();

    bool upPressed = btnUp.pressed(), downPressed = btnDown.pressed(), okPressed = btnOk.pressed(), menuPressed = btnMenu.pressed();

    if (menuPressed)
    {
        if (currentState == STATE_CONFIG || currentState == STATE_CONNECTING)
            restart_ESP();
        else if (currentState == STATE_GAUGES || currentState == STATE_STYLE_MENU)
        {
            buildMenu();
            currentState = STATE_MENU;
        }
        else if (currentState == STATE_MENU || currentState >= STATE_EDIT_MIN)
            currentState = STATE_GAUGES;
    }

    if (upPressed || downPressed)
    {
        int dir = upPressed ? -1 : 1;
        if (currentState == STATE_GAUGES && !isTransitioning)
        {
            slideDirection = dir;
            startScreenTransition();
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
            TURBO_MIN_BAR = constrain(TURBO_MIN_BAR + (dir * -0.1), -1.0, 0.5);
        else if (currentState == STATE_EDIT_MAX)
            TURBO_MAX_BAR = constrain(TURBO_MAX_BAR + (dir * -0.1), 0.5, 3.0);
        else if (currentState == STATE_EDIT_SPEED)
            TARGET_SPEED = constrain(TARGET_SPEED + (dir * -10), 40, 200);
        else if (currentState == STATE_EDIT_BRIGHTNESS)
        {
            OLED_BRIGHTNESS = constrain(OLED_BRIGHTNESS + (dir * -25), 0, 255);
            setOledBrightness(OLED_BRIGHTNESS);
        }
    }

    if (okPressed)
    {
        if (currentState == STATE_MENU)
        {
            int action = currentMenu[menuCursor].action;
            if (action == ACT_CLOSE)
                currentState = STATE_GAUGES;
            else if (action == ACT_AUTO_UPDATE)
            {
                currentState = STATE_AUTO_UPDATE;
            }
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
        }
        else if (currentState == STATE_STYLE_MENU)
        {
            int action = currentMenu[menuCursor].action;
            if (action == ACT_BACK_TO_MENU)
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

    unsigned long refreshInterval = (currentState == STATE_CONFIG) ? 100 : 40;
    static unsigned long lastDrawTime = 0;

    if (millis() - lastDrawTime > refreshInterval)
    {
        lastDrawTime = millis();

        u8g2.clearBuffer();
        if (currentState == STATE_CONNECTING)
            drawConnectingScreen();
        else if (currentState == STATE_GAUGES)
        {
            draw_GaugeScreen(screenIndex);
            if (isTransitioning)
                updateScreenTransition();
        }
        else if (currentState == STATE_CONFIG)
        {
            if (ota_updating)
                drawOTAScreen();
            else
                drawConfigScreen();
        }
        else if (currentState == STATE_MENU || currentState == STATE_STYLE_MENU)
            drawMenuScreen();

        else if (currentState == STATE_EDIT_MIN)
            drawEditScreen("Turbo Min", String(TURBO_MIN_BAR, 1) + " b", (TURBO_MIN_BAR + 1.0) / 1.5);
        else if (currentState == STATE_EDIT_MAX)
            drawEditScreen("Turbo Max", String(TURBO_MAX_BAR, 1) + " b", (TURBO_MAX_BAR - 0.5) / 2.5);
        else if (currentState == STATE_EDIT_SPEED)
            drawEditScreen("Target Speed", String(TARGET_SPEED) + " km/h", (TARGET_SPEED - 40.0) / 160.0);
        else if (currentState == STATE_EDIT_BRIGHTNESS)
            drawEditScreen("Brightness", String(map(OLED_BRIGHTNESS, 0, 255, 0, 100)) + " %", OLED_BRIGHTNESS / 255.0);

        u8g2.sendBuffer();
    }
    yield();
}
