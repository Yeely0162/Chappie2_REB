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
#define DEBUG_CPU 1
#include "esp32s3/rom/rtc.h"
void print_reset_reason(int reason)
{
  switch ( reason)
  {
    case 1 : printf("\nPOWERON_RESET");break;          /**<1,  Vbat power on reset*/
    case 3 : printf("\nSW_RESET");break;               /**<3,  Software reset digital core*/
    case 4 : printf("\nOWDT_RESET");break;             /**<4,  Legacy watch dog reset digital core*/
    case 5 : printf("\nDEEPSLEEP_RESET");break;        /**<5,  Deep Sleep reset digital core*/
    case 6 : printf("\nSDIO_RESET");break;             /**<6,  Reset by SLC module, reset digital core*/
    case 7 : printf("\nTG0WDT_SYS_RESET");break;       /**<7,  Timer Group0 Watch dog reset digital core*/
    case 8 : printf("\nTG1WDT_SYS_RESET");break;       /**<8,  Timer Group1 Watch dog reset digital core*/
    case 9 : printf("\nRTCWDT_SYS_RESET");break;       /**<9,  RTC Watch dog Reset digital core*/
    case 10 : printf("\nINTRUSION_RESET");break;       /**<10, Instrusion tested to reset CPU*/
    case 11 : printf("\nTGWDT_CPU_RESET");break;       /**<11, Time Group reset CPU*/
    case 12 : printf("\nSW_CPU_RESET");break;          /**<12, Software reset CPU*/
    case 13 : printf("\nRTCWDT_CPU_RESET");break;      /**<13, RTC Watch dog Reset CPU*/
    case 14 : printf("\nEXT_CPU_RESET");break;         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : printf("\nRTCWDT_BROWN_OUT_RESET");break;/**<15, Reset when the vdd voltage is not stable*/
    case 16 : printf("\nRTCWDT_RTC_RESET");break;      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : printf("\nNO_MEAN");
  }
}
void verbose_print_reset_reason(int reason)
{
  switch ( reason)
  {
    case 1  : printf("\nVbat power on reset");break;
    case 3  : printf("\nSoftware reset digital core");break;
    case 4  : printf("\nLegacy watch dog reset digital core");break;
    case 5  : printf("\nDeep Sleep reset digital core");break;
    case 6  : printf("\nReset by SLC module, reset digital core");break;
    case 7  : printf("\nTimer Group0 Watch dog reset digital core");break;
    case 8  : printf("\nTimer Group1 Watch dog reset digital core");break;
    case 9  : printf("\nRTC Watch dog Reset digital core");break;
    case 10 : printf("\nInstrusion tested to reset CPU");break;
    case 11 : printf("\nTime Group reset CPU");break;
    case 12 : printf("\nSoftware reset CPU");break;
    case 13 : printf("\nRTC Watch dog Reset CPU");break;
    case 14 : printf("\nfor APP CPU, reseted by PRO CPU");break;
    case 15 : printf("\nReset when the vdd voltage is not stable");break;
    case 16 : printf("\nRTC Watch dog reset digital core and rtc module");break;
    default : printf("\nNO_MEAN");
  }
}
void CHAPPIE::init()
{
    /* Init I2C */
    Wire.begin(5, 4);
    Wire.setClock(400000);
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
    Imu.init();
    /* Init SD card */
    Sd.init();
    
    /* Init BMP280 */
    Env.init();
    
    
    /* Fire up */
    Speaker.setVolume(50);
    Speaker.tone(9000, 300);
    Vibrator.Buzzzzz(300);
    
    FastLED.showColor(CRGB::Black);
    #ifdef DEBUG_CPU 
        print_reset_reason(rtc_get_reset_reason(0));
        verbose_print_reset_reason(rtc_get_reset_reason(0));
        print_reset_reason(rtc_get_reset_reason(1));
        verbose_print_reset_reason(rtc_get_reset_reason(1));
    #endif
    //Resource Init
    ResourcePool::Init();
}
