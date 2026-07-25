#include "config.h"
#include <EEPROM.h>
#include "version.h"

// Structure brute écrite/lue telle quelle en EEPROM : ne pas réordonner les
// champs existants ni changer leur type sans invalider les réglages déjà
// sauvegardés sur les appareils en circulation.
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
static Settings cfg;

const int screenNumbers = 9;
uint8_t screenIndex = 0;
const char *screenNames[] = {"MAP/MAF", "Boost", "IAT", "Load", "Coolant", "Dash", "Timer", "Speed", "BLE"};

int BOOST_SCREEN = 0, ENGLOAD_SCREEN = 0, COOLANT_SCREEN = 0, IAT_SCREEN = 0;
int TICK_LINE_GAUGE = 2, TARGET_SPEED = 100, OLED_BRIGHTNESS = 255;
float TURBO_MIN_BAR = -0.7, TURBO_MAX_BAR = 1.5;

String version_string = "CANuSEE " FW_VERSION;

static void loadValues()
{
    EEPROM.get(0, cfg);
    // cfg peut contenir n'importe quoi (EEPROM vierge) : on retombe sur des
    // valeurs par défaut sûres si les données lues sont hors bornes.
    screenIndex = (cfg.last_screen >= 0 && cfg.last_screen < screenNumbers) ? cfg.last_screen : 0;
    BOOST_SCREEN = constrain(cfg.boost_screen_type, 0, 3);
    TURBO_MIN_BAR = cfg.turbo_min;
    TURBO_MAX_BAR = cfg.turbo_max;
    ENGLOAD_SCREEN = constrain(cfg.engload_screen_type, 0, 3);
    COOLANT_SCREEN = constrain(cfg.coolant_screen_type, 0, 3);
    IAT_SCREEN = constrain(cfg.intake_temp_screen_type, 0, 3);
    TICK_LINE_GAUGE = (cfg.tick_line_gauge > 0) ? cfg.tick_line_gauge : 2;
    TARGET_SPEED = constrain(cfg.target_speed, 10, 300);
    OLED_BRIGHTNESS = constrain(cfg.brightness, 0, 255);
}

void configBegin()
{
    EEPROM.begin(EEPROM_SIZE);
    loadValues();
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
