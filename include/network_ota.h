#pragma once
#include <Arduino.h>

// SSID de l'AP WiFi du portail de configuration (mot de passe fixe "12345678").
extern const char *configPortalSsid;

extern bool ota_updating;
extern float ota_progress; // 0.0 -> 1.0, mis à jour par les deux voies d'OTA (locale et GitHub)

// Démarre le point d'accès WiFi + portail captif + serveur web (page de
// configuration et endpoint ElegantOTA pour un flash manuel de firmware.bin).
// À appeler une fois dans setup() si on démarre en STATE_CONFIG.
void startConfigPortal();

// Fait avancer le serveur web / DNS du portail de config. À appeler à
// chaque tour de loop() tant que currentState == STATE_CONFIG.
void configPortalLoop();

// Se connecte au point d'accès WiFi défini par HOTSPOT_SSID/HOTSPOT_PASS,
// vérifie la dernière release GitHub (GITHUB_REPO) et, si elle diffère de
// FW_VERSION, télécharge et flashe littlefs.bin puis firmware.bin.
// Bloquant : dessine sa propre UI de progression pendant toute l'opération.
void performAutoUpdate();
