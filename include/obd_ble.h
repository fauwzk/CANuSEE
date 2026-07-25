#pragma once
#include <Arduino.h>

// Statut texte affiché sur l'écran de connexion et l'écran "BLE Status".
extern String bleStatusStr;
extern bool bleConnected;
extern uint32_t packetsReceived;

// Avancement de l'initialisation ELM327 : 0-4 = commandes AT en cours,
// 5 = adaptateur prêt et interrogation des PID démarrée.
extern uint8_t elmInitStep;
extern String elmBuffer; // dernière réponse brute reçue (debug écran BLE Status)

// Dernières valeurs OBD lues sur le bus (mises à jour par processBLE()).
extern float mapPressure, mafPressure, intakeTemp, engineLoad, engineRPM;
extern float coolantTemp, turboPressureState, targetBoost;
extern float dashBoost, dashIAT, dashCoolant, dashRPM, dashLoad;

// Chrono 0 -> TARGET_SPEED, dérivé du PID vitesse (0x0D).
extern bool timerRunning, timerReady;
extern unsigned long speedTimerStart;
extern float lastTimerValue, currentSpeed;

// Initialise la pile BLE et démarre la recherche de l'adaptateur OBD.
// À appeler une fois dans setup() (hors mode portail de configuration).
void initBLE();

// Fait avancer le scan/la connexion BLE ainsi que la machine à états
// ELM327 (handshake AT puis boucle de polling des PID). À appeler à
// chaque tour de loop() tant qu'on est en STATE_CONNECTING ou STATE_GAUGES.
void processBLE();
