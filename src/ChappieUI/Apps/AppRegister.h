/**
 * @file AppRegister.h
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-15
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "AppTypedef.h"


/**
 *  ------------------------------------------- How to Add an App -------------------------------------------
 * 
 *  1. Copy the App_Template folder, rename it like App_MyApp, and paste it next to App_Template.
 *      into App_MyApp folder, rename the cpp and h file, e.g. App_MyApp.cpp
 *      into App_MyApp.cpp and  App_MyApp.h, change all the "Template" to "MyApp"
 * 
 *  2. Include your App's header file in 2)
 *      e.g. #include "App_MyApp/App_MyApp.h"
 * 
 *  3. Log your App into AppRegister in 3)
 *      e.g. App_Login(MyApp),
 * 
 * ----------------------------------------------------------------------------------------------------------
 */


/**
 * @brief 2) Include your App's header file
 * 
 */
#include "App_StupidSnake/App_StupidSnake.h"
#include "App_Watch/App_Watch.h"
#include "App_Settings/App_Settings.h"
#include "App_Dino/App_Dino.h"
#include "App_FFT/App_FFT.h"
#include "App_ESPNOW/App_ESPNOW.h"
#include "App_LEDS/App_LEDS.h"
#include "App_Noiser/App_Noiser.h"
#include "App_Menu/App_Menu.h"
#include "App_EvilAppleJuice/App_EAJ.h"
/* Header files locator */
/* Don't remove this, or python script's auto login will be failed */


namespace App {
    
    static AppRegister_t Register[] = {
        

        /**
         * @brief 3) Log your App into AppRegister here
         * 
         */
        App_Login(Watch),
        App_Login(EAJ),
		App_Login(StupidSnake),
        App_Login(Dino),
        App_Login(LEDS),
        App_Login(Noiser),
		App_Login(FFT),
		App_Login(ESPNOW),
        App_Login(Menu),
        App_Login(Settings),
		/* Login locator */
        /* Don't remove this, or python script's auto login will be failed */


    };
}
