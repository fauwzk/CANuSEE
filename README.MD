# CANuSEE - Afficheur OBD2 BLE pour Peugeot 3008

CANuSEE est un projet d'affichage déporté pour tableau de bord, conçu pour lire et afficher les données télémétriques d'un véhicule (optimisé pour Peugeot 3008) via un dongle OBD2 Bluetooth Low Energy (BLE). Le système repose sur un microcontrôleur ESP32-C3 Super Mini couplé à un écran OLED.

## Fonctionnalités Principales

*   **Connexion Sans Fil OBD2 :** Appairage automatique aux dongles OBD2 BLE compatibles ELM327.
*   **Affichage Télémétrique en Temps Réel :** Affichage de la pression du turbo (Boost), température d'admission (IAT), charge moteur, température du liquide de refroidissement, vitesse, régime moteur (RPM), ainsi que les pressions MAP et MAF.
*   **Chronomètre Automatique :** Mesure du temps d'accélération (0 à 100 km/h par défaut, cible paramétrable).
*   **Interface Graphique Dynamique :** Quatre styles d'affichage disponibles par jauge (Texte, Graphique avec historique, Cadran rond, Barre segmentée).
*   **Portail Captif de Configuration :** Serveur web intégré permettant de paramétrer l'affichage (luminosité, limites des jauges, cible de vitesse) depuis un smartphone.
*   **Mise à jour OTA (Over-The-Air) :** Module de téléchargement et d'installation autonome des mises à jour du firmware via Wi-Fi.

## Architecture et Fonctionnement du Code

Le programme est développé en C++ sous l'environnement PlatformIO. Il est structuré autour d'une machine à états et d'une boucle de rafraîchissement asynchrone.

### 1. La Machine à États (State Machine)

L'application est régie par la variable `currentState` qui détermine le comportement de la boucle principale :
*   `STATE_CONNECTING` : Initialisation du composant BLE, scan de l'environnement et connexion au dongle. Affiche l'animation de démarrage.
*   `STATE_GAUGES` : État nominal de conduite. Interroge le véhicule en continu et met à jour l'interface graphique.
*   `STATE_MENU` / `STATE_STYLE_MENU` : Interfaces de navigation pour modifier le style visuel ou accéder aux paramètres matériels via les boutons physiques.
*   `STATE_CONFIG` : Interruption du cycle BLE pour activer le mode Point d'Accès Wi-Fi et lancer le serveur web de configuration.
*   `STATE_AUTO_UPDATE` : Connexion à un réseau Wi-Fi externe pour rechercher et installer automatiquement la dernière version du firmware depuis GitHub.

### 2. Communication BLE et Protocole ELM327

La communication avec le véhicule s'effectue via les requêtes standards OBD2 (Mode 01) formatées pour la puce ELM327.
1.  **Initialisation :** Une fois connecté au BLE, l'ESP32 envoie une séquence de commandes Hayes (`ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATSP0`) pour configurer la vitesse et le formatage du dongle.
2.  **Requêtes Intelligentes :** La fonction `getNextSmartPID()` détermine dynamiquement quel paramètre demander en fonction de l'écran actuellement affiché par l'utilisateur. Cela évite de saturer la bande passante Bluetooth avec des données invisibles à l'écran.
3.  **Traitement (Parsing) :** Les réponses brutes en hexadécimal reçues sur la caractéristique RX sont analysées par `parseOBDResponse()`, qui isole les octets de données et applique les formules de conversion standard de l'OBD2 pour obtenir les valeurs physiques.

### 3. Interface Graphique et Rendu

L'affichage OLED I2C est piloté par la bibliothèque `U8g2`.
*   Afin d'économiser la mémoire flash et la RAM, les icônes de l'interface ne sont pas des images matricielles (bitmaps), mais sont dessinées de manière vectorielle via la fonction `drawVectorIcon()`.
*   Les transitions entre les différentes jauges utilisent un buffer mémoire temporaire (`oldScreenBuffer`) pour calculer un effet de glissement horizontal (Slide).
*   La boucle de rendu graphique est cadencée indépendamment des requêtes BLE. En mode conduite, l'écran est rafraîchi toutes les 40ms (25 FPS) avec les dernières valeurs brutes connues, garantissant une fluidité maximale sans ralentir la lecture des capteurs.

## Liste des PIDs OBD2 Utilisés

Le programme utilise les identifiants de paramètres (PIDs) suivants pour extraire la télémétrie :

*   **0x04 :** Charge moteur calculée (Pourcentage)
*   **0x05 :** Température du liquide de refroidissement (Degrés Celsius)
*   **0x0B :** Pression absolue du collecteur d'admission - MAP (kPa) - *Utilisé comme base pour le calcul de la pression Turbo.*
*   **0x0C :** Régime moteur - RPM (Tours par minute)
*   **0x0D :** Vitesse du véhicule (km/h)
*   **0x0F :** Température de l'air d'admission - IAT (Degrés Celsius)
*   **0x10 :** Débit d'air massique - MAF (Grammes par seconde)
*   **0x70 :** Pression de suralimentation commandée - Target Boost (Bar)

---
*Avertissement : Ce code, sa structure et cette documentation ont été partiellement générés, optimisés et documentés avec l'assistance d'une Intelligence Artificielle.*
