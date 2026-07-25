#pragma once
#include <Arduino.h>

#define MAX_MENU_ITEMS 24

struct MenuItem
{
    const char *text;
    int action;
    int iconType; // valeur de IconType (display.h)
};

extern MenuItem currentMenu[MAX_MENU_ITEMS];
extern int menuSize, menuCursor;

// Actions déclenchées par OK sur un item de menu (voir loop() dans main.cpp).
#define ACT_CLOSE 0
#define ACT_OPEN_STYLE_MENU 1
#define ACT_EDIT_MIN 2
#define ACT_EDIT_MAX 3
#define ACT_EDIT_SPEED 4
#define ACT_EDIT_BRIGHTNESS 5
#define ACT_ENTER_CONFIG 6
#define ACT_AUTO_UPDATE 7
#define ACT_GO_SCREEN_0 10 // + screenIndex pour "aller à l'écran N"
#define ACT_BACK_TO_MENU 30
#define ACT_SET_STYLE_TEXT 31
#define ACT_SET_STYLE_GRAPH 32
#define ACT_SET_STYLE_DIAL 33
#define ACT_SET_STYLE_BAR 34

// Construit le menu principal, adapté à l'écran de jauge courant
// (ex: "Turbo Min/Max" uniquement visible sur l'écran Boost).
void buildMenu();

// Construit le sous-menu de choix de style (Texte/Graphe/Cadran/Barre)
// et positionne le curseur sur le style actuellement sélectionné.
void buildStyleMenu();
