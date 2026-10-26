/**
 * @file ChappieCTP.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-10
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "Arduino.h"
#include "Wire.h"
#include "../Utility/ChappieI2C.hpp"

/* Configs */
#define CTP_DEV_ADDR            0x15
#define CTP_INT_PIN             21
#define CTP_INIT_TIMEOUT_MS     600

#define CTP_HORIZON
// #define CTP_PORTRAIT

#ifdef CTP_HORIZON
    #define CTP_REVERS_XPOS     0
    #define CTP_REVERS_YPOS     1
    #define CTP_REVERS_XY       1
#else
#ifdef CTP_PORTRAIT
    #define CTP_REVERS_XPOS     0
    #define CTP_REVERS_YPOS     1
    #define CTP_REVERS_XY       0
#endif
#endif

/* Use map for calibration, can solve some dead area problem, but lost precision */
#define USING_MAP_TO_CALIBRATE  0


class ChappieCTP {
    private:
        int _x_pos = -1;
        int _y_pos = -1;
        bool _enable = false;

        /* Touch IRQ state */
        volatile bool _irq_flag = false;
        bool _was_touched = false;
        uint32_t _last_touch_ms = 0;
        uint32_t _wake_start_ms = 0;

        /* I2C related */
        TwoWire* _wire = NULL;
        uint8_t _dev_addr = CTP_DEV_ADDR;

        static void IRAM_ATTR _touch_isr(void* arg)
        {
            ChappieCTP* self = (ChappieCTP*)arg;
            self->_irq_flag = true;
        }

        inline void _I2C_init(TwoWire * wire, uint8_t dev_addr)
        {
            _wire = wire;
            _dev_addr = dev_addr;
        }

        inline bool _I2C_checkDevAvl()
        {
            ChappieI2CGuard lock;

            _wire->beginTransmission(_dev_addr);
            return ( _wire->endTransmission() ? true : false);
        }

        inline void _I2C_write1Byte(uint8_t addr, uint8_t data)
        {
            ChappieI2CGuard lock;

            _wire->beginTransmission(_dev_addr);
            _wire->write(addr);
            _wire->write(data);
            if (_wire->endTransmission() != 0)
                printf("[CTP] write 0x%02X failed\n", (unsigned)addr);
        }

        inline void _log_read_error(uint8_t addr, const char* phase)
        {
            static uint32_t last_log_ms = 0;
            if ((millis() - last_log_ms) >= 1000) {
                printf("[CTP] reg 0x%02X %s failed task=%s\n",
                       (unsigned)addr, phase, pcTaskGetName(NULL));
                last_log_ms = millis();
            }
        }

        inline bool _I2C_readBuff(uint8_t addr, int size, uint8_t buff[])
        {
            ChappieI2CGuard lock;
            const char* phase = "transaction";

            for (int i = 0; i < size; i++)
                buff[i] = 0;

            for (int attempt = 0; attempt < 3; attempt++) {
                phase = "write";
                _wire->beginTransmission(_dev_addr);
                _wire->write(addr);
                if (_wire->endTransmission() != 0) {
                    if (attempt < 2) {
                        delay(10);
                        continue;
                    }
                    break;
                }

                phase = "request";
                if (_wire->requestFrom(_dev_addr, (size_t)size, true) != (size_t)size) {
                    if (attempt < 2) {
                        delay(10);
                        continue;
                    }
                    break;
                }

                phase = "read";
                bool ok = true;
                for (int i = 0; i < size; i++) {
                    int data = _wire->read();
                    if (data < 0) {
                        ok = false;
                        break;
                    }
                    buff[i] = (uint8_t)data;
                }
                if (ok)
                    return true;

                if (attempt < 2)
                    delay(10);
                else
                    break;
            }

            _log_read_error(addr, phase);
            return false;
        }

        inline bool _I2C_read8Bit(uint8_t addr, uint8_t& data)
        {
            return _I2C_readBuff(addr, 1, &data);
        }

        inline uint16_t _I2C_read12Bit(uint8_t addr)
        {
            uint8_t buff[2];
            _I2C_readBuff(addr, 2, buff);
            return (buff[0] << 4) + buff[1];
        }

        /* CTP init */
        inline void _init()
        {
            /* Reset coordinate */
            _reset_coor();
            _enable = true;
            _irq_flag = false;
            _was_touched = false;
            _last_touch_ms = 0;
            _wake_start_ms = 0;

            /* Setup INT PIN: the controller asserts it on touch */
            pinMode(CTP_INT_PIN, INPUT_PULLUP);
            attachInterruptArg(digitalPinToInterrupt(CTP_INT_PIN), _touch_isr, this, FALLING);
            _I2C_write1Byte(0xFA, 0x70);
            // _I2C_write1Byte(0xED, 0xC8);

            /* Keep the original auto sleep setting. A touch wakes the
             * controller and the IRQ state machine handles the read. */
            // _I2C_write1Byte(0xFE, 0xFF);
            _I2C_write1Byte(0xFE, 0x07);
            printf("[CTP] auto sleep shut down\n");
        }

        inline void _reset_coor()
        {
            _x_pos = -1;
            _y_pos = -1;
        }

        inline bool _update_coor()
        {
            // coor_x = _I2C_read12Bit(0x03);
            // coor_y = _I2C_read12Bit(0x05);

            uint8_t buff[4];
            if (!_I2C_readBuff(0x03, 4, buff))
                return false;
            // _x_pos = ((buff[0]&0x0F)<<8)|buff[1];
            // _y_pos = ((buff[2]&0x0F)<<8)|buff[3];

            #if CTP_REVERS_XY
                _y_pos = ((buff[0]&0x0F)<<8)|buff[1];
                _x_pos = ((buff[2]&0x0F)<<8)|buff[3];
                #if CTP_REVERS_XPOS
                    _x_pos = -(_x_pos ) + 280;
                #endif
                #if CTP_REVERS_YPOS
                    _y_pos = -(_y_pos ) + 240;
                #endif
            #else
                _x_pos = ((buff[0]&0x0F)<<8)|buff[1];
                _y_pos = ((buff[2]&0x0F)<<8)|buff[3];
            #endif

            /* A failed read used to look like 0x0FFF, do not pass it to LVGL */
            if ((_x_pos > 1000) || (_x_pos < -1000) ||
                (_y_pos > 1000) || (_y_pos < -1000)) {
                _reset_coor();
                return false;
            }

            return true;
        }


    public:
        /**
         * @brief Init toupac with wire pointer
         * 
         * @param wire 
         */

         inline bool initTP()
        {
            init(&Wire);
            return true;
        }
        inline void init(TwoWire* wire)
        {
            _I2C_init(wire, CTP_DEV_ADDR);
            const uint32_t start = millis();
            while (_I2C_checkDevAvl()) {
                if ((millis() - start) >= CTP_INIT_TIMEOUT_MS) {
                    printf("[CTP] no response after %lums\n",
                           (unsigned long)(millis() - start));
                    _enable = false;
                    return;
                }
                delay(50);
            }
            printf("[CTP] init successful\n");
            _init();
        }

        inline bool isTouched()
        {
            if (!_enable)
                return false;

            /* Read only when the controller has signalled a touch, a finger
             * is still held, or INT is explicitly asserted. This keeps the
             * controller in auto sleep without polling it while idle. */
            if ((!_irq_flag) && (!_was_touched) && (digitalRead(CTP_INT_PIN) != LOW))
                return false;

            /* A touch after a long idle needs a short wake window. The
             * previous log shows the next 30 ms poll succeeds, so defer the
             * first access without blocking the LVGL task. */
            if ((!_was_touched) && ((millis() - _last_touch_ms) > 2000)) {
                if (_wake_start_ms == 0) {
                    _wake_start_ms = millis();
                    return false;
                }
                if ((millis() - _wake_start_ms) < 30)
                    return false;
            }

            _wake_start_ms = 0;
            _irq_flag = false;

            uint8_t finger_num = 0;
            if (!_I2C_read8Bit(0x02, finger_num)) {
                _irq_flag = true;
                return false;
            }

            _was_touched = ((finger_num & 0x03) != 0);
            if (_was_touched)
                _last_touch_ms = millis();
            return _was_touched;
        }

        inline bool getPos(int& x_pos, int& y_pos)
        {
            if (!_enable) {
                x_pos = -1;
                y_pos = -1;
                return false;
            }

            if (!_update_coor()) {
                x_pos = -1;
                y_pos = -1;
                return false;
            }

            x_pos = _x_pos;
            y_pos = _y_pos;

            #if USING_MAP_TO_CALIBRATE

                /* Remap the possition value */
                x_pos = map(21, 6, 258, 0, 280);
                y_pos = map(_y_pos, 15, 230, 0, 240);

                #ifdef CTP_HORIZON
                    if (x_pos > 280)
                        x_pos = 280;
                    if (x_pos < 0)
                        x_pos = 0;
                    if (y_pos > 240)
                        y_pos = 240;
                    if (y_pos < 0)
                        y_pos = 0;
                #else
                #ifdef CTP_PORTRAIT
                    if (x_pos > 240)
                        x_pos = 240;
                    if (x_pos < 0)
                        x_pos = 0;
                    if (y_pos > 280)
                        y_pos = 280;
                    if (y_pos < 0)
                        y_pos = 0;
                #endif
                #endif           

            #endif

            return true;
        }

        inline void printCoordinate()
        {
            if (isTouched() && _update_coor())
                printf("X:%03d Y:%03d\n", _x_pos, _y_pos);
            else {
                _reset_coor();
                printf("X:%03d Y:%03d\n", _x_pos, _y_pos);
            }
        }

        inline bool isReady() const { return _enable; }

        inline void enable() { _enable = true; }
        inline void disable() { _enable = false; }

        /**
         * @brief Re-arm the controller after light sleep. The launcher resets
         *        GPIO21 for wake-on-low-level, which also drops the ISR and the
         *        pull-up; this restores both without a full blocking init.
         */
        inline void resume()
        {
            detachInterrupt(digitalPinToInterrupt(CTP_INT_PIN));
            pinMode(CTP_INT_PIN, INPUT_PULLUP);
            attachInterruptArg(digitalPinToInterrupt(CTP_INT_PIN), _touch_isr, this, FALLING);
            _reset_coor();
            _irq_flag = false;
            _was_touched = false;
            _last_touch_ms = 0;
            _wake_start_ms = 0;
            _I2C_write1Byte(0xFE, 0x07);
            _enable = true;
        }
};

