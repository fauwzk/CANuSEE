#include "network_ota.h"

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <DNSServer.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "app_state.h"
#include "display.h"
#include "version.h"

// =================================================================
// CONFIGURATION DE L'AUTO-UPDATER GITHUB (HOTSPOT TELEPHONE)
// =================================================================
static const char *HOTSPOT_SSID = "iPhone 15 Pro de Axel"; // <-- METS LE NOM DE TON PARTAGE DE CO ICI
static const char *HOTSPOT_PASS = "polentes";              // <-- METS LE MOT DE PASSE DU PARTAGE DE CO ICI
static const char *GITHUB_REPO = "Fauwzk/CANuSEE";          // <-- EXEMPLE: "Dupont/CANuSEE"
// =================================================================

const char *configPortalSsid = "CANuSEE_Config";
static WebServer server(80);
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

bool ota_updating = false;
float ota_progress = 0.0;

static String generateWebPage()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
        return "<html><body><h3>File not found</h3></body></html>";
    String html = file.readString();
    file.close();
    return html;
}

static void startCaptivePortal()
{
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_AP);

    WiFi.setSleep(false);
    delay(100);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);

    WiFi.softAP(configPortalSsid, "12345678", 6, 0, 4);
    delay(500);

    dnsServer.start(DNS_PORT, "*", apIP);
}

void startConfigPortal()
{
    startCaptivePortal();
    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", generateWebPage()); });

    ElegantOTA.begin(&server);

    ElegantOTA.onStart([]()
                       {
        ota_updating = true;
        ota_progress = 0.0; });
    ElegantOTA.onProgress([](size_t current, size_t final)
                          { ota_progress = (float)current / (float) final; });
    ElegantOTA.onEnd([](bool success)
                     { ota_updating = false; });

    server.begin();
}

void configPortalLoop()
{
    server.handleClient();
    ElegantOTA.loop();
    dnsServer.processNextRequest();
}

void performAutoUpdate()
{
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

    drawAutoUpdateStatusScreen("Connecting Wi-Fi...");

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20)
    {
        delay(500);
        timeout++;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        drawAutoUpdateStatusScreen("Wi-Fi Timeout!");
        delay(3000);
        currentState = STATE_MENU;
        return;
    }

    drawAutoUpdateStatusScreen("Checking GitHub...");

    WiFiClientSecure client;
    client.setInsecure(); // Important pour les API HTTPS sans certificat

    HTTPClient http;
    http.begin(client, "https://github.com/" + String(GITHUB_REPO) + "/releases/latest");
    const char *headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);

    int httpCode = http.GET();
    String latestTag = "";

    // GitHub renvoie un 302 Redirect vers l'URL contenant le tag de la dernière release.
    if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
    {
        String location = http.header("Location");
        int lastSlash = location.lastIndexOf('/');
        if (lastSlash > 0)
        {
            latestTag = location.substring(lastSlash + 1);
        }
    }
    http.end();

    if (latestTag == "")
    {
        drawAutoUpdateStatusScreen("Repo not found!");
        delay(3000);
        currentState = STATE_MENU;
        return;
    }

    if (latestTag == String(FW_VERSION))
    {
        drawAutoUpdateStatusScreen("Already Up to Date!");
        delay(3000);
        currentState = STATE_MENU;
        return;
    }

    drawAutoUpdateStatusScreen("Updating FS...");

    String fsURL = "https://github.com/" + String(GITHUB_REPO) + "/releases/download/" + latestTag + "/littlefs.bin";
    String fwURL = "https://github.com/" + String(GITHUB_REPO) + "/releases/download/" + latestTag + "/firmware.bin";

    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    // Réutilise l'écran OTA existant pour afficher la progression.
    httpUpdate.onProgress([](int current, int final)
                          {
        ota_progress = (float)current / (float)final;
        u8g2.clearBuffer();
        drawOTAScreen();
        u8g2.sendBuffer(); });

    // 1. Mise à jour de LittleFS
    t_httpUpdate_return retFS = httpUpdate.updateSpiffs(client, fsURL);

    if (retFS == HTTP_UPDATE_OK)
    {
        drawAutoUpdateStatusScreen("Updating FW...");

        // 2. Mise à jour du firmware principal
        t_httpUpdate_return retFW = httpUpdate.update(client, fwURL);

        if (retFW == HTTP_UPDATE_OK)
        {
            drawAutoUpdateStatusScreen("Success! Rebooting...");
            delay(1000);
            ESP.restart();
        }
    }

    drawAutoUpdateStatusScreen("Update Failed!");
    delay(3000);
    currentState = STATE_MENU;
}
