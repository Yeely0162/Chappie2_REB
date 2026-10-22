/**
 * @file SettingsPages.h
 * @brief Settings pages. Each builder owns one full-screen root object; the
 *        app swaps roots instead of re-creating the whole settings tree.
 */
#pragma once
#include "lvgl.h"
#include "ChappieBsp/Chappie.h"

namespace SettingsPages {

enum PageId : uint8_t {
    Root = 0,
    Wifi,
    Bluetooth,
    Time,
    Display,
    Battery,
    Keys,
    Test,
    About,
    PageCount
};

typedef void (*NavHandler)(uint8_t page);
typedef void (*BackHandler)();

void setNavHandler(NavHandler handler);
void setBackHandler(BackHandler handler);

/**
 * @brief Build the settings home page.
 */
lv_obj_t *buildRoot(lv_obj_t *parent, CHAPPIE *dev);

/**
 * @brief Build one sub-page (header + body).
 */
lv_obj_t *buildPage(PageId page, lv_obj_t *parent, CHAPPIE *dev);

/**
 * @brief Tell the module which page is currently visible (refresh dispatch).
 */
void setCurrent(PageId page);

/**
 * @brief Refresh dynamic values of the active page. Runs in the LVGL task.
 */
void refresh();

/**
 * @brief Handle the hardware B key. Closes a modal, exits a full-screen test,
 *        or navigates from a sub-page to the root. Returns false when the app
 *        should be left (launcher default).
 */
bool handleBack();

/**
 * @brief Close modals and drop cached widget pointers (app exit).
 */
void destroy();

} // namespace SettingsPages
