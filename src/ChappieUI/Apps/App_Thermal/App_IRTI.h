#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_IRTI_appName();
    void* App_IRTI_appIcon();
    void App_IRTI_onCreate();
    void App_IRTI_onLoop();
    void App_IRTI_onDestroy();
    void App_IRTI_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(IRTI);
}

#endif
