#if 1
#include "App_Noiser.h"
#include "../../../ChappieBsp/Chappie.h"

static CHAPPIE * device;
static std::string app_name = "Noiser";


LV_IMG_DECLARE(ui_img_icon_ear_png);


// #include "arduinoFFT.h"
// static arduinoFFT  FFT;
#define MIC_BAR_WIDTH  14
#define MIC_BAR_HEIGHT 21
#define NUM_BARS 14
#define STANDER_VALUE 840
#define GAIN -25
// const double    samplingFrequency = 48000;
const uint16_t  _samples = 256; //This value MUST ALWAYS be a power of 2
static double* _vReal;
static int16_t* _rawData;
static lv_obj_t * contentbox;
static lv_obj_t * decibel; //decibel text
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
    _vReal      = new double[_samples];
    _rawData  = new int16_t[_samples];
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
    _noiser_update = lv_timer_create(Noiser_update, 300,NULL);
    Noiser_update(_noiser_update);     // status bar time update

    _time_update = lv_timer_create(Time_update, 1000,NULL);
    Time_update(_time_update);     // status bar time update
}


void Time_update(lv_timer_t * timer){
    static I2C_BM8563_TimeTypeDef rtc_time;
    device->Rtc.getTime(&rtc_time);
    static char label_buffer[10];
    snprintf(label_buffer, 10, "%d:%02d", rtc_time.hours, rtc_time.minutes);
    lv_label_set_text(timelabel,label_buffer);

    
}

double calculateDecibels(double *vReal, uint16_t samples) {
    double sum = 0.0;
    for (int i = 0; i < samples; i++){
        // printf("%.2f,",vReal[i]);
        double diff = abs(vReal[i] - STANDER_VALUE);
        sum += diff * diff;
    }
    double rms = sqrt(sum / (double)samples);
    double dB = 20 * log10(rms / 0.01f) + GAIN;

    return dB;
}
// double calculateDecibels(double *vReal, uint16_t samples) {
//     double sum = 0.0;
//     for (int i = 0; i < samples / 2; i++){
//         // printf("%.2f,",vReal[i]);
//         double diff = abs(vReal[i] - STANDER_VALUE);
//         sum += diff * diff;
//     }
//     double rms = sqrt(sum / (double)samples);
//     double dB = 20 * log10(rms / 0.001f) + GAIN;

//     return dB;
// }
void Noiser_update(lv_timer_t * timer)
{

    // 麦克风型号为: MSM261D4030H1CPM
    device->Mic.record(_rawData, _samples); //录制声音
    while (device->Mic.isRecording()); //当录制完成时
    
    /* Copy data */
    for (int i = 0; i < _samples; i++) {
        _vReal[i] = (double)_rawData[i]; //将数据丢入实部数组
        // printf("%.2f,",_vReal[i]);
    }
    
    /* FFT */
    // ArduinoFFT库 
    // FFT = arduinoFFT(_vReal, _vImag, _samples, samplingFrequency);
    // FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    // FFT.Compute(FFT_FORWARD);
    // FFT.ComplexToMagnitude();
    // 
    // FFT.Windowing(FFT_WIN_TYP_HANN, FFT_FORWARD);
    // FFT.Compute(FFT_FORWARD);
    // FFT.ComplexToMagnitude();
    
    // // 计算噪声分贝级别
    
    int16_t soundLevel = calculateDecibels(_vReal, _samples);
    for (int i = 0; i < NUM_BARS; i++) {
        if( i <= (soundLevel / 10 ) ){
            if(  soundLevel < 75 ){
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
    delete _rawData;
}


namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_Noiser_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_Noiser_appIcon()
    {
        // return NULL;
        return (void*) &ui_img_icon_ear_png;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_Noiser_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Noiser_appName().c_str());

        /*Create an Arc*/
        NoiserPage();
        // Noiser_Loop();

    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_Noiser_onLoop()
    {
        // Noiser_Loop();
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_Noiser_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Noiser_appName().c_str());
        Noiser_deinit();
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_Noiser_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
