#if 1
#include "App_Synology.h"
#include "../../../ChappieBsp/Chappie.h"
#include "ArduinoJson.h"
#define InternetIONum 5
#define APIUpdateTime 2000 //ms
#define SystemInfoUpdateTime 1000 *5
#define Ubuntu18 ResourcePool::GetFont("UbuntuBold18")
#define Font12 &lv_font_montserrat_12
#define Font24 &lv_font_montserrat_24
LV_IMG_DECLARE(ui_img_icon_Synology_png);
static std::string app_name = "Synology";
static CHAPPIE* device;
static lv_timer_t* _Info_update;
static LGFX_Sprite* screen;
void Info_update(lv_timer_t * timer);
int SidJson(String Json);
void CPU_Net_RAM_Json(String Json);
String HttpGetJson(String url);
TaskHandle_t  API_Update_Task,System_Info_Task;
static lv_anim_t anim;


struct MeterInfo{
    lv_obj_t * METER;
    lv_meter_indicator_t* INDIC;
    int16_t Meter_val_old = 0;
};
MeterInfo Meter,Meter1,Meter2;
void SystemInfoTask(void *arg);
void RequestTask(void *arg);
MeterInfo ArcCreate(lv_obj_t * obj,const char * text,int16_t value,int x);
//---------page2 define-----------
lv_obj_t * Volume1,*Volume2,*CpuBOX,*RamBOX,*TempBox,*WorkBox,*DeviceBOX;
lv_obj_t * InfoBox;
//---------page2 define-----------
struct SynologyConfig{
  int16_t SSHPort= 22;
  String  User  = "XXXXXXXXXXXXX";
  String  Password  = "XXXXXXXXXXXX";
  String  Sid;
  bool  SidStatus = false;
  String SidAPI = "xxxxxxxxxxxx/webapi/auth.cgi?api=SYNO.API.Auth&version=3&method=login&account="+ User +"&passwd="+ Password +"";
  String CPUAPI = ("xxxxxxxxxxxx/webapi/entry.cgi?api=SYNO.Virtualization.Cluster&method=get_host&version=1&object_id=NOTE_ID&_sid=");
  String LogoutAPI = ("xxxxxxxxxxxx/webapi/entry.cgi?api=SYNO.API.Auth&version=6&method=logout&_sid=");
  String StorageAPI = ("xxxxxxxxxxxx/webapi/entry.cgi?api=SYNO.Core.System&type=storage&method=info&version=3&_sid=");
  String SystemInfoAPI = "xxxxxxxxxxxx/webapi/entry.cgi?api=SYNO.Core.System&method=info&version=3&_sid=";
}SyConfig;
struct SynologyInfo{
    String ServerName  = "non";
    String DeviceName  ="non";
    String FirmwareVer = "non";
    String StorageName[2] ={"non"};
    String CPUType ="non";
    String UpTime ="non";
    int8_t  CPUCoreNum = 0;
    int8_t InternetIOStatus[InternetIONum]={0};
    int16_t CPUUse  = 1;
    int16_t RAMGB   = 0;
    int16_t RAMUse  = 1;
    int16_t SystemTemp = 1;
    int32_t Upload  = 1;
    int32_t Downlaod= 1;
    uint64_t Volume[2][2]={0}; // 0:total 1:used
    bool iSTempWarn = false; 
}SystemInfo;
struct {
    bool SIDStatu = false;
    bool TaskOnWork = false;
    bool TaskOnDestroy =false;
    int8_t SIDCode = 0;
}TaskStatus;

lv_obj_t * ui_SyPageMain,*ui_SyPageSec;
lv_obj_t * Uplabel,*Downlabel,*mask;
lv_obj_t * net_chart;
lv_obj_t * TimeLabel;
lv_chart_series_t * ser1;
lv_chart_series_t * ser2;
int SynologyPad_Current = 0;

static std::array<_lv_obj_t**, 2> SynologyPad_List = {
    &ui_SyPageMain,
    &ui_SyPageSec,
};


//-------------animation---------------
static void set_meter_cpu_value(void* indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter1.METER,Meter1.INDIC, v);
}
static void set_meter_ram_value(void* indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter2.METER,Meter2.INDIC, v);
}
//-------------animation---------------

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

MeterInfo ArcCreate(lv_obj_t * obj,const char * text,int16_t value,int x){
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

void Content_Update(MeterInfo *meterobj,int16_t value,bool check){
    
    lv_anim_init(&anim);

    if (check){
        lv_anim_set_exec_cb(&anim, set_meter_cpu_value);
    }else{
        lv_anim_set_exec_cb(&anim, set_meter_ram_value);
    }

    lv_anim_set_values(&anim, meterobj->Meter_val_old, value);
    lv_anim_set_time(&anim, 700); /*2 sec for 1 turn of the minute hand (1 hour)*/
    lv_anim_set_var(&anim, meterobj->INDIC);
    lv_anim_start(&anim);
    lv_anim_set_path_cb(&anim,lv_anim_path_ease_in_out);
    lv_label_set_text_fmt(lv_obj_get_child(meterobj->METER,0),"%d %%",value);
    meterobj->Meter_val_old = value;
}

float NetworkSpeed(int32_t bytes) {
    if (bytes < 1024 * 1024) {
        return  float(bytes / 1000.0);
    } else if (bytes < 1000 * 1000 * 1000) { //MB
        return  float(bytes / 1000000.0);
    }else{
        return 0.00;
    }
}
void SpeedSymbol(int32_t v1,int32_t v2){
    bool a = false;
    if ( v1 < 1024 * 1024 && v2 < 1024 * 1024  ){
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

const char *StorageVolumeCal(uint64_t vol) {
    float volume = vol / 1024.0 * 1024.0;
    static char buffer[10];
    if(volume >= 1024){ // MB
        if((volume /=1024.0) >= 1024){//GB
            if((volume/=1024.0) >= 1024){//TB
                snprintf(buffer, 10, "%.2fTB",(volume/=1024.0));
            }
            snprintf(buffer, 10, "%.2fGB",(volume/=1024.0));
        }
        else{
            snprintf(buffer, 10, "%.2fMB",volume);
        }
    }
    else{
        snprintf(buffer, 10, "%.2fKB",volume);
    }
    return buffer;
}
void StorageLabelUpdate(lv_obj_t * volume, int select){
    // lv_label_set_text()
    float value = (float(SystemInfo.Volume[select][1]) / SystemInfo.Volume[select][0]) * 100;
    printf("\nvolValue1:%d",value);

    lv_bar_set_value(lv_obj_get_child(volume,1),(int)value, LV_ANIM_OFF);
    lv_label_set_text(lv_obj_get_child(volume,2),SystemInfo.StorageName[select].c_str());

    static char label_buffer[14];
    snprintf(label_buffer, 14, "%s|%s", StorageVolumeCal(SystemInfo.Volume[select][1]), StorageVolumeCal(SystemInfo.Volume[select][0]));
    lv_label_set_text(lv_obj_get_child(volume,3),label_buffer);
    lv_label_set_text_fmt(lv_obj_get_child(volume,4),"%.1f%%",value); 

}
void SystemInfoBox(lv_obj_t * Info,String text,String smalltitle2,bool isDot){
    // lv_label_set_text()
    if (smalltitle2 != NULL || "NULL"){
        lv_label_set_text(lv_obj_get_child(Info,1),smalltitle2.c_str());
    }
    
    lv_label_set_text(lv_obj_get_child(Info,2),text.c_str());
    

}
void SystemInfoBox(lv_obj_t * Info,int value,int select,String format,bool isDot){
    // lv_label_set_text()
    if(isDot){
        
    }else{
        
    }
    lv_label_set_text_fmt(lv_obj_get_child(Info,select),format.c_str(),value);
    

}
//--------------page Main style-------------------

//-----------------------Function-------------------
void UserLogout(){
    HttpGetJson(SyConfig.LogoutAPI + SyConfig.Sid);
    TaskStatus.SIDStatu =false;
}
// Json analysis
int SidJson(String Json){
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
void CPU_Net_RAM_Json(String Json){
    StaticJsonDocument<1024> doc;
    deserializeJson(doc,Json);
    JsonObject hosts_0 = doc["data"]["hosts"][0];
    SystemInfo.CPUUse  = hosts_0["cpu_usage"];
    SystemInfo.RAMUse  = hosts_0["ram_usage"];
    
    printf("CPU:%d, RAM:%d\n",SystemInfo.CPUUse,SystemInfo.RAMUse);
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
String HttpGetJson(String url){
    HTTPClient http;
    WiFiClient client;
    String json = "";
    http.setTimeout(15000);
    http.begin(client,url);
    int code = http.GET();
    if ( code == HTTP_CODE_OK ){
        json = http.getString();
        printf("Json:%s",json.c_str());
        http.end();
        return json;
    }else{
        http.end();
        return "None";
    }
}
void ui_event_SyPad(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        // printf("->\n");
        if (SynologyPad_Current < (SynologyPad_List.size() - 1)) {
            SynologyPad_Current++;
            lv_scr_load_anim(*SynologyPad_List[SynologyPad_Current], LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            Info_update(_Info_update);
            // RequestTask(API_Update_Task);
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
            Info_update(_Info_update);
            // SystemInfoTask(System_Info_Task);
        }
    }
}
void Storage_Json(String Json){
    StaticJsonDocument<1526> doc;
    deserializeJson(doc, Json);   
    int i = 0;
    for (JsonObject data_vol_info_item : doc["data"]["vol_info"].as<JsonArray>()) {
        SystemInfo.StorageName[i] = data_vol_info_item["name"].as<String>(); // "volume_2", "volume_1"
        SystemInfo.Volume[i][0] = data_vol_info_item["total_size"].as<u64_t>(); // "475195183104", ...
        SystemInfo.Volume[i][1] = data_vol_info_item["used_size"].as<u64_t>(); // "260152016896", ...
        printf("Vol:%s\n",data_vol_info_item["total_size"].as<const char *>());
        i++;
    }
    // printf("VolumeNamr:%s,total:%lld,used:%lld\n",SystemInfo.StorageName[0].c_str(),SystemInfo.Volume[0][0],SystemInfo.Volume[0][1]);
}
void SystemInfo_Json(String Json){
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, Json);   
    JsonObject data = doc["data"];
    SystemInfo.CPUCoreNum = data["cpu_cores"]; // "4"
    SystemInfo.CPUType = data["cpu_series"].as<String>(); // "J3455"
    SystemInfo.FirmwareVer = data["firmware_ver"].as<String>(); // "DSM 7.1.1-42962 Update 6"
    SystemInfo.DeviceName = data["model"].as<String>(); // "DS718+"
    SystemInfo.RAMGB = data["ram_size"]; // 6144
    SystemInfo.SystemTemp = data["sys_temp"]; // 51
    SystemInfo.iSTempWarn = data["temperature_warning"]; // false
    SystemInfo.UpTime = data["up_time"].as<String>(); // "158:56:32"
    // printf("\nDevice:%s,RAMGB:%d SystemTemp:%d,Up_Time:%s,Firware:%s\n",
    //                 SystemInfo.DeviceName,SystemInfo.RAMGB,
    //                 SystemInfo.SystemTemp,SystemInfo.UpTime,
    //                 SystemInfo.FirmwareVer.c_str()
    //                 );
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
            device->Wf.enableIPv6();
            TaskStatus.SIDCode  =  SidJson(HttpGetJson(SyConfig.SidAPI));
            TaskStatus.SIDStatu = true;
        }
        if (TaskStatus.SIDStatu && device->Wf.isConnected())
        {
            CPU_Net_RAM_Json(HttpGetJson(SyConfig.CPUAPI + SyConfig.Sid));           
        }
        vTaskDelay(APIUpdateTime);
    }
    TaskStatus.TaskOnWork = false;
    vTaskDelete(API_Update_Task);
}
void SystemInfoTask(void *arg)
{
    while (true)
    {
        /* code */
        vTaskDelay(SystemInfoUpdateTime);
        if(TaskStatus.SIDStatu && device->Wf.isConnected()){
            SystemInfo_Json(HttpGetJson(SyConfig.SystemInfoAPI + SyConfig.Sid));
            Storage_Json(HttpGetJson(SyConfig.StorageAPI + SyConfig.Sid));
            vTaskDelay(55000);
        }
        
    }
    vTaskDelete(System_Info_Task);
}

void Info_update(lv_timer_t * timer){
    if(SynologyPad_Current == 0){
        static char label_buffer[10];
        static I2C_BM8563_TimeTypeDef rtc_time;
        static int16_t hours, minutes, seconds;
        device->Rtc.getTime(&rtc_time);
        snprintf(label_buffer, 10, "%02d:%02d", rtc_time.hours, rtc_time.minutes);
        lv_label_set_text(TimeLabel, label_buffer);
        Content_Update(&Meter1,SystemInfo.CPUUse,true);
        Content_Update(&Meter2,SystemInfo.RAMUse,false);
        Net_Update(Uplabel,Downlabel,SystemInfo.Upload,SystemInfo.Downlaod);
    }else if(SynologyPad_Current == 1){
        StorageLabelUpdate(Volume1,0);
        StorageLabelUpdate(Volume2,1);
        SystemInfoBox(CpuBOX,SystemInfo.CPUType,String(SystemInfo.CPUCoreNum),false);
        SystemInfoBox(DeviceBOX,SystemInfo.DeviceName,"T",false);
        SystemInfoBox(RamBOX,SystemInfo.RAMGB / 1024.0,1,"%d G",false);
        SystemInfoBox(TempBox,SystemInfo.SystemTemp,1,"%d C",true);
        SystemInfoBox(WorkBox,SystemInfo.UpTime,"Time",false);
        // SystemInfoBox(TempBox,SystemInfo.RAMGB/1024,"%d G",false);
    }
    
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
    lv_obj_set_style_text_color(TimeLabel,lv_color_hex(0x8F8F8F),0);
    // lv_obj_set_style_text_opa(TimeLabel,180,0);
    
}


void ScreenSec(){
    
    
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
        xTaskCreatePinnedToCore(RequestTask, "RequestTask", 1024 * 4,NULL, 5, &API_Update_Task, 1 );
        xTaskCreatePinnedToCore(SystemInfoTask, "SystemInfo", 1024 * 4,NULL, 6, &System_Info_Task, 1 );
        _Info_update = lv_timer_create(Info_update, APIUpdateTime + 500,NULL);
        Info_update(_Info_update);     // status bar time update
        for (auto i : SynologyPad_List) {
            lv_obj_add_event_cb(*i, ui_event_SyPad, LV_EVENT_ALL, NULL);
        }
        SynologyPad_Current = 0;
        lv_scr_load_anim(*SynologyPad_List[SynologyPad_Current], LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
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
        // delete CpuBOX;
        // delete RamBOX;
        // delete TempBox;
        // delete WorkBox;
        // delete DeviceBOX;
        vTaskDelete(API_Update_Task);
        vTaskDelete(System_Info_Task);
        lv_timer_del(_Info_update);
        lv_obj_del(ui_SyPageMain);
        lv_obj_del(ui_SyPageSec);
        
        if (device->Wf.isConnected()){
            SyConfig.SidStatus=false;
            UserLogout();
        }
        UI_LOG("Destroy Done!");
        
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
