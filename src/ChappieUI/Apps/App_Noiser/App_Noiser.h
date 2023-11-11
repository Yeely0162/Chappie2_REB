#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"
/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_Noiser_appName();
    void* App_Noiser_appIcon();
    void App_Noiser_onCreate();
    void App_Noiser_onLoop();
    void App_Noiser_onDestroy();
    void App_Noiser_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(Noiser);
}

#endif
