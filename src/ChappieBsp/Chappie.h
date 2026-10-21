/**
 * @file Chappie.h
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-10
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "EmmaBSP/Emma.h"
#include "CTP/ChappieCTP.hpp"
#include "IMU/ChappieIMU.hpp"
#include "MAG/ChappieMAG.hpp"
#include "Power/ChappiePower.hpp"
#include "SD/ChappieSD.hpp"
#include "ENV/ChappieENV.hpp"
#include "Utility/BM8563/I2C_BM8563.h"
#include "Lvgl/ChappieLvgl.hpp"
//  Network
#include "ChappieNet/WiFi/ConfigWiFi.h"
#include "ChappieNet/BLE/ConfigBLE.h"
//Resoure 
#include "ChappieUI/Resource/ResourcePool.h"
class CHAPPIE : public EMMA {
    private:
        
    public:
        const std::string Logo = R"(
  ___ _  _  __  ____ ____ __ ____ 
 / __) )( \/ _\(  _ (  _ (  |  __)
( (__) __ (    \) __/) __/)( ) _) 
 \___)_)(_|_/\_(__) (__) (__|____)
 
)";
        const std::string Yeely = R"(
 _  _  ____  ____  __   _  _ 
( \/ )( ___)( ___)(  ) ( \/ )
 \  /  )__)  )__)  )(__ \  / 
 (__) (____)(____)(____)(__) 
 
)";
        ChappieCTP Tp;

        ChappieIMU Imu;

        ChappieMAG Mag;

        ChappieSD Sd;

        ChappiePower Pow;

        ChappieLvgl lvgl;

        I2C_BM8563 Rtc = I2C_BM8563(I2C_BM8563_DEFAULT_ADDRESS, Wire);

        ChappieENV Env;

        // Net --WiFi --Bluetooth
        ConfigWiFi Wf;
        ConfigBLE Ble;

        /**
         * @brief Init BMM150 on demand
         *
         * Only the App that needs the magnetometer should call this. The
         * driver is not touched during boot, so an unused sensor produces no
         * I2C traffic and no serial output.
         *
         * @return true when the sensor answered and was configured
         */
        inline bool initMag()
        {
            if (Mag.isReady())
                return true;
            return Mag.init(&Wire);
        }

        void init();
    
};
