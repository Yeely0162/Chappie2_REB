#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

/**
 * @brief Create an App in name space 
 * 
 */
namespace App {

    std::string App_ChatGPT_appName();
    void* App_ChatGPT_appIcon();
    void App_ChatGPT_onCreate();
    void App_ChatGPT_onLoop();
    void App_ChatGPT_onDestroy();
    void App_ChatGPT_getBsp(void* bsp);

    /**
     * @brief Declace your App like this 
     * 
     */
    App_Declare(ChatGPT);
}

#endif
