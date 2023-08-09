#if 1
#include "App_LEDS.h"
#include "LEDS.h"

static std::string app_name = "LEDS";
static CHAPPIE* device;

LV_IMG_DECLARE(ui_img_icon_leds_png);
namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_LEDS_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_LEDS_appIcon()
    {
        return (void*) &ui_img_icon_leds_png;
        // return NULL;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_LEDS_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_LEDS_appName().c_str());
        LEDSPage();
        /*Create an Arc*/

    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_LEDS_onLoop()
    {
        LEDS_Loop();
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_LEDS_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_LEDS_appName().c_str());
        FastLED.showColor(CRGB::Black);
        LEDS_DeInit();
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_LEDS_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
