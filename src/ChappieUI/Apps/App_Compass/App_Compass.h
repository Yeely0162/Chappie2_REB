#if 1
#pragma once
#include "../AppTypedef.h"
#include "../../ChappieUIConfigs.h"

namespace App {

    std::string App_Compass_appName();
    void* App_Compass_appIcon();
    void App_Compass_onCreate();
    void App_Compass_onLoop();
    void App_Compass_onDestroy();
    void App_Compass_getBsp(void* bsp);

    App_Declare(Compass);
}

#endif
