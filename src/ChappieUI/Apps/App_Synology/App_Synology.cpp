#if 1
#include "App_Synology.h"
#include "../../../ChappieBsp/Chappie.h"
#include "ArduinoJson.h"
#define InternetIONum 5
#define APIUpdateTime 2000 //ms
#define Ubuntu18 ResourcePool::GetFont("UbuntuBold18")
LV_IMG_DECLARE(ui_img_icon_Synology_png);
static std::string app_name = "Synology";
static CHAPPIE* device;
static lv_timer_t* _Info_update;
static LGFX_Sprite* screen;
static void Info_update(lv_timer_t * timer);
static int SidJson(String Json);
static void CPU_Net_RAM_Json(String Json);
static String HttpGetJson(String url);
static TaskHandle_t  API_Update_Task;
static WiFiClient client;
struct MeterInfo{
    lv_obj_t * METER;
    lv_meter_indicator_t* INDIC;
    int32_t Meter_val_old = 0;
};
static MeterInfo Meter1,Meter2,Meter;

static MeterInfo ArcCreate(lv_obj_t * obj,const char * text,int16_t value,int x);

struct SynologyConfig{
  int16_t SSHPort= 22;
  String  User  = "Chappie";
  String  Password  = "uUYVG<4n";
  String  Sid;
  bool  SidStatus = false;
  String SidAPI = "http://192.168.0.8:20203/webapi/auth.cgi?api=SYNO.API.Auth&version=3&method=login&account="+ User +"&passwd="+ Password +"";
  String CPUAPI = ("http://192.168.0.8:20203/webapi/entry.cgi?api=SYNO.Virtualization.Cluster&method=get_host&version=1&object_id=NOTE_ID&_sid=");
  String LogoutAPI = ("http://192.168.0.8:20203/webapi/entry.cgi?api=SYNO.API.Auth&version=6&method=logout&_sid=");
  String StorageAPI = ("http://192.168.0.8:20203/webapi/entry.cgi?api=SYNO.Core.System&type=storage&method=info&version=3&_sid=");
}SyConfig;
struct SynologyInfo{
    const char *  ServerName = "";
    int16_t CPUUse  = 1;
    int16_t RAMUse  = 1;
    int32_t Upload  = 1;
    int32_t Downlaod= 1;
    int8_t InternetIOStatus[InternetIONum]={0};
}SystemInfo;
struct {
    bool SIDStatu = false;
    bool TaskOnWork = false;
    bool TaskOnDestroy =false;
    int8_t SIDCode = 0;
}TaskStatus;

static lv_obj_t * ui_SyPageMain,*ui_SyPageSec;
static lv_obj_t * Uplabel,*Downlabel,*mask;
static lv_obj_t * net_chart;
static lv_obj_t * TimeLabel;
static lv_chart_series_t * ser1;
static lv_chart_series_t * ser2;
static int SynologyPad_Current = 0;

static std::array<_lv_obj_t**, 2> SynologyPad_List = {
    &ui_SyPageMain,
    &ui_SyPageSec,
};


// animation


//--------------page Main style-------------------
// lv_obj_t * ArcCreate(lv_obj_t * obj,const char * text,int16_t value,int x){
//     lv_obj_t * arc = lv_arc_create(obj);
//     lv_obj_set_size(arc, 110, 110);
//     lv_arc_set_rotation(arc, 135);
//     lv_arc_set_bg_angles(arc, 0, 270);
//     lv_obj_set_style_arc_color(arc,lv_color_hex(0x4C4C4C),LV_ARC_DRAW_PART_BACKGROUND);
//     lv_obj_set_style_arc_color(arc,lv_color_hex(0x53DE77),LV_PART_INDICATOR);
    
//     lv_arc_set_value(arc, value);   
//     lv_obj_set_align(arc,LV_ALIGN_TOP_LEFT);
//     lv_obj_set_x(arc,20+x);
//     lv_obj_set_y(arc,10);
    
//     lv_obj_t * tvalue= lv_label_create(arc);
//     lv_label_set_text_fmt(tvalue,"%d%%",value);
//     lv_obj_set_style_text_font(tvalue,ResourcePool::GetFont("UbuntuBold18"),0);
//     lv_obj_set_style_text_color(tvalue,lv_color_hex(0xFFFFFF),0);
//     lv_obj_set_align(tvalue,LV_ALIGN_CENTER);
//     lv_obj_t * t = lv_label_create(arc);
//     lv_obj_set_style_text_color(t,lv_color_hex(0xFFFFFF),0);
//     lv_label_set_text(t,text);
//     lv_obj_set_align(t,LV_ALIGN_BOTTOM_MID);
//     lv_obj_set_y(t,-10);
    
//     lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
//     lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
//     return arc;
// }
static void set_meter_cpu_value(void* indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter1.METER,Meter1.INDIC, v);
}
static void set_meter_ram_value(void* indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter2.METER,Meter2.INDIC, v);
}
static MeterInfo ArcCreate(lv_obj_t * obj,const char * text,int16_t value,int x){
    Meter.METER = lv_meter_create(obj);
    lv_obj_remove_style(Meter.METER, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(Meter.METER, 0, 0);
    lv_obj_set_style_border_opa(Meter.METER, 0, 0);
    lv_obj_set_style_radius(Meter.METER,1,0);
    lv_meter_scale_t* scale_min1 = lv_meter_add_scale(Meter.METER);
    lv_obj_set_size(Meter.METER, 150, 150);
    lv_obj_set_pos(Meter.METER, 0, 0);

    lv_meter_set_scale_ticks(Meter.METER, scale_min1, 50, 2, 10, lv_color_hex(0x4C4C4C));
    lv_meter_set_scale_range(Meter.METER, scale_min1, 0, 100, 270, 135);

    Meter.INDIC = lv_meter_add_scale_lines(Meter.METER, scale_min1, lv_color_hex(0x85FFBD), lv_color_hex(0xFF6E3B),false,0);
    lv_meter_set_indicator_end_value(Meter.METER, (lv_meter_indicator_t*)Meter.INDIC, value);
    lv_obj_set_align(Meter.METER,LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(Meter.METER,x);
    lv_obj_set_y(Meter.METER,-3);


    lv_obj_t * tvalue= lv_label_create(Meter.METER);
    lv_label_set_text_fmt(tvalue,"%d%%",value);
    lv_obj_set_style_text_font(tvalue,Ubuntu18,0);
    lv_obj_set_style_text_color(tvalue,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_align(tvalue,LV_ALIGN_CENTER);
    
    lv_obj_t * t = lv_label_create(Meter.METER);
    lv_obj_set_style_text_color(t,lv_color_hex(0xFFFFFF),0);
    lv_label_set_text(t,text);
    lv_obj_set_y(t,-10);
    lv_obj_set_align(t,LV_ALIGN_BOTTOM_MID);
    
    return Meter;
}
void Net_UpDownload_box(lv_obj_t * obj){
    lv_obj_t * UpdownBox = lv_obj_create(obj);
    lv_obj_set_size(UpdownBox,60,100);
    lv_obj_set_align(UpdownBox,LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_bg_opa(UpdownBox,150,0);
    lv_obj_set_x(UpdownBox,6);
    lv_obj_set_y(UpdownBox,-10);
    lv_obj_set_style_bg_color(UpdownBox,lv_color_hex(0x4C4C4C),0);
    lv_obj_set_style_border_opa(UpdownBox,0,0);
    lv_obj_clear_flag(UpdownBox, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_opa(UpdownBox,150,0);
    //up
    lv_obj_t * UpIcon =lv_label_create(UpdownBox);
    lv_label_set_text(UpIcon,LV_SYMBOL_UP);
    lv_obj_set_align(UpIcon,LV_ALIGN_TOP_MID);
    lv_obj_set_y(UpIcon,-15);
    lv_obj_set_style_text_color(UpIcon,lv_color_hex(0x85FFBD),0);
    
    Uplabel =lv_label_create(UpdownBox);
    lv_label_set_text_fmt(Uplabel,"%.2f",98.21);
    lv_obj_set_y(Uplabel,-23);
    lv_obj_set_style_text_color(Uplabel,lv_color_hex(0x85FFBD),0);
    lv_obj_set_align(Uplabel,LV_ALIGN_CENTER);
    //down
    lv_obj_t * DownIcon =lv_label_create(UpdownBox);
    lv_label_set_text(DownIcon,LV_SYMBOL_DOWN);
    lv_obj_set_align(DownIcon,LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(DownIcon,+15);
    lv_obj_set_style_text_color(DownIcon,lv_color_hex(0xFFFB7D),0);

    Downlabel =lv_label_create(UpdownBox);
    lv_label_set_text_fmt(Downlabel,"%d",98);
    lv_obj_set_style_text_color(Downlabel,lv_color_hex(0xFFFB7D),0);
    lv_obj_set_y(Downlabel,+23);
    
    lv_obj_set_align(Downlabel,LV_ALIGN_CENTER);
    mask = lv_label_create(UpdownBox);
    lv_label_set_long_mode(mask, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(mask, true);
    lv_obj_set_align(mask,LV_ALIGN_CENTER);
    lv_label_set_text(mask,"#85FFBD KB# #FFFFFF |# #FFFB7D MB#");
     
}
static void draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);

    /*Add the faded area before the lines are drawn*/
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_ITEMS) {
        if(!dsc->p1 || !dsc->p2) return;

        /*Add a line mask that keeps the area below the line*/
        lv_draw_mask_line_param_t line_mask_param;
        lv_draw_mask_line_points_init(&line_mask_param, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y, LV_DRAW_MASK_LINE_SIDE_BOTTOM);
        int16_t line_mask_id = lv_draw_mask_add(&line_mask_param, NULL);

        /*Add a fade effect: transparent bottom covering top*/
        lv_coord_t h = lv_obj_get_height(obj);
        lv_draw_mask_fade_param_t fade_mask_param;
        lv_draw_mask_fade_init(&fade_mask_param, &obj->coords, LV_OPA_COVER, obj->coords.y1 + h / 8, LV_OPA_TRANSP,obj->coords.y2);
        int16_t fade_mask_id = lv_draw_mask_add(&fade_mask_param, NULL);

        /*Draw a rectangle that will be affected by the mask*/
        lv_draw_rect_dsc_t draw_rect_dsc;
        lv_draw_rect_dsc_init(&draw_rect_dsc);
        draw_rect_dsc.bg_opa = LV_OPA_20;
        draw_rect_dsc.bg_color = dsc->line_dsc->color;

        lv_area_t a;
        a.x1 = dsc->p1->x;
        a.x2 = dsc->p2->x - 1;
        a.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
        a.y2 = obj->coords.y2;
        lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);

        /*Remove the masks*/
        lv_draw_mask_free_param(&line_mask_param);
        lv_draw_mask_free_param(&fade_mask_param);
        lv_draw_mask_remove_id(line_mask_id);
        lv_draw_mask_remove_id(fade_mask_id);
    }
    /*Hook the division lines too*/
    else if(dsc->part == LV_PART_MAIN) {
        if(dsc->line_dsc == NULL || dsc->p1 == NULL || dsc->p2 == NULL) return;

        /*Vertical line*/
        if(dsc->p1->x == dsc->p2->x) {
            dsc->line_dsc->color  = lv_palette_lighten(LV_PALETTE_GREY, 1);
            if(dsc->id == 3) {
                dsc->line_dsc->width  = 2;
                dsc->line_dsc->dash_gap  = 0;
                dsc->line_dsc->dash_width  = 0;
            }
            else {
                dsc->line_dsc->width = 1;
                dsc->line_dsc->dash_gap  = 6;
                dsc->line_dsc->dash_width  = 6;
            }
        }
        /*Horizontal line*/
        else {
            if(dsc->id == 2) {
                dsc->line_dsc->width  = 2;
                dsc->line_dsc->dash_gap  = 0;
                dsc->line_dsc->dash_width  = 0;
            }
            else {
                dsc->line_dsc->width = 2;
                dsc->line_dsc->dash_gap  = 6;
                dsc->line_dsc->dash_width  = 6;
            }

            if(dsc->id == 1  || dsc->id == 3) {
                dsc->line_dsc->color  = lv_palette_main(LV_PALETTE_GREEN);
            } else {
                dsc->line_dsc->color  = lv_palette_lighten(LV_PALETTE_GREY, 1);
            }
        }
    }
}

void Network_Charts(lv_obj_t* obj)
{
    /*Create a net_chart*/
    net_chart = lv_chart_create(obj);
    lv_obj_set_size(net_chart, 200, 100);
    lv_obj_center(net_chart);
    lv_obj_set_x(net_chart,-6);
    lv_obj_set_y(net_chart,-10);
    lv_chart_set_type(net_chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
    lv_obj_set_align(net_chart,LV_ALIGN_BOTTOM_RIGHT);
    lv_chart_set_div_line_count(net_chart, 0, 0);
    lv_chart_set_range(net_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_add_event_cb(net_chart, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_chart_set_update_mode(net_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(net_chart,lv_color_hex(0x4C4C4C),0);
    lv_obj_set_style_bg_opa(net_chart,150,0);
    lv_obj_set_style_border_opa(net_chart,0,0);

    /*Add two data series*/
    ser1 = lv_chart_add_series(net_chart, lv_color_hex(0x85FFBD), LV_CHART_AXIS_PRIMARY_Y);
    ser2 = lv_chart_add_series(net_chart, lv_color_hex(0xFFFB7D), LV_CHART_AXIS_SECONDARY_Y);

}

void Content_Update(MeterInfo meterobj,int16_t value,bool check){
    lv_anim_t anim;
     int val_old = meterobj.Meter_val_old;
    lv_anim_init(&anim);
    if (check){
        lv_anim_set_exec_cb(&anim, set_meter_cpu_value);
    }else{
        lv_anim_set_exec_cb(&anim, set_meter_ram_value);
    }

    if(val_old == 0){
        
        lv_anim_set_values(&anim, 0, value);

    }else if (val_old > value){

        lv_anim_set_values(&anim, value, val_old);
    
    }else if (val_old < value){

        lv_anim_set_values(&anim, val_old, value);
    
    }
    
    lv_anim_set_time(&anim, 200); /*2 sec for 1 turn of the minute hand (1 hour)*/
    lv_anim_set_var(&anim, meterobj.INDIC);
    lv_anim_start(&anim);

    // lv_meter_set_indicator_end_value(obj,indic, value);
    lv_label_set_text_fmt(lv_obj_get_child(meterobj.METER,0),"%d %%",value);
}

float NetworkSpeed(int32_t bytes) {
    if (bytes < 1024 * 1024) {
        return  float(bytes / 1000.0);
    } else if (bytes < 1000 * 1000 * 1000) { //MB
        return  float(bytes / 1000000.0);
    } 
}
void SpeedSymbol(int32_t v1,int32_t v2){
    bool a = false;
    if ( v1 && v2 < 1024 * 1024  ){
        lv_label_set_text(mask,"#85FFBD KB# #FFFFFF |# #FFFB7D KB#");
    }
    else if ( v1 >= 1024 * 1024  && v2 >= 1024 * 1024)
    {
        lv_label_set_text(mask,"#85FFBD MB# #FFFFFF |# #FFFB7D MB#");
        a = true;
    }
    else if ( v1  > 1024 * 1024 && v2 < 1024 * 1024)
    {
        lv_label_set_text(mask,"#85FFBD MB# #FFFFFF |# #FFFB7D KB#");
        a = true;
    }
    else if ( v1  < 1024 * 1024 && v2 > 1024 * 1024)
    {
        lv_label_set_text(mask,"#85FFBD KB# #FFFFFF |# #FFFB7D MB#");
        a = true;
    }
    if (a){
        lv_chart_set_next_value(net_chart, ser1,int32_t(v1/1000.0 * 1000.0));
        lv_chart_set_next_value(net_chart, ser2,int32_t(v2/1000.0 * 1000.0));
    }else{
        lv_chart_set_next_value(net_chart, ser1,int32_t(v1/1000.0));
        lv_chart_set_next_value(net_chart, ser2,int32_t(v2/1000.0));
    }

}
void Net_Update(lv_obj_t * net1,lv_obj_t * net2,int32_t value1,int32_t value2){
    
    SpeedSymbol(value1,value2);
    lv_label_set_text_fmt(net1,"%.2f",NetworkSpeed(value1)); //Upload
    lv_label_set_text_fmt(net2,"%.2f",NetworkSpeed(value2)); //Download
 

}

//--------------page Main style-------------------

//-----------------------Function-------------------
void UserLogout(){
    HttpGetJson(SyConfig.LogoutAPI + SyConfig.Sid);
    TaskStatus.SIDStatu =false;
}
// Json analysis
static int SidJson(String Json){
    StaticJsonDocument<384> doc;
    deserializeJson(doc, Json);
    int code = 1;
    // const char * isSucces  =doc["success"];
    // if (strcmp(isSucces,"false") == 0){
    //     code = doc["error"]["code"];
    //     TaskStatus.SIDCode = code;
    // }
    SyConfig.Sid = doc["data"]["sid"].as<String>();
    return code; 
}
static void CPU_Net_RAM_Json(String Json){
    StaticJsonDocument<1024> doc;
    deserializeJson(doc,Json);
    JsonObject hosts_0 = doc["data"]["hosts"][0];
    SystemInfo.CPUUse  = hosts_0["cpu_usage"];
    SystemInfo.RAMUse  = hosts_0["ram_usage"];
    
    printf("\nCPU:%d, RAM:%d\n",SystemInfo.CPUUse,SystemInfo.RAMUse);
    int i =0;
    SystemInfo.Upload = 0,
    SystemInfo.Downlaod = 0;
    for (JsonObject data_hosts_0_nic : hosts_0["nics"].as<JsonArray>()) {
        SystemInfo.Downlaod += (int32_t)data_hosts_0_nic["rx"]; // 684, 0
        if (i < InternetIONum){
            String s =data_hosts_0_nic["status"]; // "connected", "disconnected"
            if (s == "connected"){
                SystemInfo.InternetIOStatus[i] = 1;
            }else{
                SystemInfo.InternetIOStatus[i] = 0;
            }
        }
        SystemInfo.Upload += (int32_t)data_hosts_0_nic["tx"]; // 325, 0
        i++;
    }

}
static String HttpGetJson(String url){
    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if ( code == HTTP_CODE_OK){
        String json = http.getString();
        printf("Json:%s",json.c_str());
        return json;
    }else{
        http.end();
        return "None";
    }
}
static void ui_event_SyPad(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        // printf("->\n");
        if (SynologyPad_Current < (SynologyPad_List.size() - 1)) {
            SynologyPad_Current++;
            lv_scr_load_anim(*SynologyPad_List[SynologyPad_Current], LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            // _watch_time_update(_timer_watch_time_update);
        }
    }
    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        // printf("<-\n");
        SynologyPad_Current--;
        if (SynologyPad_Current < 0) {
            SynologyPad_Current = 0;
        }
        else {
            lv_scr_load_anim(*SynologyPad_List[SynologyPad_Current], LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            // _watch_time_update(_timer_watch_time_update);
        }
    }
}

//-----------------------Function-------------------

//-----------------------Task-------------------
void RequestTask(void *arg)
{
    
    TaskStatus.TaskOnWork = true;
    while (true)
    {
        /* code */
        if(!TaskStatus.SIDStatu && device->Wf.isConnected()){
            TaskStatus.SIDCode  =  SidJson(HttpGetJson(SyConfig.SidAPI));
            TaskStatus.SIDStatu = true;
        }
        if (TaskStatus.SIDStatu)
        {
            CPU_Net_RAM_Json(HttpGetJson(SyConfig.CPUAPI + SyConfig.Sid));           
        }
        vTaskDelay(APIUpdateTime);
    }
    TaskStatus.TaskOnWork = false;
    vTaskDelete(API_Update_Task);
}
static void Info_update(lv_timer_t * timer){
    static char label_buffer[10];
    static I2C_BM8563_TimeTypeDef rtc_time;
    static int16_t hours, minutes, seconds;
    device->Rtc.getTime(&rtc_time);
    snprintf(label_buffer, 10, "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    lv_label_set_text(TimeLabel, label_buffer);
    Content_Update(Meter1,SystemInfo.CPUUse,true);
    Content_Update(Meter2,SystemInfo.RAMUse,false);
    Net_Update(Uplabel,Downlabel,SystemInfo.Upload,SystemInfo.Downlaod);
}
//-----------------------Task-------------------


void ScreenMain(){
    ui_SyPageMain = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SyPageMain, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_SyPageMain, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(ui_SyPageMain,280);
    lv_obj_set_height(ui_SyPageMain,240);
    lv_obj_set_style_bg_opa(ui_SyPageMain, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(ui_SyPageMain, LV_ALIGN_CENTER);
    Meter1 = ArcCreate(ui_SyPageMain,"CPU",1,-2);
    Meter2 = ArcCreate(ui_SyPageMain,"RAM",1,135);
    Network_Charts(ui_SyPageMain);
    Net_UpDownload_box(ui_SyPageMain);
    TimeLabel = lv_label_create(ui_SyPageMain);
    lv_obj_set_align(TimeLabel,LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_font(TimeLabel,Ubuntu18,0);
    lv_label_set_text(TimeLabel,"Init...");
    lv_obj_set_style_text_opa(TimeLabel,80,0);
    
}

void ScreenSec(){
    ui_SyPageSec = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SyPageSec, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_SyPageSec, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(ui_SyPageSec,280);
    lv_obj_set_height(ui_SyPageSec,240);
    lv_obj_set_style_bg_opa(ui_SyPageSec, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(ui_SyPageSec, LV_ALIGN_CENTER);

    lv_obj_t * lmeter = lv_meter_create(ui_SyPageSec);
    lv_obj_remove_style(lmeter, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(lmeter, 0, 0);
    lv_obj_set_style_border_opa(lmeter, 0, 0);
    lv_obj_set_style_radius(lmeter,1,0);
    lv_meter_scale_t* scale_min1 = lv_meter_add_scale(lmeter);
    lv_obj_set_size(lmeter, 150, 150);
    lv_obj_set_pos(lmeter, 0, 0);

    lv_meter_set_scale_ticks(lmeter, scale_min1, 50, 2, 10, lv_color_hex(0x85FFBD));
    lv_meter_set_scale_range(lmeter, scale_min1, 0, 100, 270, 135);

    lv_meter_indicator_t* indic;
    indic = lv_meter_add_scale_lines(lmeter, scale_min1, lv_color_hex(0x85FFBD), lv_color_hex(0xFF7F3B),false,0);
    lv_meter_set_indicator_end_value(lmeter, (lv_meter_indicator_t*)indic, 3);
    lv_obj_set_align(lmeter,LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(lmeter,50);
    lv_obj_set_y(lmeter,-3);
    

    // lv_obj_t * arc = lv_arc_create(ui_SyPageSec);
    // lv_obj_set_size(arc, 150, 150);
    // lv_arc_set_rotation(arc, 135);
    // lv_arc_set_bg_angles(arc, 0, 270);
    // lv_arc_set_value(arc, 40);
    // lv_obj_center(arc);
    
}

void ScreenInit(){
    ScreenMain();
    ScreenSec();
}




//

void ScreenStorage(){
    // device->Wf.requesturl(SyConfig.domain,SyConfig.Sport,SyConfig.CPUAPI);
}
// Timer

void ErrorPage(const char *text){
    screen = new LGFX_Sprite(&device->Lcd);
    screen->fillScreen(TFT_BLACK);
    screen->setTextSize(2);
    screen->setTextColor(TFT_ORANGE);
    screen->setCursor(0, 30);
    screen->printf(text);
    screen->printf(" > (=.=) <\n");
    screen->pushSprite(0, 0);
}

namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_Synology_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_Synology_appIcon()
    {
        return (void *)&ui_img_icon_Synology_png;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_Synology_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Synology_appName().c_str());
        // UI Init
        ScreenInit();
        for (auto i : SynologyPad_List) {
            lv_obj_add_event_cb(*i, ui_event_SyPad, LV_EVENT_ALL, NULL);
        }
        SynologyPad_Current = 0;
        lv_scr_load_anim(*SynologyPad_List[SynologyPad_Current], LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);

        _Info_update = lv_timer_create(Info_update, APIUpdateTime + 500,NULL);
        Info_update(_Info_update);     // status bar time update
        xTaskCreatePinnedToCore(RequestTask, "RequestTask", 1024 * 4,NULL, 1, &API_Update_Task, 1 );
        // switch (code)
        // {
        //     case 0:
        //         ErrorPage("NetWork Error or Other Error");
        //         break;
        //     case 1:
                
        //         break;
        //     case 100 ... 104:
        //         /* API problem */
        //         ErrorPage("API Error");
        //         break;
        //     case 105:
        //         /* Insufficient user privilege */
        //         ErrorPage("Insufficient user privilege");
        //         break;
        //     case 107:
        //         /* Multiple login detected */
        //         ErrorPage("Multiple login detected");
        //         break;
        //     case 119:
        //         /* Invalid session */
        //         ErrorPage("Invalid session");
        //         if(sizeof(SyConfig.Sid) > 5){
        //             UserLogout(); // 注销上一个Cookie
        //         }
        //         break;
        //     default:    
        //         break;
        // }
                
    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_Synology_onLoop()
    {
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_Synology_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Synology_appName().c_str());
        UserLogout();
        lv_timer_del(_Info_update);
        vTaskDelete(API_Update_Task);
        
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_Synology_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
