/**
 * @file App_Settings.cpp
 * @brief Settings centre lifecycle and navigation.
 *
 * The page builders live in SettingsPages.cpp and the visual primitives in
 * SettingsTheme.cpp. This file only wires the app lifecycle to them.
 */
#include "App_Settings.h"
#include "SettingsPages.h"
#include "SettingsTheme.h"
#include "ChappieBsp/Chappie.h"

LV_IMG_DECLARE(ui_img_icon_setting_png);

static const char *app_name = "Settings";
static CHAPPIE *device = nullptr;

static lv_obj_t *s_current = nullptr;
static lv_timer_t *s_timer = nullptr;
static SettingsPages::PageId s_page = SettingsPages::Root;

static void show_page(SettingsPages::PageId page)
{
    if (!device)
        return;

    if (s_current) {
        lv_obj_del(s_current);
        s_current = nullptr;
    }

    s_page = page;
    SettingsPages::setCurrent(page);
    if (page == SettingsPages::Root)
        s_current = SettingsPages::buildRoot(lv_scr_act(), device);
    else
        s_current = SettingsPages::buildPage(page, lv_scr_act(), device);
}

static void nav_cb(uint8_t page)
{
    show_page((SettingsPages::PageId)page);
}

static void back_cb()
{
    show_page(SettingsPages::Root);
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    SettingsPages::refresh();
}

/* B: close modal -> leave full-screen test -> back to root -> leave app. */
static bool settings_back_handler()
{
    return SettingsPages::handleBack();
}

namespace App {

    std::string App_Settings_appName()
    {
        return app_name;
    }

    void *App_Settings_appIcon()
    {
        return (void *)&ui_img_icon_setting_png;
    }

    void App_Settings_onCreate()
    {
        UI_LOG("\n[%s] onCreate\n", App_Settings_appName().c_str());

        if (!device)
            return;

        STheme::styleScreen(lv_scr_act());
        SettingsPages::setNavHandler(nav_cb);
        SettingsPages::setBackHandler(back_cb);

        show_page(SettingsPages::Root);

        App::SetAppBackHandler(settings_back_handler);
        s_timer = lv_timer_create(refresh_timer_cb, 500, nullptr);
    }

    void App_Settings_onLoop()
    {
        /* All work is driven by the refresh timer; keep this empty so the
         * launcher frame is never delayed. */
    }

    void App_Settings_onDestroy()
    {
        UI_LOG("\n[%s] onDestroy\n", App_Settings_appName().c_str());

        App::SetAppBackHandler(nullptr);

        if (s_timer) {
            lv_timer_del(s_timer);
            s_timer = nullptr;
        }

        SettingsPages::destroy();

        if (s_current) {
            lv_obj_del(s_current);
            s_current = nullptr;
        }
        s_page = SettingsPages::Root;
    }

    void App_Settings_getBsp(void *bsp)
    {
        device = (CHAPPIE *)bsp;
    }

}
