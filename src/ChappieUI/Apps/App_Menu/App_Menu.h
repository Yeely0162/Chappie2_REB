#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_Menu_appName();
    void* App_Menu_appIcon();
    void App_Menu_onCreate();
    void App_Menu_onLoop();
    void App_Menu_onDestroy();
    void App_Menu_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(Menu);
}

#endif
