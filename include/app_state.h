#pragma once

// États possibles de l'interface (contrôle quel écran est dessiné et
// comment les boutons sont interprétés dans loop()).
enum AppState
{
    STATE_CONNECTING,      // Recherche/connexion BLE à l'adaptateur OBD
    STATE_GAUGES,           // Affichage normal d'une des jauges (écran principal)
    STATE_MENU,             // Menu principal (choix d'écran, réglages...)
    STATE_STYLE_MENU,       // Sous-menu de choix du style d'affichage d'une jauge
    STATE_EDIT_MIN,         // Édition de la borne min du boost
    STATE_EDIT_MAX,         // Édition de la borne max du boost
    STATE_EDIT_SPEED,       // Édition de la vitesse cible du chrono 0-100
    STATE_EDIT_BRIGHTNESS,  // Édition de la luminosité de l'écran OLED
    STATE_CONFIG,           // Portail WiFi de configuration (captive portal + OTA)
    STATE_AUTO_UPDATE       // Mise à jour automatique du firmware depuis GitHub
};

extern AppState currentState;
