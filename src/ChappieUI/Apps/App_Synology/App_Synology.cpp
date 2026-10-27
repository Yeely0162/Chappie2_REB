/**
 * @file App_Synology.cpp
 * @brief Synology monitor: app lifecycle only.
 *
 * The pages live in SyUi.cpp, the DSM session and the FreeRTOS worker in
 * SyNet.cpp. This file wires the launcher lifecycle to them:
 *
 *   onCreate   build the five screens, start the worker, start the UI timer
 *   onLoop     empty, everything is timer or worker driven
 *   onDestroy  stop the timer, stop the worker (it logs out), free the UI
 */
#if 1
#include "App_Synology.h"
#include "SyCommon.h"

#include "../../../ChappieBsp/Chappie.h"

LV_IMG_DECLARE(ui_img_icon_Synology_png);

static std::string app_name = "Synology";
static CHAPPIE *device = nullptr;
static lv_timer_t *s_uiTimer = nullptr;

/* 250 ms: enough for the clock and status dot, cheap enough for LVGL. The
 * real data cadence is decided by the worker (3 s on the home page). */
#define SY_UI_TICK_MS 250

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    Sy::UiRefresh();
}

/* B key: close the keyboard, else return to the home page, else leave the app. */
static bool synology_back_handler()
{
    return Sy::UiHandleBack();
}

namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     *
     * @return std::string
     */
    std::string App_Synology_appName()
    {
        return app_name;
    }

    /**
     * @brief Return the App Icon laucnher, NULL for default
     *
     * @return void*
     */
    void *App_Synology_appIcon()
    {
        return (void *)&ui_img_icon_Synology_png;
    }

    /**
     * @brief Called when App is on create
     *
     */
    void App_Synology_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Synology_appName().c_str());

        if (!device) {
            UI_LOG("[Synology] no BSP pointer, abort\n");
            return;
        }

        Sy::UiBuild(device);

        if (!s_uiTimer)
            s_uiTimer = lv_timer_create(ui_timer_cb, SY_UI_TICK_MS, nullptr);

        Sy::NetStart(device);

        App::SetAppBackHandler(synology_back_handler);
    }

    /**
     * @brief Called repeatedly, end this function ASAP!
     *
     */
    void App_Synology_onLoop()
    {
        /* Everything is driven by the LVGL timer and the worker task. */
    }

    /**
     * @brief Called when App is about to be destroy
     *
     */
    void App_Synology_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Synology_appName().c_str());

        App::SetAppBackHandler(nullptr);

        if (s_uiTimer) {
            lv_timer_del(s_uiTimer);
            s_uiTimer = nullptr;
        }

        /* Ask the worker to finish (it logs out first), then free the UI.
         * The worker never touches LVGL objects, so it is safe to delete the
         * screens even if the last request is still winding down. */
        Sy::NetStop();
        Sy::UiDestroy();

        UI_LOG("[Synology] destroy done\n");
    }

    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     *
     */
    void App_Synology_getBsp(void *bsp)
    {
        device = (CHAPPIE *)bsp;
    }

}
#endif
