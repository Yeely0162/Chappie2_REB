#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_Synology_appName();
    void* App_Synology_appIcon();
    void App_Synology_onCreate();
    void App_Synology_onLoop();
    void App_Synology_onDestroy();
    void App_Synology_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(Synology);
}

#endif
