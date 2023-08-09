#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_LEDS_appName();
    void* App_LEDS_appIcon();
    void App_LEDS_onCreate();
    void App_LEDS_onLoop();
    void App_LEDS_onDestroy();
    void App_LEDS_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(LEDS);
}

#endif
