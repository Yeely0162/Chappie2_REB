/**
 * @file ChappieI2C.hpp
 * @author Yeely
 * @brief Shared I2C transaction lock for the Chappie2 bus
 * @version 0.1
 * @date 2026-10-21
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* Created once in CHAPPIE::init() before the LVGL task and main loop start. */
extern SemaphoreHandle_t g_chappie_i2c_mutex;

/**
 * @brief Serialize one complete I2C transaction
 *
 * Wire's internal lock covers beginTransmission(), endTransmission() and
 * requestFrom() separately. A register read is a multi-call sequence, so two
 * tasks can interleave their calls and leave the bus in a bad state. This
 * guard covers the whole sequence.
 */
class ChappieI2CGuard {
    private:
        bool _locked;

    public:
        ChappieI2CGuard() : _locked(false)
        {
            if (g_chappie_i2c_mutex != NULL) {
                if (xSemaphoreTake(g_chappie_i2c_mutex, portMAX_DELAY) == pdTRUE)
                    _locked = true;
            }
        }

        ~ChappieI2CGuard()
        {
            if (_locked)
                xSemaphoreGive(g_chappie_i2c_mutex);
        }

        ChappieI2CGuard(const ChappieI2CGuard&) = delete;
        ChappieI2CGuard& operator=(const ChappieI2CGuard&) = delete;
};
