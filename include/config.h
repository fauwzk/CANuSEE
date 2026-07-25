#pragma once
#include <Arduino.h>

// ---- Broches des 4 boutons physiques ----
#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 3
#define BTN_MENU 6

// Nombre total d'écrans de jauges disponibles (voir screenNames[]).
extern const int screenNumbers;
// Index de l'écran de jauge actuellement affiché (persisté en EEPROM).
extern uint8_t screenIndex;
extern const char *screenNames[];

// Style d'affichage choisi pour chaque jauge qui en propose plusieurs
// (0 = texte, 1 = graphe historique, 2 = cadran rond, 3 = barre linéaire).
extern int BOOST_SCREEN, ENGLOAD_SCREEN, COOLANT_SCREEN, IAT_SCREEN;

extern int TICK_LINE_GAUGE;   // réservé pour un futur réglage de graduation
extern int TARGET_SPEED;      // vitesse cible (km/h) du chrono 0-100
extern int OLED_BRIGHTNESS;   // luminosité de l'écran OLED (0-255)
extern float TURBO_MIN_BAR, TURBO_MAX_BAR; // bornes affichées pour la jauge de boost

// "CANuSEE <FW_VERSION>", affiché au démarrage.
extern String version_string;

// Réglages utilisateur persistés en EEPROM (voir Settings dans config.cpp).
// Doit être appelé une fois dans setup() avant toute autre fonction de ce module :
// initialise l'EEPROM et recharge les valeurs ci-dessus depuis la mémoire.
void configBegin();

// Sauvegarde les valeurs courantes (screenIndex, styles, bornes, etc.) en EEPROM.
// À appeler après toute modification faite depuis le menu.
void saveValues();
