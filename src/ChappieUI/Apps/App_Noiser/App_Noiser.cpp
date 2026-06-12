#if 1
#include "App_Noiser.h"
#include "../../../ChappieBsp/Chappie.h"

static CHAPPIE * device;
static std::string app_name = "Noiser";


LV_IMG_DECLARE(ui_img_icon_ear_png);


#define MIC_BAR_WIDTH  14
#define MIC_BAR_HEIGHT 21
#define NUM_BARS 14
const uint16_t  _samples = 256;
int16_t* _vReal;
static float _smoothDb = 30.0f;
static int _noiseBarValue = 0;
static lv_obj_t * contentbox;
static lv_obj_t * decibel;
static lv_timer_t* _noiser_update;
static lv_timer_t* _time_update;
static void Noiser_update(lv_timer_t * timer);
static void Time_update(lv_timer_t * timer);
static lv_obj_t * timelabel;
static lv_obj_t *mic_bars[NUM_BARS];
static lv_obj_t * accept,*warming,*acceptIcon,*warmingIcon;
void vltext(lv_obj_t * obj,lv_align_t pos,lv_coord_t x,lv_coord_t y,const char * text,lv_color_t color = lv_color_hex(0xFFFFFF));

void create_mic_bars(lv_obj_t *obj) {
    for (int i = 0; i < NUM_BARS; i++) {
        mic_bars[i] = lv_bar_create(obj);
        lv_obj_align(mic_bars[i], LV_ALIGN_LEFT_MID, i * 20, 0);
        lv_obj_set_size(mic_bars[i], MIC_BAR_WIDTH, MIC_BAR_HEIGHT);
        lv_obj_set_style_radius(mic_bars[i],4,0);
        lv_obj_set_style_bg_color(mic_bars[i], lv_color_hex(0x4C4C4C), LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(mic_bars[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    }
    lv_obj_t *MinV = lv_label_create(obj);
    lv_obj_t *MidV = lv_label_create(obj);
    lv_obj_t *MaxV = lv_label_create(obj);
    vltext(MinV,LV_ALIGN_LEFT_MID,18,20,"30",lv_color_hex(0x9B9AA0));
    vltext(MidV,LV_ALIGN_CENTER,26,20,"80",lv_color_hex(0x9B9AA0));
    vltext(MaxV,LV_ALIGN_RIGHT_MID,-10,20,"120",lv_color_hex(0x9B9AA0));
}
void vltext(lv_obj_t * obj,lv_align_t pos,lv_coord_t x,lv_coord_t y,const char * text,lv_color_t color){
    lv_obj_set_align(obj, pos);
    lv_obj_set_x(obj,x);
    lv_obj_set_y(obj,y);
    lv_label_set_text(obj,text);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void StatusNoise(bool value ){
    if (value){
        lv_obj_clear_flag(accept, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(acceptIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(warming, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(warmingIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(contentbox, lv_color_hex(0x53DE77), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else{
        lv_obj_clear_flag(warming, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(warmingIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(accept, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(acceptIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(contentbox, lv_color_hex(0xF7E545), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}
void NoiserPage(void)
{
    _vReal      = new int16_t[_samples];
    static lv_obj_t *UI_Noiser;
    UI_Noiser = lv_scr_act();
    lv_obj_clear_flag(UI_Noiser, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_width(UI_Noiser, 280);
    lv_obj_set_height(UI_Noiser, 240);
    lv_obj_set_style_bg_color(UI_Noiser, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(UI_Noiser, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(UI_Noiser, LV_ALIGN_CENTER);
    create_mic_bars(UI_Noiser);

    lv_obj_t * title = lv_label_create(UI_Noiser);
    vltext(title,LV_ALIGN_TOP_LEFT,15,10,"Noiser");
    lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);

    timelabel = lv_label_create(UI_Noiser);
    vltext(timelabel,LV_ALIGN_TOP_RIGHT,-10,10,"10:00");
    lv_obj_set_style_text_font(timelabel,&lv_font_montserrat_24,0);

    decibel = lv_label_create(UI_Noiser);
    lv_obj_set_style_text_font(decibel,ResourcePool::GetFont("bahnschrift_65"),0);
    vltext(decibel,LV_ALIGN_LEFT_MID,30,-50,"58");


    lv_obj_t * dBlabel = lv_label_create(UI_Noiser);
    vltext(dBlabel,LV_ALIGN_CENTER,-3,-40,"dB");
    lv_obj_set_style_text_font(dBlabel,&lv_font_montserrat_24,0);


    contentbox = lv_obj_create(UI_Noiser);
	lv_obj_set_pos(contentbox, 0, 160);
	lv_obj_set_size(contentbox, 280, 94);
    lv_obj_set_style_border_opa(contentbox,0,0);
    lv_obj_set_style_bg_color(contentbox, lv_color_hex(0x53DE77), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    accept = lv_label_create(contentbox);
    vltext(accept,LV_ALIGN_CENTER,15,0,"Acceptable",lv_color_hex(0x000000));
    lv_obj_set_style_text_font(accept,&lv_font_montserrat_24,0);
    acceptIcon = lv_img_create(contentbox);
    lv_obj_align(acceptIcon,LV_ALIGN_CENTER,-80,0);
    lv_img_set_src(acceptIcon, ResourcePool::GetImage("ok_dark"));

    warming = lv_label_create(contentbox);
    vltext(warming,LV_ALIGN_CENTER,15,0,"Wariming",lv_color_hex(0x000000));
    lv_obj_set_style_text_font(warming,&lv_font_montserrat_24,0);
    warmingIcon = lv_img_create(contentbox);
    lv_obj_align(warmingIcon,LV_ALIGN_CENTER,-80,0);
    lv_img_set_src(warmingIcon, ResourcePool::GetImage("warming_dark"));
    lv_obj_add_flag(warmingIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(warming, LV_OBJ_FLAG_HIDDEN);

    device->Mic.begin();
    _noiser_update = lv_timer_create(Noiser_update, 300,NULL);
    Noiser_update(_noiser_update);
    _time_update = lv_timer_create(Time_update, 500,NULL);
    Time_update(_time_update);
}


void Time_update(lv_timer_t * timer){
    static I2C_BM8563_TimeTypeDef rtc_time;
    device->Rtc.getTime(&rtc_time);
    static char label_buffer[10];
    snprintf(label_buffer, 10, "%d:%02d", rtc_time.hours, rtc_time.minutes);
    lv_label_set_text(timelabel,label_buffer);

    
}

// =====================================================================
//  Fixed noise calculation algorithm
//  MSM261D4030H1CPM PDM MEMS microphone
//  Sensitivity: -26 dBFS at 94 dB SPL (1 kHz, 1 Pa)
//  SPL formula: dBSPL = 20 * log10(RMS/32767) + 120
// =====================================================================
void Noiser_update(lv_timer_t * timer)
{
    if (device->Mic.record(_vReal, _samples)) {
        // 1) Remove DC offset
        int32_t sum = 0;
        for (int i = 0; i < _samples; i++) sum += _vReal[i];
        float dc = (float)sum / (float)_samples;

        // 2) Calculate RMS of AC component (actual sound)
        float sumSq = 0.0f;
        for (int i = 0; i < _samples; i++) {
            float ac = (float)_vReal[i] - dc;
            sumSq += ac * ac;
        }
        float rms = sqrtf(sumSq / (float)_samples);
        if (rms < 1.0f) rms = 1.0f;

        // 3) Convert to dB SPL
        // At 94 dB SPL, output is at -26 dBFS → RMS = 1638
        // dBSPL = 20*log10(RMS/32767) + 120
        float db = 20.0f * log10f(rms / 32767.0f) + 120.0f;
        if (db < 30.0f) db = 30.0f;
        if (db > 120.0f) db = 120.0f;

        // 4) Smooth for stable display (70% old + 30% new)
        _smoothDb = _smoothDb * 0.7f + db * 0.3f;
        _noiseBarValue = (int)_smoothDb;
    } else {
        _noiseBarValue = 0;
    }

    int soundLevel = _noiseBarValue;
    for (int i = 0; i < NUM_BARS; i++) {
        int level =(int)soundLevel;
        if( i <= ( level/ 10 ) ){
            if(  level < 75 ){
                StatusNoise(true);
                lv_obj_set_style_bg_color(mic_bars[i],lv_color_hex(0x53DE77),0);
                lv_obj_set_style_bg_opa(mic_bars[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            else{
                StatusNoise(false);
                lv_obj_set_style_bg_color(mic_bars[i],lv_color_hex(0xF7E545),0);
                lv_obj_set_style_bg_opa(mic_bars[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
        else{
            lv_obj_set_style_bg_color(mic_bars[i],lv_color_hex(0x4C4C4C),0);
            lv_obj_set_style_bg_opa(mic_bars[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    static char decibel_buffer[8];
    snprintf(decibel_buffer, 8,"%d",soundLevel);
    lv_label_set_text(decibel, decibel_buffer);
}
void Noiser_deinit()
{
    lv_timer_del(_noiser_update);
    lv_timer_del(_time_update);
    delete [] _vReal;
}


namespace App {

    std::string App_Noiser_appName()
    {
        return app_name;
    }

    void* App_Noiser_appIcon()
    {
        return (void*) &ui_img_icon_ear_png;
    }

    void App_Noiser_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Noiser_appName().c_str());
        NoiserPage();
    }

    void App_Noiser_onLoop()
    {
    }

    void App_Noiser_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Noiser_appName().c_str());
        Noiser_deinit();
    }

    void App_Noiser_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
