# CANuSEE

Petit tableau de bord OBD-II sur écran OLED, basé sur un ESP32-C3 qui se
connecte en Bluetooth Low Energy à un adaptateur ELM327 (type "OBDBLE")
branché sur la prise diagnostic du véhicule.

## Fonctionnalités

- 9 écrans de jauges, accessibles avec les boutons Haut/Bas :
  MAP/MAF, Boost, Température IAT, Charge moteur, Température liquide de
  refroidissement, Dashboard (vue combinée), Chrono 0-100 km/h,
  Vitesse, Statut BLE.
- Pour les jauges qui s'y prêtent (Boost, IAT, Charge, LdR), 4 styles
  d'affichage au choix : texte, graphe historique, cadran rond, barre linéaire.
- Menu de réglages (bouton MENU) : style de jauge, luminosité de l'écran,
  bornes min/max du boost, vitesse cible du chrono.
- Réglages persistés en EEPROM (conservés entre deux redémarrages).
- Portail de configuration WiFi (point d'accès + captive portal) pour flasher
  le firmware manuellement via [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA).
- Mise à jour automatique du firmware et du filesystem depuis les
  [releases GitHub](https://github.com/Fauwzk/CANuSEE/releases) du dépôt,
  en se connectant au partage de connexion d'un téléphone.

## Matériel

- Carte ESP32-C3 (ex: "ESP32-C3 Super Mini")
- Écran OLED SSD1306 128x64 en I2C
- 4 boutons poussoirs (Haut / Bas / OK / Menu)
- Adaptateur OBD-II Bluetooth Low Energy compatible ELM327

## Structure du code

Le firmware (`src/`, `include/`) est découpé par domaine :

| Fichier              | Rôle                                                                                                 |
| -------------------- | ---------------------------------------------------------------------------------------------------- |
| `main.cpp`           | `setup()` / `loop()` : lecture des boutons, animation de démarrage, orchestration des autres modules |
| `app_state.h/.cpp`   | État courant de l'interface (écran de jauge, menu, édition, portail config...)                       |
| `config.h/.cpp`      | Réglages persistés en EEPROM et variables ajustables depuis le menu                                  |
| `obd_ble.h/.cpp`     | Scan/connexion BLE à l'adaptateur OBD, protocole ELM327, décodage des PID OBD-II                     |
| `display.h/.cpp`     | Tout le rendu à l'écran (icônes vectorielles, jauges, menus, transition entre écrans)                |
| `menu.h/.cpp`        | Construction du menu principal et du sous-menu de style                                              |
| `network_ota.h/.cpp` | Portail WiFi de configuration + mise à jour automatique depuis GitHub                                |

## Compilation et flash

Le projet utilise [PlatformIO](https://platformio.org/).

```bash
pio run                # compiler
pio run -t upload      # flasher le firmware
pio run -t uploadfs    # flasher le filesystem (data/index.html, servi par le portail de config)
```

Cible : `esp32c3` (voir `platformio.ini`). Le numéro de version affiché au
démarrage (`FW_VERSION`) est généré automatiquement à la compilation par
`extra_scripts/set_version.py`.

## Configuration

- Broches des boutons : `include/config.h`.
- SSID/mot de passe du hotspot téléphone et dépôt GitHub utilisés par la
  mise à jour automatique : constantes en haut de `src/network_ota.cpp`.
- Pour entrer dans le portail de configuration WiFi, maintenir le bouton
  MENU enfoncé au démarrage.

## /!\ IA /!\

Développé avec l'aide de Claude (Anthropic) et de
Gemini (Google) pour une partie du code et de la documentation.
