/**
 * @file Chappie.cpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-10
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include "Chappie.h"

// String list [5]={"0"};
// bool I2C_dev_scan(uint8_t tar) {
//     uint8_t error, address;
//     int nDevices;
//     nDevices = 0;
//     for (address = 1; address < 127; address++ ) {
//         int i =0;
//         Wire.beginTransmission(address);
//         error = Wire.endTransmission();

//         if (error == 0) {
//             printf("\n[I2C_SCAN]: device found at address 0x");
//             if (address < 16)
//                 Serial.print("0");
//             printf("%hx\n",address, HEX);
//             if((tar,HEX) == (address, HEX)){
//                 return true;
//             }
//             nDevices++;
//         }
//     }
//     return false;
// }

void CHAPPIE::init()
{
    /* Init I2C */
    Wire.begin(5, 4);
    // Wire.setClock(400000);
    // delay(3000);
    /* Init power contrl */
    delay(30);
    Pow.init();
    
    // delay(3000);

    /* Init RGB led */
    FastLED.showColor(CRGB::Red, 5);
    /* Init lcd */
    Lcd.init();
    Lcd.setFont(&fonts::efontCN_12);

    /* Print Logo in a cool way */
    Lcd.setCursor(0, 0);
    for (char c : Yeely) {
        Lcd.printf("%c", c);
        delay(1);
    }
    // Lcd.printf("%s", Logo);
    // delay(50);
    Lcd.printf("\n ChappieBSP %s :)\n Author: Forairaaaaa\n", EMMA_BSP_VERISON);
    // delay(50);
    Lcd.printf(" Project: %s\n", EMMA_PROJECT_NAME);
    // Lcd.printf(" IIC Num: %d\n", num);
    // delay(50);
    Tp.init(&Wire);

    /* Init touchpad */
    Lcd.printf("%s", Cowsay("Pls touch screen to wakeup Tp").c_str());
    
    Lcd.printf("Tp init success!\n");
    Wf.Init();
    /* Init RTC */
    Rtc.begin();
    /* Init IMU */
    // Imu.init();
    /* Init SD card */
    Sd.init();
    
    /* Init BMP280 */
    Env.init();
    
    // if(!I2C_dev_scan(167)){ // 167(OCT)  - > 77 (HEX)
    //     Pow.CloseLDO();
    // }else{
    //     Env.init();
    // }
    // printf("Temp:%.2f,Pressure: %.2f , Altitude:%.2f",Env.getTemperature(),Env.getPressure(),Env.calcAltitude(Env.getPressure()));
    
    /* Fire up */
    Speaker.setVolume(50);
    Speaker.tone(9000, 300);
    Vibrator.Buzzzzz(300);
    
    FastLED.showColor(CRGB::Black);

    //Resource Init
    ResourcePool::Init();
}
