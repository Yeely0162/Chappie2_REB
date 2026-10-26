/**
 * @file ChappieIMU.hpp
 * @author Forairaaaaa
 * @brief Reference: https://github.com/ElectronicCats/mpu6050/blob/master/examples/MPU6050_DMP6/MPU6050_DMP6.ino
 * @version 0.1
 * @date 2023-03-11
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "../Utility/MPU6050/MPU6050_6Axis_MotionApps20.h"
#include "../Utility/MPU6050/MPU6050.h"


class ChappieIMU : public MPU6050 {
    private:
        uint8_t _fifoBuffer[64];
        Quaternion _q;
        VectorFloat _gravity;
        float _ypr[3];
        bool _ready = false;

    public:
        ChappieIMU() {}
        ~ChappieIMU() {}

        inline void init()
        {
            // /* Every call performs the full DMP init/calibration, as the snake
            //  * app relies on when it enters. The flag only tracks the result. */
            // _ready = false;
            

            // initialize device
            initialize();
            printf(testConnection() ? "[IMU] connection successful\n" : "[IMU] connection failed\n");
            // reset DMP 
            // resetDMP();
            // load and configure the DMP
            uint8_t devStatus = dmpInitialize();

            // supply your own gyro offsets here, scaled for min sensitivity
            // setXGyroOffset(220);
            // setYGyroOffset(76);
            // setZGyroOffset(-85);
            // setZAccelOffset(1788);

            // make sure it worked (returns 0 if so)
            printf("devStatus:%d\n",devStatus);
            if (devStatus == 0) {
                // Calibration Time: generate offsets and calibrate our MPU6050
                CalibrateAccel(6);
                CalibrateGyro(6);
                PrintActiveOffsets();
                // turn on the DMP, now that it's ready
                setDMPEnabled(true);
                _ready = true;
            } else {
                printf("[IMU] DMP init failed\n");
            }
        }

        inline bool isReady() const { return _ready; }

        /**
         * @brief Get the Euler angles in degrees
         * 
         * @param yaw
         * @param pitch
         * @param roll
         * @param gravityX optional: DMP gravity vector X in the sensor frame
         * @param gravityY optional: DMP gravity vector Y in the sensor frame
         * @param gravityZ optional: DMP gravity vector Z in the sensor frame
         * @return uint8_t 0 when a new packet was read, 1 when the IMU is not
         *                 ready, 2 when no new FIFO packet is available
         *
         * Gravity is the world-up direction expressed in the MPU sensor
         * frame (about (0,0,1) when the sensor is level).  It is more useful
         * than the Euler angles for tilt compensation because it avoids
         * pitch/roll ordering and wrap-around issues.
         */
        inline uint8_t getYawPitchRoll(float& yaw, float& pitch, float& roll,
                                       float* gravityX = nullptr,
                                       float* gravityY = nullptr,
                                       float* gravityZ = nullptr)
        {
            if (!_ready)
                return 1;

            // read a packet from FIFO
            if (dmpGetCurrentFIFOPacket(_fifoBuffer)) {
                dmpGetQuaternion(&_q, _fifoBuffer);
                dmpGetGravity(&_gravity, &_q);
                dmpGetYawPitchRoll(_ypr, &_q, &_gravity);
                yaw = _ypr[0] * 180 / M_PI;
                pitch = _ypr[1] * 180 / M_PI;
                roll = _ypr[2] * 180 / M_PI;
                if (gravityX != nullptr) *gravityX = _gravity.x;
                if (gravityY != nullptr) *gravityY = _gravity.y;
                if (gravityZ != nullptr) *gravityZ = _gravity.z;
                return 0;
            }
            return 2;
        }



};

