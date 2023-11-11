/**
 * @file ChappieENV.hpp
 * @author Forairaaaaa
 * @brief Thanks to https://github.com/adafruit/Adafruit_BMP280_Library
 * @version 0.1
 * @date 2023-03-14
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "../lib/Sensor BMP280/Seeed_BMP280.h"

class ChappieENV : public BMP280 {
    private:
        float _offset;

    public:
        using BMP280::BMP280;

        inline bool init(float pressureOffset = 0)
        {
            setPressureOffset(pressureOffset);

            bool val = BMP280::init();
            //printf("BMP280 Init: ");
            //printf(val ? "True\n" : "False\n");
            return val;

        }

        /**
         * @brief Set pressure offset for value adjust
         * 
         * @param offset 
         */
        inline void setPressureOffset(float offset) { _offset = offset; }

        /* Rewrite functions with offset */
        /*!
        * Reads the barometric pressure from the device.
        * @return Barometric pressure in Pa.
        */
        inline float readPressure(void)
        {
            return BMP280::getPressure() + _offset;
        }
        
        /*!
        * @brief Calculates the approximate altitude using barometric pressure and the
        * supplied sea level hPa as a reference.
        * @param seaLevelhPa
        *        The current hPa at sea level.
        * @return The approximate altitude above sea level in meters.
        */
        inline float readAltitude(float seaLevelhPa = 1013.25)
        {
            return BMP280::calcAltitude(seaLevelhPa + (_offset / 100));
        }

};

