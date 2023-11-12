#if 1
#include "App_EAJ.h"
#include "devices.h"

static std::string app_name = "EAJ";
static CHAPPIE* device;

LV_IMG_DECLARE(ui_img_icon_EAJ_png);
namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_EAJ_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_EAJ_appIcon()
    {
        return (void*) &ui_img_icon_EAJ_png;
        // return NULL;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_EAJ_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_EAJ_appName().c_str());
        EAJPage();
        /*Create an Arc*/

    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_EAJ_onLoop()
    {
        EAJ_Loop();
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_EAJ_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_EAJ_appName().c_str());
        FastLED.showColor(CRGB::Black);
        //EAJ_DeInit();
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_EAJ_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
