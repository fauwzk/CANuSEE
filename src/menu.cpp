#include "menu.h"
#include "display.h"
#include "config.h"

MenuItem currentMenu[MAX_MENU_ITEMS];
int menuSize = 0, menuCursor = 0;

void buildMenu()
{
    menuSize = 0;

    if (screenIndex >= 1 && screenIndex <= 4)
        currentMenu[menuSize++] = {"Gauge Style ->", ACT_OPEN_STYLE_MENU, ICON_GAUGE};

    currentMenu[menuSize++] = {"Brightness", ACT_EDIT_BRIGHTNESS, ICON_SUN};

    if (screenIndex == 1)
    {
        currentMenu[menuSize++] = {"Turbo Min", ACT_EDIT_MIN, ICON_SLIDERS};
        currentMenu[menuSize++] = {"Turbo Max", ACT_EDIT_MAX, ICON_SLIDERS};
    }
    if (screenIndex == 6)
        currentMenu[menuSize++] = {"Target Speed", ACT_EDIT_SPEED, ICON_TIMER};

    currentMenu[menuSize++] = {"MAP/MAF", ACT_GO_SCREEN_0 + 0, ICON_AIR};
    currentMenu[menuSize++] = {"Boost", ACT_GO_SCREEN_0 + 1, ICON_TURBO};
    currentMenu[menuSize++] = {"IAT Temp", ACT_GO_SCREEN_0 + 2, ICON_TEMP};
    currentMenu[menuSize++] = {"Engine Load", ACT_GO_SCREEN_0 + 3, ICON_ENGINE};
    currentMenu[menuSize++] = {"Coolant", ACT_GO_SCREEN_0 + 4, ICON_TEMP};
    currentMenu[menuSize++] = {"Dashboard", ACT_GO_SCREEN_0 + 5, ICON_DASH};
    currentMenu[menuSize++] = {"0-100 Timer", ACT_GO_SCREEN_0 + 6, ICON_TIMER};
    currentMenu[menuSize++] = {"Speedometer", ACT_GO_SCREEN_0 + 7, ICON_GAUGE};
    currentMenu[menuSize++] = {"BLE Status", ACT_GO_SCREEN_0 + 8, ICON_BLE};

    currentMenu[menuSize++] = {"Cloud Update", ACT_AUTO_UPDATE, ICON_UPDATE};
    currentMenu[menuSize++] = {"Mode Config", ACT_ENTER_CONFIG, ICON_GEAR};
    currentMenu[menuSize++] = {"Exit Menu", ACT_CLOSE, ICON_EXIT};
    menuCursor = 0;
}

void buildStyleMenu()
{
    menuSize = 0;
    currentMenu[menuSize++] = {"Text", ACT_SET_STYLE_TEXT, ICON_AIR};
    currentMenu[menuSize++] = {"Graph", ACT_SET_STYLE_GRAPH, ICON_TURBO};
    currentMenu[menuSize++] = {"Dial", ACT_SET_STYLE_DIAL, ICON_GAUGE};
    currentMenu[menuSize++] = {"Bar", ACT_SET_STYLE_BAR, ICON_SLIDERS};
    currentMenu[menuSize++] = {"<- Back", ACT_BACK_TO_MENU, ICON_EXIT};

    int currentType = 0;
    if (screenIndex == 1)
        currentType = BOOST_SCREEN;
    else if (screenIndex == 2)
        currentType = IAT_SCREEN;
    else if (screenIndex == 3)
        currentType = ENGLOAD_SCREEN;
    else if (screenIndex == 4)
        currentType = COOLANT_SCREEN;

    menuCursor = constrain(currentType, 0, 3);
}
