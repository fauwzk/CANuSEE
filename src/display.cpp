#include "display.h"

#include "epd_bitmap_logo_3008.h"
#include "config.h"
#include "menu.h"
#include "obd_ble.h"
#include "network_ota.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 21, 20);
const int centerX = 64;

const int LOGO_OFFSET_Y = -8;
const int UI_BASE_Y = 38;
const int UI_TEXT_Y = UI_BASE_Y + 10;
const int UI_BAR_Y = UI_BASE_Y + 14;

void setOledBrightness(uint8_t b) { u8g2.setContrast(b); }

void drawStringCenter(int y, String text)
{
    int w = u8g2.getStrWidth(text.c_str());
    u8g2.setCursor(centerX - (w / 2), y);
    u8g2.print(text);
}
void drawStringLeft(int x, int y, String text)
{
    u8g2.setCursor(x, y);
    u8g2.print(text);
}
void drawStringRight(int x, int y, String text)
{
    int w = u8g2.getStrWidth(text.c_str());
    u8g2.setCursor(x - w, y);
    u8g2.print(text);
}

// Toutes les icônes de menu sont dessinées vectoriellement (lignes/formes
// primitives u8g2) plutôt que stockées en bitmap, pour économiser la flash.
void drawVectorIcon(int cx, int cy, int type)
{
    switch (type)
    {
    case ICON_EXIT:
        u8g2.drawFrame(cx - 14, cy - 16, 14, 32);
        u8g2.drawBox(cx - 11, cy - 13, 8, 26);
        u8g2.drawLine(cx - 2, cy, cx + 14, cy);
        u8g2.drawTriangle(cx + 6, cy - 6, cx + 16, cy, cx + 6, cy + 6);
        break;
    case ICON_GEAR:
        u8g2.drawDisc(cx, cy, 12);
        u8g2.drawBox(cx - 4, cy - 16, 8, 32);
        u8g2.drawBox(cx - 16, cy - 4, 32, 8);
        u8g2.drawBox(cx - 12, cy - 12, 24, 24);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy, 6);
        u8g2.setDrawColor(1);
        break;
    case ICON_SUN:
        u8g2.drawDisc(cx, cy, 8);
        u8g2.drawLine(cx, cy - 12, cx, cy - 18);
        u8g2.drawLine(cx, cy + 12, cx, cy + 18);
        u8g2.drawLine(cx - 12, cy, cx - 18, cy);
        u8g2.drawLine(cx + 12, cy, cx + 18, cy);
        u8g2.drawLine(cx - 8, cy - 8, cx - 13, cy - 13);
        u8g2.drawLine(cx + 8, cy + 8, cx + 13, cy + 13);
        u8g2.drawLine(cx - 8, cy + 8, cx - 13, cy + 13);
        u8g2.drawLine(cx + 8, cy - 8, cx + 13, cy - 13);
        break;
    case ICON_GAUGE:
        u8g2.drawCircle(cx, cy + 8, 18);
        u8g2.setDrawColor(0);
        u8g2.drawBox(cx - 20, cy + 8, 40, 20);
        u8g2.setDrawColor(1);
        u8g2.drawLine(cx - 18, cy + 8, cx + 18, cy + 8);
        u8g2.drawLine(cx, cy + 8, cx + 12, cy - 4);
        u8g2.drawDisc(cx, cy + 8, 3);
        break;
    case ICON_TURBO:
        u8g2.drawDisc(cx, cy + 2, 14);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy + 2, 8);
        u8g2.setDrawColor(1);
        u8g2.drawDisc(cx, cy + 2, 3);
        u8g2.drawBox(cx + 4, cy - 14, 12, 12);
        u8g2.drawLine(cx, cy + 2, cx + 5, cy - 3);
        u8g2.drawLine(cx, cy + 2, cx - 5, cy - 3);
        u8g2.drawLine(cx, cy + 2, cx - 5, cy + 7);
        break;
    case ICON_TEMP:
        u8g2.drawFrame(cx - 5, cy - 16, 10, 26);
        u8g2.drawDisc(cx, cy + 10, 9);
        u8g2.setDrawColor(0);
        u8g2.drawDisc(cx, cy + 10, 6);
        u8g2.drawLine(cx, cy + 7, cx, cy - 12);
        u8g2.setDrawColor(1);
        u8g2.drawDisc(cx, cy + 10, 3);
        u8g2.drawLine(cx, cy + 10, cx, cy - 4);
        u8g2.drawLine(cx + 6, cy - 8, cx + 10, cy - 8);
        u8g2.drawLine(cx + 6, cy - 2, cx + 10, cy - 2);
        break;
    case ICON_ENGINE:
        u8g2.drawBox(cx - 14, cy - 4, 28, 18);
        u8g2.drawBox(cx - 10, cy - 12, 8, 8);
        u8g2.drawBox(cx + 2, cy - 12, 8, 8);
        u8g2.drawDisc(cx - 16, cy + 6, 5);
        u8g2.drawDisc(cx + 16, cy + 6, 5);
        u8g2.drawLine(cx - 16, cy + 11, cx + 16, cy + 11);
        break;
    case ICON_TIMER:
        u8g2.drawCircle(cx, cy + 2, 16);
        u8g2.drawBox(cx - 4, cy - 18, 8, 4);
        u8g2.drawLine(cx + 11, cy - 9, cx + 16, cy - 14);
        u8g2.drawLine(cx, cy + 2, cx, cy - 10);
        break;
    case ICON_BLE:
        u8g2.drawLine(cx, cy - 16, cx, cy + 16);
        u8g2.drawLine(cx, cy - 16, cx + 10, cy - 6);
        u8g2.drawLine(cx + 10, cy - 6, cx - 10, cy + 6);
        u8g2.drawLine(cx, cy + 16, cx + 10, cy + 6);
        u8g2.drawLine(cx + 10, cy + 6, cx - 10, cy - 6);
        break;
    case ICON_DASH:
        u8g2.drawFrame(cx - 16, cy - 16, 14, 14);
        u8g2.drawFrame(cx + 2, cy - 16, 14, 14);
        u8g2.drawFrame(cx - 16, cy + 2, 14, 14);
        u8g2.drawFrame(cx + 2, cy + 2, 14, 14);
        u8g2.drawBox(cx - 14, cy + 4, 10, 10);
        break;
    case ICON_SLIDERS:
        u8g2.drawLine(cx - 14, cy - 8, cx + 14, cy - 8);
        u8g2.drawBox(cx - 8, cy - 14, 6, 12);
        u8g2.drawLine(cx - 14, cy + 8, cx + 14, cy + 8);
        u8g2.drawBox(cx + 2, cy + 2, 6, 12);
        break;
    case ICON_AIR:
        u8g2.drawFrame(cx - 12, cy - 14, 24, 28);
        for (int i = -10; i <= 10; i += 5)
            u8g2.drawLine(cx - 12, cy + i, cx + 12, cy + i);
        break;
    case ICON_UPDATE:                           // Icône nuage + flèche de téléchargement
        u8g2.drawLine(cx, cy - 12, cx, cy + 8); // Flèche centre
        u8g2.drawTriangle(cx, cy + 12, cx - 6, cy + 4, cx + 6, cy + 4);
        u8g2.drawFrame(cx - 14, cy + 14, 28, 4); // Boîte
        break;
    }
}

void draw_StatusBar(String title)
{
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringLeft(0, 8, title);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringRight(128, 8, String(screenIndex + 1) + "/" + String(screenNumbers));
    u8g2.drawLine(0, 10, 128, 10);
}

void drawMenuScreen()
{
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 8, String(menuCursor + 1) + "/" + String(menuSize));

    u8g2.drawFrame(124, 0, 4, 46);
    int scrollHeight = max(6, 46 / menuSize);
    int scrollY = 0;
    if (menuSize > 1)
        scrollY = (menuCursor * (46 - scrollHeight)) / (menuSize - 1);
    u8g2.drawBox(124, scrollY, 4, scrollHeight);

    int iconX = 61, iconY = 28;
    drawVectorIcon(iconX, iconY, currentMenu[menuCursor].iconType);

    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 48, 128, 16);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawStringCenter(61, currentMenu[menuCursor].text);
    u8g2.setDrawColor(1);
}

void drawEditScreen(String title, String valueStr, float progress)
{
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(14, title);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(36, valueStr);

    u8g2.drawFrame(14, 42, 100, 6);
    int fillWidth = progress * 96;
    fillWidth = constrain(fillWidth, 0, 96);
    u8g2.drawBox(16, 44, fillWidth, 2);

    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 52, 128, 12);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(60, "U/D: Edit | OK: Save");
    u8g2.setDrawColor(1);
}

void drawConnectingScreen()
{
    u8g2.setClipWindow(0, 0, 128, UI_BASE_Y);
    u8g2.drawXBM(0, LOGO_OFFSET_Y, 128, 64, epd_bitmap_logo_3008);
    u8g2.setMaxClipWindow();

    u8g2.setDrawColor(0);
    u8g2.drawBox(0, UI_BASE_Y, 128, 64 - UI_BASE_Y);
    u8g2.setDrawColor(1);
    u8g2.drawLine(0, UI_BASE_Y, 128, UI_BASE_Y);

    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(UI_TEXT_Y, bleStatusStr);

    u8g2.drawFrame(4, UI_BAR_Y, 120, 6);

    if (bleConnected)
    {
        // Init ELM327 en cours : la barre se remplit selon l'étape atteinte.
        int fill = (elmInitStep / 5.0) * 116;
        if (fill > 0)
            u8g2.drawBox(6, UI_BAR_Y + 2, fill, 2);
    }
    else
    {
        // Scan en cours : un petit bloc fait des allers-retours dans la barre.
        int width = 20;
        int max_x = 116 - width;
        int pos = (millis() / 15) % (max_x * 2);
        int xOffset = (pos < max_x) ? pos : (max_x * 2) - pos;
        u8g2.drawBox(6 + xOffset, UI_BAR_Y + 2, width, 2);
    }
}

void drawConfigScreen()
{
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 14);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(10, "PORTAL CONFIG");
    u8g2.setDrawColor(1);

    int cx = 14, cy = 30;
    u8g2.drawBox(cx - 2, cy + 4, 4, 4);
    u8g2.drawFrame(cx - 6, cy, 12, 2);
    u8g2.drawFrame(cx - 10, cy - 4, 20, 2);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(28, 26, "SSID: " + String(configPortalSsid));
    drawStringLeft(28, 38, "PASS: 12345678");

    u8g2.drawLine(10, 46, 118, 46);

    u8g2.setFont(u8g2_font_helvB08_tr);
    drawStringCenter(58, "http://192.168.4.1");
}

void drawOTAScreen()
{
    u8g2.setFont(u8g2_font_helvB12_tr);
    drawStringCenter(16, "UPDATING...");

    u8g2.drawFrame(14, 30, 100, 10);
    int fill = ota_progress * 96;
    if (fill > 0)
        u8g2.drawBox(16, 32, fill, 6);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringCenter(52, String((int)(ota_progress * 100)) + " %");
    drawStringCenter(62, "Do not power off !");
}

void drawAutoUpdateStatusScreen(String msg)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tr);
    drawStringCenter(20, "AUTO UPDATE");
    u8g2.drawLine(0, 26, 128, 26);
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringCenter(44, msg);
    u8g2.sendBuffer();
}

// ---- Graphe historique (aire) ----
#define AREA_CHART_HISTORY 94
struct AreaChartData
{
    double values[AREA_CHART_HISTORY];
    uint8_t currentIndex;
    bool initialized; // devient vrai une fois le buffer circulaire rempli une première fois
};
static AreaChartData turboHistory = {{0}, 0, false}, loadHistory = {{0}, 0, false}, coolantHistory = {{0}, 0, false}, iatHistory = {{0}, 0, false};

static String alignSign(String value) { return (!value.startsWith("-")) ? " " + value : value; }
static String formatDecimal(double value, uint8_t decimals)
{
    String result = (value < 0) ? "-" : "";
    value = abs(value);
    result += String((int)value) + ".";
    double decPart = value - (int)value;
    for (int i = 0; i < decimals; i++)
    {
        decPart *= 10;
        int digit = (int)decPart;
        result += String(digit);
        decPart -= digit;
    }
    return result;
}

static void draw_InfoText(String title, double value, String unit)
{
    draw_StatusBar(title);
    u8g2.setFont(u8g2_font_helvB18_tr);
    String valStr = (value == (int)value) ? String((int)value) : String(value, 1);
    drawStringCenter(48, valStr + " " + unit);
}

static void draw_AreaChartWithHistory(AreaChartData &history, double newValue, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    double valToStore = constrain(newValue, minValue, maxValue);
    history.values[history.currentIndex] = valToStore;
    history.currentIndex = (history.currentIndex + 1) % AREA_CHART_HISTORY;
    if (history.currentIndex == 0)
        history.initialized = true;

    draw_StatusBar(label);
    int chartX = 32, chartY = 16, chartWidth = AREA_CHART_HISTORY, chartHeight = 44, baseY = chartY + chartHeight;
    u8g2.drawFrame(chartX, chartY, chartWidth, chartHeight);

    double range = maxValue - minValue;
    for (int i = 0; i < chartWidth; i++)
    {
        int historyIdx = (history.currentIndex + i) % AREA_CHART_HISTORY;
        if (!history.initialized && i >= history.currentIndex)
            break;
        double val = constrain(history.values[historyIdx], minValue, maxValue);
        int pixelHeight = (int)(chartHeight * ((val - minValue) / range));
        u8g2.drawLine(chartX + i, baseY, chartX + i, baseY - pixelHeight);
    }

    if (targetValue > -999.0)
    {
        double t_val = constrain(targetValue, minValue, maxValue);
        int ty = baseY - (int)(chartHeight * ((t_val - minValue) / range));
        for (int tx = chartX; tx < chartX + chartWidth; tx += 4)
            u8g2.drawPixel(tx, ty);
    }

    int alignBorderX = chartX - 2;
    u8g2.setFont(u8g2_font_helvR08_tr);
    drawStringRight(alignBorderX, chartY + 8, alignSign(formatDecimal(maxValue, 1)));
    drawStringRight(alignBorderX, baseY, alignSign(formatDecimal(minValue, 1)));
    drawStringRight(alignBorderX, chartY + (chartHeight / 2) + 4, alignSign(formatDecimal(newValue, 1)));
}

static void draw_LinearGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    draw_StatusBar(label);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(36, String(value, 1) + " " + unit);

    int barX = 4, barY = 42, barW = 120, barH = 12;
    u8g2.drawFrame(barX, barY, barW, barH);

    float val = constrain(value, minValue, maxValue);
    int segments = 23;
    int activeSegs = (int)(((val - minValue) / (maxValue - minValue)) * segments);
    for (int i = 0; i < activeSegs; i++)
        u8g2.drawBox(barX + 3 + (i * 5), barY + 3, 4, barH - 6);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 62, String(minValue, 1));
    drawStringRight(128, 62, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        float t_val = constrain(targetValue, minValue, maxValue);
        int t_x = barX + 2 + (int)(((t_val - minValue) / (maxValue - minValue)) * (barW - 4));
        u8g2.drawTriangle(t_x, barY - 1, t_x - 3, barY - 5, t_x + 3, barY - 5);
    }
}

static void draw_RoundGauge(double value, double minValue, double maxValue, String label, String unit, double targetValue = -1000.0)
{
    draw_StatusBar(label);

    int cx = 64, cy = 64, r = 50;

    for (int i = 0; i <= 10; i++)
    {
        float a = PI - (i * PI / 10.0);
        int r_inner = r - ((i % 5 == 0) ? 6 : 3);
        u8g2.drawLine(cx + cos(a) * r, cy - sin(a) * r, cx + cos(a) * r_inner, cy - sin(a) * r_inner);
    }

    float val = constrain(value, minValue, maxValue);
    float angle = PI - ((val - minValue) / (maxValue - minValue)) * PI;
    int nx = cx + cos(angle) * (r - 8);
    int ny = cy - sin(angle) * (r - 8);
    u8g2.drawTriangle(nx, ny, cx + cos(angle + 0.15) * 6, cy - sin(angle + 0.15) * 6, cx + cos(angle - 0.15) * 6, cy - sin(angle - 0.15) * 6);

    u8g2.setDrawColor(0);
    u8g2.drawBox(20, 46, 88, 20);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_helvB18_tr);
    drawStringCenter(62, String(value, 1) + " " + unit);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStringLeft(0, 62, String(minValue, 1));
    drawStringRight(128, 62, String(maxValue, 1));

    if (targetValue > -999.0)
    {
        float t_angle = PI - ((constrain(targetValue, minValue, maxValue) - minValue) / (maxValue - minValue)) * PI;
        u8g2.drawCircle(cx + cos(t_angle) * (r + 2), cy - sin(t_angle) * (r + 2), 3);
    }
}

void draw_GaugeScreen(uint8_t index)
{
    switch (index)
    {
    case 0:
        draw_StatusBar("AIR SENSORS");
        u8g2.setFont(u8g2_font_helvB14_tr);
        drawStringCenter(34, "MAP : " + String((int)mapPressure) + " kPa");
        drawStringCenter(56, "MAF : " + String(mafPressure, 1) + " g/s");
        break;
    case 1:
        if (BOOST_SCREEN == 0)
            draw_InfoText("Boost", turboPressureState, "Bar");
        else if (BOOST_SCREEN == 1)
            draw_AreaChartWithHistory(turboHistory, turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else if (BOOST_SCREEN == 2)
            draw_RoundGauge(turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        else
            draw_LinearGauge(turboPressureState, TURBO_MIN_BAR, TURBO_MAX_BAR, "Boost", "Bar", targetBoost);
        break;
    case 2:
        if (IAT_SCREEN == 0)
            draw_InfoText("Temp IAT", intakeTemp, "C");
        else if (IAT_SCREEN == 1)
            draw_AreaChartWithHistory(iatHistory, intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        else if (IAT_SCREEN == 2)
            draw_RoundGauge(intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        else
            draw_LinearGauge(intakeTemp, -20.0, 60.0, "Temp IAT", "C");
        break;
    case 3:
        if (ENGLOAD_SCREEN == 0)
            draw_InfoText("Charge", engineLoad, "%");
        else if (ENGLOAD_SCREEN == 1)
            draw_AreaChartWithHistory(loadHistory, engineLoad, 0, 100, "Charge", "%");
        else if (ENGLOAD_SCREEN == 2)
            draw_RoundGauge(engineLoad, 0, 100, "Charge", "%");
        else
            draw_LinearGauge(engineLoad, 0, 100, "Charge", "%");
        break;
    case 4:
        if (COOLANT_SCREEN == 0)
            draw_InfoText("Temp LdR", coolantTemp, "C");
        else if (COOLANT_SCREEN == 1)
            draw_AreaChartWithHistory(coolantHistory, coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        else if (COOLANT_SCREEN == 2)
            draw_RoundGauge(coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        else
            draw_LinearGauge(coolantTemp, 40.0, 120.0, "Temp LdR", "C");
        break;
    case 5:
        draw_StatusBar("DASHBOARD");
        u8g2.setFont(u8g2_font_helvB08_tr);
        drawStringLeft(0, 32, "BST: " + String(turboPressureState, 1) + "b");
        drawStringLeft(68, 32, "IAT: " + String(intakeTemp, 0) + "C");
        drawStringLeft(0, 56, "LDR: " + String(coolantTemp, 0) + "C");
        drawStringLeft(68, 56, "RPM: " + String((int)dashRPM));
        break;
    case 6:
        draw_StatusBar("CHRONO");
        u8g2.setFont(u8g2_font_helvB10_tr);
        drawStringCenter(24, "0 - " + String(TARGET_SPEED) + " km/h");
        u8g2.setFont(u8g2_font_helvB14_tr);
        if (timerRunning)
            drawStringCenter(46, String((millis() - speedTimerStart) / 1000.0, 2) + " s");
        else if (lastTimerValue > 0)
            drawStringCenter(46, String(lastTimerValue, 2) + " s");
        else
        {
            u8g2.setFont(u8g2_font_helvB14_tr);
            drawStringCenter(46, "READY");
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        drawStringCenter(62, "Speed: " + String((int)currentSpeed) + " km/h");
        break;
    case 7:
        draw_InfoText("Speed", currentSpeed, "km/h");
        break;
    case 8:
        draw_StatusBar("OBD BLE");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(0, 24);
        u8g2.print("Status: " + bleStatusStr);
        u8g2.setCursor(0, 34);
        u8g2.print("Packets: " + String(packetsReceived));
        u8g2.setCursor(0, 44);
        u8g2.print("Init Step: " + String(elmInitStep) + "/5");
        u8g2.setCursor(0, 54);
        u8g2.print("Buf: " + elmBuffer.substring(0, 16));
        break;
    }
}

// ---- Transition (glissement) entre deux écrans de jauges ----
bool isTransitioning = false;
int slideOffset = 0;
int slideDirection = 1;
static uint8_t oldScreenBuffer[1024];

void startScreenTransition()
{
    memcpy(oldScreenBuffer, u8g2.getBufferPtr(), 1024);
    isTransitioning = true;
    slideOffset = 0;
}

void updateScreenTransition()
{
    slideOffset += 24;
    if (slideOffset >= 128)
    {
        isTransitioning = false;
        return;
    }

    // Le buffer u8g2 est organisé en 8 pages de 128 colonnes ; on recompose
    // chaque page en juxtaposant l'ancien et le nouvel écran, décalés de
    // slideOffset pixels, pour simuler un glissement horizontal.
    uint8_t temp[1024];
    uint8_t *currentScreen = u8g2.getBufferPtr();
    for (int p = 0; p < 8; p++)
    {
        int pageStart = p * 128;
        if (slideDirection == 1)
        {
            memcpy(&temp[pageStart], &oldScreenBuffer[pageStart + slideOffset], 128 - slideOffset);
            memcpy(&temp[pageStart + 128 - slideOffset], &currentScreen[pageStart], slideOffset);
        }
        else
        {
            memcpy(&temp[pageStart], &currentScreen[pageStart + 128 - slideOffset], slideOffset);
            memcpy(&temp[pageStart + slideOffset], &oldScreenBuffer[pageStart], 128 - slideOffset);
        }
    }
    memcpy(currentScreen, temp, 1024);
}
