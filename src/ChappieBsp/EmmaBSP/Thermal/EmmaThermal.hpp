/**
 * @file EmmaThermal.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-02
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "../../EmmaConfig.h"
#if EMMA_MODULE_Thermal
#include "Thermal/Thermal_Class.hpp"
#include <driver/gpio.h>


class EmmaThermal : public m5::Thermal_Class {
    private:
    public:
        EmmaThermal()
        {
            /* Setup pins */
            auto cfg = config();
            // cfg.pin_data_out = EMMA_Thermal_DATA_PIN;
            // cfg.pin_bck = EMMA_Thermal_BCLK_PIN;
            // cfg.pin_ws = EMMA_Thermal_LRCLK_PIN;
            // cfg.i2s_port = EMMA_Thermal_I2S_PORT;
            cfg.task_priority = 10;
            cfg.sample_rate = 48000;
            config(cfg);
            begin();

            enable();
        }
        ~EmmaThermal() { end(); }
        
        /* For hardware enable pin if exist */
        inline void enable() { setCtrlPin(1); }
        inline void disable() { setCtrlPin(0); }
        inline void setCtrlPin(uint8_t level)
        {
            // gpio_reset_pin((gpio_num_t)EMMA_Thermal_ENABLE_PIN);
            // gpio_set_direction((gpio_num_t)EMMA_Thermal_ENABLE_PIN, GPIO_MODE_OUTPUT_OD);
            // gpio_set_level((gpio_num_t)EMMA_Thermal_ENABLE_PIN, level);
        }
};

#endif
