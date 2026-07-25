#pragma once
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern const int centerX;

// ---- Mise en page de l'écran de démarrage / connexion ----
// (utilisées à la fois par l'animation de boot dans setup() et par drawConnectingScreen())
extern const int LOGO_OFFSET_Y;
extern const int UI_BASE_Y;
extern const int UI_TEXT_Y;
extern const int UI_BAR_Y;

enum IconType
{
    ICON_EXIT,
    ICON_GEAR,
    ICON_SUN,
    ICON_GAUGE,
    ICON_TURBO,
    ICON_TEMP,
    ICON_ENGINE,
    ICON_TIMER,
    ICON_BLE,
    ICON_DASH,
    ICON_SLIDERS,
    ICON_AIR,
    ICON_UPDATE
};

void setOledBrightness(uint8_t b);
void drawStringCenter(int y, String text);
void drawStringLeft(int x, int y, String text);
void drawStringRight(int x, int y, String text);
void drawVectorIcon(int cx, int cy, int type);

// Bandeau du haut commun à tous les écrans de jauge (titre + "n/total").
void draw_StatusBar(String title);

void drawMenuScreen();
void drawEditScreen(String title, String valueStr, float progress);
void drawConnectingScreen();
void drawConfigScreen();
void drawOTAScreen();
void drawAutoUpdateStatusScreen(String msg);

// Dessine l'écran de jauge d'index `index` (0..screenNumbers-1), avec le
// style (texte/graphe/cadran/barre) choisi par l'utilisateur le cas échéant.
void draw_GaugeScreen(uint8_t index);

// ---- Animation de transition (glissement) entre deux écrans de jauges ----
extern bool isTransitioning;
extern int slideOffset, slideDirection;

// Capture l'écran actuellement affiché puis démarre le glissement vers le
// nouvel écran de jauge (appelé quand l'utilisateur change d'écran).
void startScreenTransition();

// Fait avancer l'animation d'un pas ; à appeler juste après avoir dessiné
// le nouvel écran de jauge tant que isTransitioning est vrai.
void updateScreenTransition();
