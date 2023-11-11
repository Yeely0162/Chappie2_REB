#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_EAJ_appName();
    void* App_EAJ_appIcon();
    void App_EAJ_onCreate();
    void App_EAJ_onLoop();
    void App_EAJ_onDestroy();
    void App_EAJ_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(EAJ);
}

#endif
