/**
 * @file App_Launcher.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-03-15
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "App_Launcher.h"
#include "../ChappieUIConfigs.h"
#include "../Resource/ResourcePool.h"
#include "UI/ui.h"

static const char *week[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

/* Structure to hold App num and status */
struct AppManager_t {
    uint16_t totalNum = 0;
    int16_t selected = -1;
    bool isRunning = false;
    bool onCreate = false;
    bool onDestroy = false;
    bool showTimePage = false;
};
static AppManager_t _app;


/* Structure to hold device status owned by the launcher UI */
struct DeviceStatus_t {
    bool autoScreenOff = false;
    uint8_t brightness = 127;
    uint32_t autoScreenOffSleep = (1000UL * 60UL) * 20UL;   // 20 min
};
static DeviceStatus_t _device_status;

/* Small charging bolt overlay on top of the status-bar battery body. */
static lv_obj_t *s_bolt = nullptr;

namespace {

#define COL_WHITE   lv_color_hex(0xF2F4F7)
#define COL_MUTED   lv_color_hex(0x8A919B)
#define COL_TEAL    lv_color_hex(0x18C7A8)
#define COL_BLUE    lv_color_hex(0x3B82F6)
#define COL_ORANGE  lv_color_hex(0xFFB02E)
#define COL_RED     lv_color_hex(0xE5484D)
#define COL_NEUTRAL lv_color_hex(0xA6B4CD)

lv_color_t wifi_color(WifiUiState s)
{
    switch (s) {
    case WifiUiState::Connected:
    case WifiUiState::ApConnected:
        return COL_WHITE;
    case WifiUiState::ApMode:
        return COL_TEAL;
    case WifiUiState::Connecting:
    case WifiUiState::ApConnecting:
    case WifiUiState::Starting:
        return COL_ORANGE;
    case WifiUiState::Error:
        return COL_RED;
    case WifiUiState::Off:
    default:
        return COL_NEUTRAL;
    }
}

lv_color_t wifi_button_color(WifiUiState s)
{
    switch (s) {
    case WifiUiState::Connected: return COL_BLUE;
    case WifiUiState::ApConnected: return COL_BLUE;
    case WifiUiState::ApMode: return COL_TEAL;
    case WifiUiState::Connecting:
    case WifiUiState::ApConnecting:
    case WifiUiState::Starting: return COL_ORANGE;
    case WifiUiState::Error: return COL_RED;
    case WifiUiState::Off:
    default: return COL_NEUTRAL;
    }
}

lv_color_t ble_color(BleUiState s)
{
    switch (s) {
    case BleUiState::Connected: return COL_TEAL;
    case BleUiState::Advertising: return COL_WHITE;
    case BleUiState::Starting: return COL_ORANGE;
    case BleUiState::Error: return COL_RED;
    case BleUiState::Off:
    default: return COL_NEUTRAL;
    }
}

lv_color_t ble_button_color(BleUiState s)
{
    switch (s) {
    case BleUiState::Connected: return COL_BLUE;
    case BleUiState::Advertising: return COL_TEAL;
    case BleUiState::Starting: return COL_ORANGE;
    case BleUiState::Error: return COL_RED;
    case BleUiState::Off:
    default: return COL_NEUTRAL;
    }
}

void set_button_bg(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

} // namespace


namespace App {

    App_Launcher::AppBackHandler App_Launcher::_appBackHandler = nullptr;

    void SetAppBackHandler(AppBackHandlerFn handler)
    {
        App_Launcher::setAppBackHandler(handler);
    }

    void App_Launcher::onCreate()
    {
        _device->lvgl.disable();

        /* Rebuild only the UI objects. Re-initialising LVGL here used to
         * allocate two new full-screen PSRAM buffers on every app exit. */
        ui_init();
        lv_obj_set_style_bg_color(ui_ScreenLauncher, lv_color_hex(0x0B0D10), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_scr_load_anim(ui_ScreenLauncher, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);

        /* Desktop init */
        lv_obj_set_scroll_snap_x(ui_PanelDesktop, LV_SCROLL_SNAP_CENTER);
        lv_obj_update_snap(ui_PanelDesktop, LV_ANIM_ON);

        /* Read App register */
        _app.totalNum = sizeof(App::Register) / sizeof(App::AppRegister_t);

        /* Place App on the desktop panel */
        for (int i = 0; i < _app.totalNum; i++) {
            /* Apps' Icon (button) */
            lv_obj_t* app_btn = lv_btn_create(ui_PanelDesktop);
            lv_obj_set_size(app_btn, 150, 150);
            lv_obj_align(app_btn, LV_ALIGN_CENTER, i * 180 + 560, 0);
            lv_obj_add_flag(app_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
            lv_obj_set_style_radius(app_btn, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(app_btn, lv_color_hex(0xA6B4CD), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(app_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_spread(app_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

            /* Setup event call back */
            lv_obj_add_event_cb(app_btn, button_event_cb, LV_EVENT_CLICKED, NULL);

            /* Get App Icon image */
            const lv_img_dsc_t* app_icon = (const lv_img_dsc_t*)App::Register[i].appIcon();
            if (app_icon == NULL) {
                app_icon = &ui_img_img_icon_defalut_png;
            }
            lv_obj_set_style_bg_img_src(app_btn, app_icon, LV_PART_MAIN | LV_STATE_DEFAULT);

            /* Apps' name */
            lv_obj_t* app_name = lv_label_create(ui_PanelDesktop);
            lv_obj_align(app_name, LV_ALIGN_CENTER, i * 180 + 560, 95);
            lv_label_set_text(app_name, App::Register[i].appName().c_str());
            lv_obj_set_style_text_color(app_name, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(app_name, &ui_font_FontUbuntuBold18, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        /* The control-pad buttons drive real services and are not local
         * toggles, so do not let LVGL manage a CHECKED state for them. */
        lv_obj_clear_flag(ui_ButtonWifi, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_clear_flag(ui_ButtonBle, LV_OBJ_FLAG_CHECKABLE);

        /* Charging badge: a small bolt drawn on top of the battery body.
         * The battery outline and its level bar are children of
         * ui_ImgBattery and stay visible in both states; charging only
         * toggles this overlay. Created once per launcher build. */
        s_bolt = nullptr;
        if (ui_ImgBattery) {
            /* Fixed geometry in both charge states, set once: the status bar
             * must not re-layout when the charger is plugged or removed. */
            lv_img_set_src(ui_ImgBattery, &ui_img_img_battery_png);
            lv_obj_set_size(ui_ImgBattery, 32, 27);
            lv_img_set_zoom(ui_ImgBattery, 256);
            lv_obj_clear_flag(ui_BatteryBar, LV_OBJ_FLAG_HIDDEN);

            const lv_img_dsc_t *bolt = (const lv_img_dsc_t *)ResourcePool::GetImage("bolt");
            if (bolt) {
                s_bolt = lv_img_create(ui_ImgBattery);
                lv_img_set_src(s_bolt, bolt);
                lv_obj_center(s_bolt);
                lv_obj_set_style_img_recolor(s_bolt, lv_color_hex(0x18C7A8), 0);
                lv_obj_set_style_img_recolor_opa(s_bolt, 255, 0);
                lv_obj_clear_flag(s_bolt, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(s_bolt, LV_OBJ_FLAG_HIDDEN);
            }
        }

        /* Add ui event call back */
        lv_obj_add_event_cb(ui_PanelDesktop, scroll_event_cb, LV_EVENT_SCROLL_END, NULL);
        lv_obj_add_event_cb(ui_ArcBrightness, panel_control_pad_event_cb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(ui_ButtonAutoScreenOff, panel_control_pad_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(ui_ButtonInfos, panel_control_pad_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(ui_ButtonWifi, panel_control_pad_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(ui_ButtonBle, panel_control_pad_event_cb, LV_EVENT_CLICKED, this);

        /* Brightness source is the display driver, shared with Settings. */
        uint8_t current = _device->Lcd.getBrightness();
        if (current == 0) {
            /* Never start with the panel dark after a wake/shutdown abort. */
            current = 127;
            _device->Lcd.setBrightness(current);
        }
        _device_status.brightness = current;
        lv_arc_set_value(ui_ArcBrightness, current);
        char bbuf[24];
        snprintf(bbuf, sizeof(bbuf), "BRIGHTNESS %u%%", (unsigned)((uint16_t)current * 100 / 255));
        lv_label_set_text(ui_LabelBrightness, bbuf);

        /* Go to the previous place */
        if (_app.selected != -1) {
            /* Pull down state bar manully */
            lv_obj_set_y(ui_ImgStateBar, -105);
            lv_obj_scroll_to_view(lv_obj_get_child(ui_PanelDesktop, (_app.selected * 2 + 2)), LV_ANIM_OFF);
        }
        else {
            lv_obj_scroll_to_view(lv_obj_get_child(ui_PanelDesktop, 1), LV_ANIM_OFF);
        }

        /* Create a timer to update time, battery and service icons */
        _time_update_timer = lv_timer_create(time_update, UpdataBarTime, (void*)_device);
        time_update(_time_update_timer);

        /* Reset auto screen off time counting */
        lv_disp_trig_activity(NULL);

        /* Update auto-screen-off state */
        if (_device_status.autoScreenOff) {
            lv_obj_add_state(ui_ButtonAutoScreenOff, (LV_STATE_CHECKED | LV_STATE_FOCUSED));
        }

        _app.showTimePage = false;
        _device->lvgl.enable();
    }


    void App_Launcher::onLoop()
    {
        _device->lvgl.disable();

        updateDeviceStatus();

        updateAppManage();

        _device->lvgl.enable();
    }


    void App_Launcher::onDestroy()
    {
        if (_time_update_timer) {
            lv_timer_del(_time_update_timer);
            _time_update_timer = nullptr;
        }
        /* Delete Launcher screen and replace it with an empty one */
        lv_disp_load_scr(lv_obj_create(NULL));
        if (ui_ScreenLauncher)
            lv_obj_del(ui_ScreenLauncher);
        ui_ScreenLauncher = nullptr;
        s_bolt = nullptr;      /* deleted with the screen above */
    }


    void App_Launcher::updateDeviceStatus()
    {
        /* Button B: back to the desktop / close the running app. The system
         * power dialog owns B while it is open (handled by CHAPPIEUI). */
        if (_device->Button.B.pressed()) {
            bool consumed = false;
            if (_app.isRunning && _appBackHandler)
                consumed = _appBackHandler();

            if (!consumed) {
                if (_app.isRunning) {
                    _app.onDestroy = true;
                }
                else {
                    lv_obj_scroll_to_view(lv_obj_get_child(ui_PanelDesktop, 1), LV_ANIM_ON);
                }
            }

            /* Reset auto screen off time counting */
            lv_disp_trig_activity(NULL);
        }

        /* If hit auto screen off time */
        if (_device_status.autoScreenOff && lv_disp_get_inactive_time(NULL) > _device_status.autoScreenOffSleep) {
            _device->Lcd.setBrightness(0);
            sleep_mode();
            _device->Tp.resume();
            _device->Lcd.setBrightness(_device_status.brightness);
            lv_disp_trig_activity(NULL);
        }
    }


    void App_Launcher::updateAppManage()
    {
        /* App state manage, kind of like a FSM */
        if (_app.onCreate) {
            _app.onCreate = false;
            _app.isRunning = true;

            /* Destroy launcher */
            onDestroy();
            /* Open App */
            App::Register[_app.selected].getBsp(_device);
            App::Register[_app.selected].onCreate();
        }
        if (_app.isRunning) {
            App::Register[_app.selected].onLoop();
        }
        if (_app.onDestroy) {
            _app.onDestroy = false;
            _app.isRunning = false;

            /* Quit App */
            App::Register[_app.selected].onDestroy();
            /* Create launcher */
            onCreate();
        }
    }

    /* ------------------------------------------------ UI events ------------------------------------------------ */

    void App_Launcher::time_update(lv_timer_t * timer)
    {
        CHAPPIE* device = (CHAPPIE*)timer->user_data;
        if (device == nullptr || ui_LabelStateBarTime == nullptr)
            return;

        char label_buffer[32];

        /* Clock (RTC reads are short and mutex protected).
         *
         * Time and date are validated separately. An RTC that has never been
         * set returns 00:00:00 and date 0/0/2000; that is still a good time
         * reading and must be shown. NTP state is never consulted here. */
        I2C_BM8563_DateTypeDef dateStruct = {0, 0, 0, 0};
        I2C_BM8563_TimeTypeDef rtc_time = {0, 0, 0};

        const bool timeRead = device->Rtc.getTime(&rtc_time);
        const bool timeOk = timeRead &&
                            rtc_time.hours >= 0 && rtc_time.hours <= 23 &&
                            rtc_time.minutes >= 0 && rtc_time.minutes <= 59 &&
                            rtc_time.seconds >= 0 && rtc_time.seconds <= 59;

        const bool dateRead = device->Rtc.getDate(&dateStruct);
        const bool dateOk = dateRead &&
                            dateStruct.month >= 1 && dateStruct.month <= 12 &&
                            dateStruct.date >= 1 && dateStruct.date <= 31 &&
                            dateStruct.year >= 2000;

        static bool everHadTime = false;
        if (timeOk) {
            everHadTime = true;
            snprintf(label_buffer, sizeof(label_buffer), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
            lv_label_set_text(ui_LabelHomeTime, label_buffer);
            lv_label_set_text(ui_LabelStateBarTime, label_buffer);
        } else {
            /* Only an actual I2C/range failure reaches this branch. A valid
             * 00:00 reading is handled above and keeps ticking. Keep the last
             * good clock on screen instead of flashing "--:--" for a single
             * transient bus error; show a placeholder only if there has never
             * been a good read. */
            if (!everHadTime) {
                lv_label_set_text(ui_LabelHomeTime, "--:--");
                lv_label_set_text(ui_LabelStateBarTime, "--:--");
            }
            static bool rtcErrorLogged = false;
            if (!rtcErrorLogged) {
                printf("[UI] RTC time read failed (i2c=%d h=%d m=%d s=%d)\n",
                       (int)timeRead, (int)rtc_time.hours,
                       (int)rtc_time.minutes, (int)rtc_time.seconds);
                rtcErrorLogged = true;
            }
        }

        if (dateOk) {
            int wd = dateStruct.weekDay;
            if (wd < 0 || wd > 6) wd = 0;
            snprintf(label_buffer, sizeof(label_buffer), "%s, %02d-%02d", week[wd], dateStruct.month, dateStruct.date);
            lv_label_set_text(ui_LabelStateBarTime1, label_buffer);
        } else {
            lv_label_set_text(ui_LabelStateBarTime1, timeOk ? "Date not set" : "RTC unavailable");
        }

        /* Battery: one source (ChappiePower snapshot) for icon and label. */
        const bool valid = device->Pow.dataValid();
        uint8_t pct = valid ? device->Pow.percent() : 0;
        bool charging = valid && device->Pow.isCharging();
        bool low = valid && !charging && (pct <= 15);

        lv_bar_set_value(ui_BatteryBar, pct, LV_ANIM_OFF);
        if (valid)
            snprintf(label_buffer, sizeof(label_buffer), "%u", (unsigned)pct);
        else
            snprintf(label_buffer, sizeof(label_buffer), "--");
        lv_label_set_text(ui_BatteryLabel, label_buffer);

        /* The battery body and its level bar are static widgets (configured
         * in onCreate). Only the charging bolt changes with the real state. */
        if (s_bolt) {
            if (charging)
                lv_obj_clear_flag(s_bolt, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_bolt, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_set_style_text_color(ui_BatteryLabel,
            !valid ? COL_MUTED : (low ? COL_RED : COL_WHITE), 0);
        lv_obj_set_style_bg_color(ui_BatteryBar,
            low ? COL_RED : COL_WHITE, LV_PART_INDICATOR);

        /* Wi-Fi icon */
        WifiUiState ws = device->Wf.state();
        if (ws == WifiUiState::Off) {
            lv_obj_add_flag(ui_WifiBar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(ui_WifiBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(ui_WifiBar, wifi_color(ws), 0);
        }

        /* Bluetooth icon */
        BleUiState bs = device->Ble.state();
        if (bs == BleUiState::Off) {
            lv_obj_add_flag(ui_BtBar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(ui_BtBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(ui_BtBar, ble_color(bs), 0);
        }

        /* Control-pad shortcuts mirror the real service state. */
        set_button_bg(ui_ButtonWifi, wifi_button_color(ws));
        set_button_bg(ui_ButtonBle, ble_button_color(bs));
        set_button_bg(ui_ButtonInfos, lv_color_hex(0xA6B4CD));

        if (_device_status.autoScreenOff)
            lv_obj_add_state(ui_ButtonAutoScreenOff, LV_STATE_CHECKED | LV_STATE_FOCUSED);
        else
            lv_obj_clear_state(ui_ButtonAutoScreenOff, LV_STATE_CHECKED | LV_STATE_FOCUSED);
    }


    void App_Launcher::scroll_event_cb(lv_event_t * e)
    {
        static int16_t last_x = 280;
        lv_obj_t * obj = lv_event_get_target(e);

        /* If enter pannel home */
        if ((lv_obj_get_scroll_x(obj) == 280) && (last_x != 280)) {
            StateBarPullUp_Animation(ui_ImgStateBar, 0);
        }
        /* If leave pannel home */
        else if ((lv_obj_get_scroll_x(obj) != 280) && (last_x == 280)) {
            StateBarDropDown_Animation(ui_ImgStateBar, 0);
        }

        last_x = lv_obj_get_scroll_x(obj);
    }


    void App_Launcher::button_event_cb(lv_event_t * e)
    {
        lv_obj_t * obj = lv_event_get_target(e);
        lv_event_code_t code = lv_event_get_code(e);

        /* Into that App */
        if (code == LV_EVENT_CLICKED) {
            int16_t idx = (lv_obj_get_index(obj) - 2) / 2;
            if (idx >= 0 && idx < _app.totalNum) {
                _app.selected = idx;
                _app.onCreate = true;
            }
        }
    }

    void App_Launcher::panel_control_pad_event_cb(lv_event_t * e)
    {
        lv_obj_t * obj = lv_event_get_target(e);
        lv_event_code_t code = lv_event_get_code(e);
        App_Launcher *self = (App_Launcher *)lv_event_get_user_data(e);
        CHAPPIE *device = self ? self->_device : nullptr;
        if (!device)
            return;

        /* If setting brightness */
        if (obj == ui_ArcBrightness) {
            if (code == LV_EVENT_VALUE_CHANGED) {
                uint8_t value = (uint8_t)lv_arc_get_value(obj);
                _device_status.brightness = value;
                device->Lcd.setBrightness(value);
                char bbuf[24];
                snprintf(bbuf, sizeof(bbuf), "BRIGHTNESS %u%%", (unsigned)((uint16_t)value * 100 / 255));
                lv_label_set_text(ui_LabelBrightness, bbuf);
            }
        }

        /* If clicked infos button */
        else if (obj == ui_ButtonInfos) {
            /* Go look for App setting */
            for (int i = 0; i < _app.totalNum; i++) {
                if (App::Register[i].appName() == "Settings") {
                    _app.selected = i;
                    _app.onCreate = true;
                    break;
                }
            }
        }

        /* If enable auto screen off */
        else if (obj == ui_ButtonAutoScreenOff) {
            _device_status.autoScreenOff = lv_obj_has_state(obj, LV_STATE_CHECKED);
            if (_device_status.autoScreenOff)
                lv_disp_trig_activity(NULL);
        }

        /* Wi-Fi shortcut: post a request, the state machine does the work */
        else if (obj == ui_ButtonWifi) {
            device->Wf.requestToggle();
            lv_disp_trig_activity(NULL);
        }

        /* Bluetooth shortcut */
        else if (obj == ui_ButtonBle) {
            device->Ble.requestToggle();
            lv_disp_trig_activity(NULL);
        }
    }


    void App_Launcher::sleep_mode()
    {
        /* Wake on button B (GPIO0) or touch INT (GPIO21). */
        gpio_reset_pin(GPIO_NUM_0);
        gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
        gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
        gpio_reset_pin(GPIO_NUM_21);
        gpio_set_direction(GPIO_NUM_21, GPIO_MODE_INPUT);
        gpio_wakeup_enable(GPIO_NUM_21, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();

        /* Hold until button released, but never wait forever. */
        uint32_t start = millis();
        while (gpio_get_level(GPIO_NUM_0) == 0 && (millis() - start) < 3000) {
            delay(10);
        }

        esp_light_sleep_start();

        /* Restore the debounced button pin and the touch controller. */
        _device->Button.B.begin();
    }

}
