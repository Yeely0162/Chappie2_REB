/**
 * @file ChappieMAG.hpp
 * @author Yeely
 * @brief Reference: https://github.com/boschsensortec/BMM150_SensorAPI
 * @version 0.1
 * @date 2026-10-21
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "../Utility/ChappieI2C.hpp"

/* BMM150 I2C address and identity */
#define MAG_DEV_ADDR            0x10
#define MAG_CHIP_ID             0x32

/* BMM150 registers */
#define MAG_REG_CHIP_ID         0x40
#define MAG_REG_DATA_X_LSB      0x42
#define MAG_REG_POWER_CONTROL   0x4B
#define MAG_REG_OP_MODE         0x4C
#define MAG_REG_REP_XY          0x51
#define MAG_REG_REP_Z           0x52
#define MAG_REG_DIG_X1          0x5D
#define MAG_REG_DIG_Z4_LSB      0x62
#define MAG_REG_DIG_Z2_LSB      0x68


/**
 * @brief BMM150 driver on the shared I2C bus
 *
 * CHAPPIE::init() does not touch this sensor. Apps that need the magnetometer
 * call CHAPPIE::initMag() (or init(&Wire)) on demand, so an unused sensor
 * produces no I2C traffic and no serial output at boot.
 */
class ChappieMAG {
    public:
        enum Status : uint8_t {
            STATUS_NOT_INIT = 0,
            STATUS_READY,
            STATUS_ABSENT,      /* no I2C ACK on any candidate address */
            STATUS_ERROR,       /* ACK but chip id / trim / mode failed */
        };

        ChappieMAG() : _wire(NULL), _dev_addr(MAG_DEV_ADDR), _status(STATUS_NOT_INIT) {}
        ~ChappieMAG() {}

        /**
         * @brief Probe and init BMM150 on the shared I2C bus
         *
         * The driver never calls Wire.begin(), the bus pins stay owned by CHAPPIE.
         * Candidate addresses 0x10 ~ 0x13 cover the SDO/CSB strap combinations.
         *
         * @param wire shared TwoWire
         * @param dev_addr address from the schematic
         * @return true when chip id and trim data are valid
         */
        inline bool init(TwoWire* wire, uint8_t dev_addr = MAG_DEV_ADDR)
        {
            _I2C_init(wire, dev_addr);
            _status = STATUS_NOT_INIT;

            if (_wire == NULL) {
                _status = STATUS_ERROR;
                return false;
            }

            if (!_I2C_checkDevAvl()) {
                bool found = false;
                for (uint8_t addr = 0x10; addr <= 0x13; addr++) {
                    if (addr == _dev_addr)
                        continue;
                    _dev_addr = addr;
                    if (_I2C_checkDevAvl()) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    _dev_addr = dev_addr;
                    _status = STATUS_ABSENT;
                    printf("[MAG] not found on 0x10 ~ 0x13\n");
                    return false;
                }
            }

            /* Suspend -> sleep, then wait for the 3ms start-up time */
            if (!_setPowerControl(1)) {
                printf("[MAG] power control failed\n");
                _status = STATUS_ERROR;
                return false;
            }
            delay(3);

            /* Verify identity before touching trim registers */
            uint8_t chip_id = 0;
            if (!_I2C_read8Bit(MAG_REG_CHIP_ID, chip_id) || (chip_id != MAG_CHIP_ID)) {
                printf("[MAG] chip id: 0x%02X\n", chip_id);
                _status = STATUS_ERROR;
                return false;
            }

            if (!_readTrim()) {
                printf("[MAG] read trim failed\n");
                _status = STATUS_ERROR;
                return false;
            }

            if (!_setRegularPreset()) {
                printf("[MAG] set mode failed\n");
                _status = STATUS_ERROR;
                return false;
            }

            _status = STATUS_READY;
            printf("[MAG] connection successful @0x%02X\n", _dev_addr);
            return true;
        }

        /**
         * @brief Read and compensate one sample
         *
         * @param x compensated field on X axis, uT
         * @param y compensated field on Y axis, uT
         * @param z compensated field on Z axis, uT
         * @return true on bus success and non-overflow sample
         */
        inline bool read(float& x, float& y, float& z)
        {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;

            if ((_status != STATUS_READY) || (_wire == NULL))
                return false;

            uint8_t buff[8];
            if (!_I2C_readBuff(MAG_REG_DATA_X_LSB, 8, buff))
                return false;

            /* Bosch layout: low 5/7 bits live in the MSBs of the LSB byte */
            int16_t raw_x = (int16_t)((int16_t)((int8_t)buff[1]) * 32) | (int16_t)(buff[0] & 0xF8);
            int16_t raw_y = (int16_t)((int16_t)((int8_t)buff[3]) * 32) | (int16_t)(buff[2] & 0xF8);
            int16_t raw_z = (int16_t)((int16_t)((int8_t)buff[5]) * 128) | (int16_t)(buff[4] & 0xFE);
            uint16_t rhall = (uint16_t)(((uint16_t)buff[7] << 6) | (buff[6] & 0xFC));

            /* Overflow / hall resistance flip means the sample is not usable */
            if ((raw_x == -4096) || (raw_y == -4096) || (raw_z == -16384))
                return false;

            x = _compensateX(raw_x, rhall);
            y = _compensateY(raw_y, rhall);
            z = _compensateZ(raw_z, rhall);
            return true;
        }

        /**
         * @brief Read one sample and print it to the serial console
         *
         */
        inline void printSample()
        {
            float x, y, z;
            if (read(x, y, z))
                printf("[MAG] X: %.2f Y: %.2f Z: %.2f uT\n", x, y, z);
            else
                printf("[MAG] read failed\n");
        }

        inline bool isReady() const { return _status == STATUS_READY; }
        inline uint8_t address() const { return _dev_addr; }
        inline Status status() const { return _status; }

        static inline const char* statusText(Status status)
        {
            switch (status) {
                case STATUS_READY:  return "OK";
                case STATUS_ABSENT: return "Not found";
                case STATUS_ERROR:  return "Init failed";
                default:            return "Not init";
            }
        }

    private:
        TwoWire* _wire;
        uint8_t _dev_addr;
        Status _status;

        /* Trim data */
        int8_t   _digX1 = 0;
        int8_t   _digY1 = 0;
        int8_t   _digX2 = 0;
        int8_t   _digY2 = 0;
        uint16_t _digZ1 = 0;
        int16_t  _digZ2 = 0;
        int16_t  _digZ3 = 0;
        int16_t  _digZ4 = 0;
        uint8_t  _digXY1 = 0;
        int8_t   _digXY2 = 0;
        uint16_t _digXYZ1 = 0;

        inline void _I2C_init(TwoWire* wire, uint8_t dev_addr)
        {
            _wire = wire;
            _dev_addr = dev_addr;
        }

        inline bool _I2C_checkDevAvl()
        {
            ChappieI2CGuard lock;

            _wire->beginTransmission(_dev_addr);
            return (_wire->endTransmission() == 0);
        }

        inline bool _I2C_write1Byte(uint8_t addr, uint8_t data)
        {
            ChappieI2CGuard lock;

            _wire->beginTransmission(_dev_addr);
            _wire->write(addr);
            _wire->write(data);
            return (_wire->endTransmission() == 0);
        }

        inline bool _I2C_readBuff(uint8_t addr, int size, uint8_t buff[])
        {
            ChappieI2CGuard lock;

            _wire->beginTransmission(_dev_addr);
            _wire->write(addr);
            if (_wire->endTransmission() != 0)
                return false;
            if (_wire->requestFrom(_dev_addr, (size_t)size, true) != (size_t)size)
                return false;
            for (int i = 0; i < size; i++) {
                int data = _wire->read();
                if (data < 0)
                    return false;
                buff[i] = (uint8_t)data;
            }
            return true;
        }

        inline bool _I2C_read8Bit(uint8_t addr, uint8_t& data)
        {
            return _I2C_readBuff(addr, 1, &data);
        }

        inline bool _setPowerControl(uint8_t value)
        {
            uint8_t reg = 0;
            if (!_I2C_read8Bit(MAG_REG_POWER_CONTROL, reg))
                return false;
            reg = (uint8_t)((reg & ~0x01) | (value & 0x01));
            return _I2C_write1Byte(MAG_REG_POWER_CONTROL, reg);
        }

        inline bool _setRegularPreset()
        {
            uint8_t reg = 0;
            if (!_I2C_read8Bit(MAG_REG_OP_MODE, reg))
                return false;

            /* ODR 10Hz, XY repetition 4, Z repetition 7 (regular preset) */
            reg = (uint8_t)(reg & ~0x38);
            if (!_I2C_write1Byte(MAG_REG_OP_MODE, reg))
                return false;
            if (!_I2C_write1Byte(MAG_REG_REP_XY, 0x04))
                return false;
            if (!_I2C_write1Byte(MAG_REG_REP_Z, 0x07))
                return false;

            /* Normal mode, write op-mode bits [2:1] last */
            reg = (uint8_t)(reg & ~0x06);
            if (!_I2C_write1Byte(MAG_REG_OP_MODE, reg))
                return false;

            delay(10);
            return true;
        }

        inline bool _readTrim()
        {
            uint8_t x1y1[2];
            uint8_t xyz[4];
            uint8_t xy[10];

            if (!_I2C_readBuff(MAG_REG_DIG_X1, 2, x1y1))
                return false;
            if (!_I2C_readBuff(MAG_REG_DIG_Z4_LSB, 4, xyz))
                return false;
            if (!_I2C_readBuff(MAG_REG_DIG_Z2_LSB, 10, xy))
                return false;

            _digX1 = (int8_t)x1y1[0];
            _digY1 = (int8_t)x1y1[1];
            _digX2 = (int8_t)xyz[2];
            _digY2 = (int8_t)xyz[3];
            _digZ4 = (int16_t)(((uint16_t)xyz[1] << 8) | xyz[0]);
            _digZ2 = (int16_t)(((uint16_t)xy[1] << 8) | xy[0]);
            _digZ1 = (uint16_t)(((uint16_t)xy[3] << 8) | xy[2]);
            _digXYZ1 = (uint16_t)(((uint16_t)(xy[5] & 0x7F) << 8) | xy[4]);
            _digZ3 = (int16_t)(((uint16_t)xy[7] << 8) | xy[6]);
            _digXY2 = (int8_t)xy[8];
            _digXY1 = xy[9];

            return (_digXYZ1 != 0);
        }

        /* Bosch BMM150 SensorAPI compensation equations (float variants) */
        inline float _compensateX(int16_t mag_x, uint16_t rhall) const
        {
            if ((mag_x == -4096) || (rhall == 0) || (_digXYZ1 == 0))
                return 0.0f;

            float ret = ((float)_digXYZ1) * 16384.0f / rhall - 16384.0f;
            float comp = ((float)_digXY2) * (ret * ret / 268435456.0f);
            comp += ret * ((float)_digXY1) / 16384.0f;
            comp = mag_x * ((comp + 256.0f) * (((float)_digX2) + 160.0f)) / 8192.0f;
            comp += ((float)_digX1) * 8.0f;
            return comp / 16.0f;
        }

        inline float _compensateY(int16_t mag_y, uint16_t rhall) const
        {
            if ((mag_y == -4096) || (rhall == 0) || (_digXYZ1 == 0))
                return 0.0f;

            float ret = ((float)_digXYZ1) * 16384.0f / rhall - 16384.0f;
            float comp = ((float)_digXY2) * (ret * ret / 268435456.0f);
            comp += ret * ((float)_digXY1) / 16384.0f;
            comp = mag_y * ((comp + 256.0f) * (((float)_digY2) + 160.0f)) / 8192.0f;
            comp += ((float)_digY1) * 8.0f;
            return comp / 16.0f;
        }

        inline float _compensateZ(int16_t mag_z, uint16_t rhall) const
        {
            if ((mag_z == -16384) || (_digZ2 == 0) || (_digZ1 == 0) ||
                (_digXYZ1 == 0) || (rhall == 0)) {
                return 0.0f;
            }

            float z0 = ((float)mag_z) - ((float)_digZ4);
            float z1 = ((float)rhall) - ((float)_digXYZ1);
            float z2 = ((float)_digZ3) * z1;
            float z3 = ((float)_digZ1) * ((float)rhall) / 32768.0f;
            float z4 = ((float)_digZ2) + z3;
            float z5 = (z0 * 131072.0f) - z2;
            return (z5 / (z4 * 4.0f)) / 16.0f;
        }
};
