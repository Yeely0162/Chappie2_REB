/**
 * @file ChappieUI.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-03-15
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "ChappieUI.h"

/* ------------------------------------------------------------------------- */
/* System power dialog                                                        */
/* ------------------------------------------------------------------------- */

namespace {

lv_obj_t *s_powerDialog = nullptr;
volatile uint8_t s_dialogAction = 0;   /* 0 none, 1 cancel, 2 power off */

void power_dialog_cb(lv_event_t *e)
{
    uintptr_t action = (uintptr_t)lv_event_get_user_data(e);
    s_dialogAction = (uint8_t)action;
}

lv_obj_t *make_dialog_button(lv_obj_t *parent, const char *text, lv_color_t bg,
                             lv_coord_t x, lv_align_t align, uint8_t action)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, (action == 2) ? 104 : 90, 34);
    lv_obj_align(btn, align, x, -16);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 9, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, power_dialog_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F4F7), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    return btn;
}

void show_power_dialog()
{
    s_dialogAction = 0;
    s_powerDialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_powerDialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_powerDialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_powerDialog, 165, 0);
    lv_obj_set_style_border_width(s_powerDialog, 0, 0);
    lv_obj_set_style_radius(s_powerDialog, 0, 0);
    lv_obj_set_style_pad_all(s_powerDialog, 0, 0);
    lv_obj_clear_flag(s_powerDialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_powerDialog, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_powerDialog);
    lv_obj_set_size(panel, 252, 154);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x151A20), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Power off");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F4F7), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 14);

    lv_obj_t *hint = lv_label_create(panel);
    lv_label_set_text(hint, "Stop all activity and power down?");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8A919B), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 18, 42);

    lv_obj_t *close = lv_btn_create(panel);
    lv_obj_set_size(close, 28, 28);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_add_event_cb(close, power_dialog_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x8A919B), 0);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_18, 0);
    lv_obj_center(close_label);

    lv_obj_t *cancel = make_dialog_button(panel, "Cancel", lv_color_hex(0x1F252D),
                                          -56, LV_ALIGN_BOTTOM_MID, 1);
    (void)cancel;
    lv_obj_t *power = make_dialog_button(panel, "Power off", lv_color_hex(0xC0392B),
                                         58, LV_ALIGN_BOTTOM_MID, 2);
    (void)power;
}

void destroy_power_dialog()
{
    if (s_powerDialog) {
        lv_obj_del(s_powerDialog);
        s_powerDialog = nullptr;
    }
    s_dialogAction = 0;
}

} // namespace

/* ------------------------------------------------------------------------- */

int CHAPPIEUI::begin()
{
    if (_inited) {
        UI_LOG("[ChappieUI] already inited\n");
        return -1;
    }

    /* Create bsp */
    _device = new CHAPPIE;
    if (_device == NULL) {
        UI_LOG("[ChappieUI] bsp create failed\n");
        return -1;
    }

    /* Create launcher */
    _launcher = new App::App_Launcher(_device);
    if (_launcher == NULL) {
        UI_LOG("[ChappieUI] Launcher create failed\n");
        delete _device;
        _device = nullptr;
        return -1;
    }

    _inited = true;

    /* Init device */
    _device->init();

    /* Init lvgl */
    _device->lvgl.init(&_device->Lcd, &_device->Tp);

    /* Start launcher */
    _launcher->onCreate();

    return 0;
}


void CHAPPIEUI::update()
{
    /* Services first: Wi-Fi/BLE/NTP/battery run in this task. */
    _device->update();

    /* A short press opens the system power dialog on any page. */
    if ((s_powerDialog == nullptr) && _device->Button.A.pressed()) {
        _device->lvgl.disable();
        show_power_dialog();
        _device->lvgl.enable();
    }

    /* While the dialog is open, B cancels it. Other apps keep their own B
     * handling when the dialog is closed. */
    if (s_powerDialog != nullptr) {
        /* B cancels. A is also treated as cancel so the power key can never
         * trigger an immediate shutdown by accident. */
        if (_device->Button.B.pressed() || _device->Button.A.pressed())
            s_dialogAction = 1;

        if (s_dialogAction == 1) {
            _device->lvgl.disable();
            destroy_power_dialog();
            _device->lvgl.enable();
        } else if (s_dialogAction == 2) {
            _device->lvgl.disable();
            destroy_power_dialog();
            _device->shutdown();   /* does not return when the rail is cut */
        }
    }

    _launcher->onLoop();
}
