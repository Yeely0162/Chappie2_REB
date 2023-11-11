#include "LEDS.h"

void panel_colorwheel_event_cb(lv_event_t * e);
void panel_Onbtn_event_cb(lv_event_t * e);

static lv_obj_t * color_label;
static lv_obj_t * ui_ScreenLEDS;
static lv_obj_t * colorwheel;
void LEDSPage(void)
{
    
    // lv_obj_t * Onbutton;
    ui_ScreenLEDS = lv_scr_act();
    lv_obj_clear_flag(ui_ScreenLEDS, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_width(ui_ScreenLEDS, 280);
    lv_obj_set_height(ui_ScreenLEDS, 240);
    lv_obj_set_style_bg_color(ui_ScreenLEDS, lv_color_hex(0x4D5B74), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenLEDS, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(ui_ScreenLEDS, LV_ALIGN_CENTER);
    
    color_label = lv_label_create(ui_ScreenLEDS);
    lv_obj_set_align(color_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(color_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(color_label, "Try dragging");

    colorwheel = lv_colorwheel_create(ui_ScreenLEDS, true);
    lv_obj_set_size(colorwheel ,200, 200);
    lv_obj_center(colorwheel);
    lv_obj_set_align(colorwheel, LV_ALIGN_CENTER);
    // button
    // Onbutton = lv_btn_create(ui_ScreenLEDS);
    // lv_obj_set_size(Onbutton, 80, 80);
    // lv_obj_set_style_radius(Onbutton, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_align(Onbutton, LV_ALIGN_CENTER);
    // lv_obj_set_style_bg_color(Onbutton, lv_color_hex(0xA6B4CD), LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_add_event_cb(Onbutton, panel_Onbtn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(colorwheel, panel_colorwheel_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
void panel_colorwheel_event_cb(lv_event_t * e){
    lv_obj_t * obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t *hex_value;
    lv_color_t color;
    if (code == LV_EVENT_VALUE_CHANGED) {
        color = lv_colorwheel_get_rgb(obj);
        uint32_t hex_value = lv_color_to32((lv_color16_t)color);
        lv_label_set_text_fmt(color_label, "Red:%d\n\nGreen:%d\n\nBlue:%d", color.ch.red, color.ch.green, color.ch.blue);
        FastLED.showColor(CRGB(color.ch.green,color.ch.red,color.ch.blue));
    }
}
void panel_Onbtn_event_cb(lv_event_t * e){
    lv_obj_t * obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
   switch(code) {
        case LV_EVENT_PRESSED:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xA6B4CD), LV_PART_MAIN | LV_STATE_DEFAULT);
            FastLED.showColor(CRGB::Black);
            break;
        case LV_EVENT_CLICKED:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x0093E9), LV_PART_MAIN | LV_STATE_DEFAULT);
            // 添加开关 代码
            FastLED.showColor(CRGB::Red, 5);
            break;
   }

}
void LEDS_Loop(){

}
void LEDS_DeInit(){
    

}