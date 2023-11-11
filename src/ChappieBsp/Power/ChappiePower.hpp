/**
 * @file ChappiePower.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-03-11
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <Arduino.h>
#include "AXP173/AXP173.h"
#define AXPManage 1             
#define IP5Manage 0
#define CHAPPIE_PWR_CTRL    GPIO_NUM_9
#define CHAPPIE_BATM_ADC    GPIO_NUM_8
#define CHAPPIE_BATM_EN     GPIO_NUM_18
#define POW_LOG(format, args...) printf(format, ##args)

// extern AXP173 pmu;
class ChappiePower {
    private:
        inline void _enableBatMeasure() { digitalWrite(CHAPPIE_BATM_EN, 0); }
        inline void _disableBatMeasure() { digitalWrite(CHAPPIE_BATM_EN, 1); }
        AXP173 pmu;
        

    public:
        ChappiePower() {}
        ~ChappiePower() {}
        inline void CloseLDO()
        { 
            #if AXPManage
                pmu.setOutputEnable(pmu.OP_LDO2,false);
                pmu.setOutputEnable(pmu.OP_LDO3,false);
            #endif
        }
        inline void init()
        {
            #if IP5Manage
                gpio_reset_pin(CHAPPIE_BATM_ADC);
                gpio_reset_pin(CHAPPIE_BATM_EN);
                gpio_set_direction(CHAPPIE_BATM_EN, GPIO_MODE_OUTPUT_OD);
                analogReadResolution(12);
            #endif
            
            // AXP173
            #if AXPManage
                bool AXPInit = pmu.begin();
                Serial.println(AXPInit ? "AXP Init succes!" : "AXP Init Failed!");
                // pmu.setChargeCurrent(pmu.CHG_700mA);//设置充电电流
                pmu.setOutputEnable(pmu.OP_LDO2,true);
                pmu.setOutputVoltage(pmu.OP_LDO2,2000);
                pmu.setOutputEnable(pmu.OP_LDO3,true);
                pmu.setOutputVoltage(pmu.OP_LDO3,2000);
                
                // pmu.setOutputVoltage(pmu.OP_LDO2,3000);
                // 注意不设置是无法生效的,即使你使用了获取电流函数
                pmu.setADCEnable(pmu.ADC_TS,false); //设置TS管脚ADC进行检测电池温度状态  
                pmu.setADCEnable(pmu.ADC_BAT_C,true); //设置电池管脚ADC进行检测电池电流状态
                pmu.setADCEnable(pmu.ADC_BAT_V,true); //设置电池管脚ADC进行检测电池电压状态
                pmu.setADCEnable(pmu.ADC_VBUS_C,true); //设置USB管脚ADC进行检测USB电流状态
                pmu.setADCEnable(pmu.ADC_VBUS_V,true); //设置USB管脚ADC进行检测USB电流状态
                POW_LOG("[POW] AXPTemp: %.2f C   USBVol: %.2f V USBCurr: %.2f mA BatPower: %.2f W\n"
                        ,pmu.getAXP173Temp(),pmu.getVBUSVoltage()
                        ,pmu.getVBUSCurrent(),pmu.getBatPower());
                POW_LOG(pmu.isCharging() ? "[POW] Charing" : "DisCharing\n");
                POW_LOG("[POW]BatUse: %.2f C   BatVol: %.2f V BatCurr: %.2f mA BatTemp: %.2f C\n"
                                ,pmu.getBatLevel(),pmu.getBatVoltage()
                                ,pmu.getBatDisCurrent(),pmu.getTSTemp());

            #endif



        }
        inline void powerOff()
        {   
            #if IP5Manage
                gpio_reset_pin(CHAPPIE_PWR_CTRL);
                gpio_set_direction(CHAPPIE_PWR_CTRL, GPIO_MODE_OUTPUT_OD);
                /* Double click */
                gpio_set_level(CHAPPIE_PWR_CTRL, 0);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 1);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 0);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 1);
                delay(1000);
            #endif
            #if AXPManage
                delay(1000);
                POW_LOG("[POW] Power OFF");
                delay(500);
                pmu.powerOFF();
            #endif
        }
        
        inline float readBatVoltage()
        {
            /* Calculate the real bat voltage */
            #if AXPManage
                return ((float)pmu.getBatLevel());
            #endif
            #if IP5Manage
                return ((float)readBatMilliVoltRaw() * 3 / 2000);
            #endif
        }
    #if AXPManage
        inline bool isCharing()
        {   
            return pmu.isCharging();
        }
        inline String AXP173Temp()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getAXP173Temp());
            
        }
        inline String USBVoltage()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getVBUSVoltage());
            
        }
        inline String USBCurrent()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getVBUSCurrent());
            
        }
        inline bool isChargedBat()
        {
            /* Calculate the real bat voltage */
            
                return pmu.isBatExist();
            
        }
        inline String BatVoltage()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getBatVoltage());
            
        }
        inline String BatCurrent()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getBatCurrent());
            
        }
        inline String BatPower()
        {
            /* Calculate the real bat voltage */
            
                return ((String)pmu.getBatPower());
            
        }
        inline bool BatisCharging()
        {
            /* Calculate the real bat voltage */
            
                return pmu.isCharging();
            
        }
        
    #endif
        inline uint32_t readBatMilliVoltRaw()
        {  
            gpio_set_direction(CHAPPIE_BATM_EN, GPIO_MODE_OUTPUT_OD);
            gpio_set_level(CHAPPIE_BATM_EN, 0);
            delay(10);
            uint32_t ret = analogReadMilliVolts(CHAPPIE_BATM_ADC);
            gpio_set_level(CHAPPIE_BATM_EN, 1);
            gpio_reset_pin(CHAPPIE_BATM_EN);
            gpio_reset_pin(CHAPPIE_BATM_ADC);
            return ret;
        }

        inline uint8_t readBatPercentage()
        {
            /* Asume 0~100% to be 3.2~4.2V */
            uint8_t ret = map((readBatMilliVoltRaw() * 3 / 2), 3200, 4100, 0, 100);
            return ret;
        }

};

