#if 1
#include "App_Settings.h"
#include "../../../ChappieBsp/Chappie.h"
#define VERSION_BUILD_TIME    __DATE__"-"__TIME__
void Status_update(lv_timer_t * timer);
static std::string app_name = "Settings";
static CHAPPIE* device;

static struct {
    lv_obj_t * connect;
    lv_obj_t * IP;
    lv_obj_t * GateWay;
    lv_obj_t * WiFi_Name;
    lv_obj_t * Mac;
}wifiInfo;
static struct {
    lv_obj_t * AXPTemp;
    lv_obj_t * BatVolt;
    lv_obj_t * BatCurr;
    lv_obj_t * BatPower;
    lv_obj_t * USBCharing;
}powInfo;
LV_IMG_DECLARE(ui_img_icon_setting_png);
static lv_obj_t * setting_list,*sub_wifi_page;
static lv_style_t EXIT_btn_style;

lv_timer_t* Ap_update_timer,*Status_update_timer;
enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

// static void back_event_handler(lv_event_t * e);
static void switch_handler(lv_event_t * e);
lv_obj_t * root_page;
static void WiFi_AP_Switch_Handler(lv_event_t * e);
static void WiFi_Switch_Handler(lv_event_t * e);
static void Bright_event_cb(lv_event_t * e);
static lv_obj_t * create_text(lv_obj_t * parent, const void * icon, const char * txt,
                                        lv_menu_builder_variant_t builder_variant);
static lv_obj_t * create_slider(lv_obj_t * parent,
                                   const char * icon, const char * txt, int32_t min, int32_t max, int32_t val);
static lv_obj_t * create_switch(lv_obj_t * parent,
                                   const char * icon, const char * txt, bool chk);
static void Created_Setting_Home(lv_obj_t * obj,
                                   const void * icon,const char * txt,lv_obj_t * menu,lv_obj_t * son);
static lv_obj_t * ContentBox(lv_obj_t * ob,const char * ttitle,const char * content,
                            bool select = true,int16_t radius=0,int16_t label_width=100,lv_align_t ctpostion = LV_FLEX_ALIGN_SPACE_EVENLY);
void Content_Update(lv_obj_t * obj,const char * text){
    // printf("child num: %d",lv_obj_get_index(obj));
    lv_label_set_text(lv_obj_get_child(obj,1),text);
}
void Ap_update(lv_timer_t * timer)
{
    device->Wf.WiFiloop();
}
void Status_update(lv_timer_t * timer)
{
    Content_Update(wifiInfo.connect,device->Wf.isConnected() ? "Connected" : "Disconnected");
    Content_Update(wifiInfo.IP,device->Wf.LocalIP().c_str());
    Content_Update(wifiInfo.GateWay,device->Wf.GateWay().c_str());
    Content_Update(wifiInfo.WiFi_Name,device->Wf.WiFiN());
    Content_Update(wifiInfo.Mac,device->Wf.WiFiMac());
    //power page
    #if AXPManage
        Content_Update(powInfo.AXPTemp,(device->Pow.AXP173Temp()+"C").c_str());
        Content_Update(powInfo.BatCurr,(device->Pow.BatCurrent()+"mA").c_str());
        Content_Update(powInfo.BatPower,(device->Pow.BatPower()+"mW").c_str());
        Content_Update(powInfo.BatVolt,(device->Pow.BatVoltage()+"V").c_str());
        Content_Update(powInfo.USBCharing,device->Pow.isCharing() ? "Charing" : "Discharge");
    #endif
}


void SettingPage(void)
{
    
    // -----style
    lv_obj_t * btn;
    lv_style_init(&EXIT_btn_style);//初始化样式
    lv_style_set_align(&EXIT_btn_style,LV_FLEX_ALIGN_CENTER);
    lv_style_set_text_font(&EXIT_btn_style,&lv_font_montserrat_14);
    //----
    lv_obj_t * menu = lv_menu_create(lv_scr_act());
    
    //background color
    // lv_obj_set_style_bg_color(menu, lv_color_hex(0x4D5B74), LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_ENABLED);
    // lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);

    lv_obj_set_size(menu, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_center(menu);

    lv_obj_t * cont;
    lv_obj_t * section;

    /*Create sub pages*/
    

    sub_wifi_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_wifi_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_wifi_page);
    lv_obj_set_style_bg_color(sub_wifi_page, lv_color_hex(0x4D5B74), LV_PART_MAIN | LV_STATE_DEFAULT);
    section = lv_menu_section_create(sub_wifi_page);
    wifiInfo.connect = ContentBox(section,LV_SYMBOL_WIFI,device->Wf.isConnected() ? "Connected" : "Disconnected",false,35);
    lv_obj_t * WiFi_switch = create_switch(section,LV_SYMBOL_WIFI, "WiFi",device->Wf.isConnected() ? true:false);
    lv_obj_add_event_cb(lv_obj_get_child(WiFi_switch,2),WiFi_Switch_Handler,LV_EVENT_CLICKED,section);

    lv_obj_t * AP_switch = create_switch(section, NULL, "AP Mode", false);
    lv_obj_add_event_cb(lv_obj_get_child(AP_switch,1),WiFi_AP_Switch_Handler,LV_EVENT_CLICKED,section);
    
    // printf("AP child ID:%d",lv_obj_get_index(AP_switch));
    wifiInfo.WiFi_Name=ContentBox(section,"WiFi:",device->Wf.WiFiN());
    wifiInfo.IP=ContentBox(section,"IP:",device->Wf.LocalIP().c_str());
    wifiInfo.GateWay=ContentBox(section,"GyIP:",device->Wf.GateWay().c_str());
    wifiInfo.Mac=ContentBox(section,"MAC:",device->Wf.WiFiMac());
    


    lv_obj_t * sub_bluetooth_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_bluetooth_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_bluetooth_page);
    section = lv_menu_section_create(sub_bluetooth_page);
    create_slider(section, LV_SYMBOL_SETTINGS, "Velocity", 0, 150, 120);
    create_slider(section, LV_SYMBOL_SETTINGS, "Acceleration", 0, 150, 50);
    create_slider(section, LV_SYMBOL_SETTINGS, "Weight limit", 0, 150, 80);

    lv_obj_t * sub_time_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_time_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_time_page);
    section = lv_menu_section_create(sub_time_page);
    create_switch(section, NULL, "Auto Time Mode", true);
    
    lv_obj_t * sub_display_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_display_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_display_page);
    section = lv_menu_section_create(sub_display_page);
    lv_obj_t * Brig = create_slider(section, LV_SYMBOL_SETTINGS, "Brightness", 0, 150, (uint8_t)device->Lcd.getBrightness());
    lv_obj_add_event_cb(lv_obj_get_child(Brig,2),Bright_event_cb,LV_EVENT_VALUE_CHANGED,NULL);
    
    // printf("Brig child ID:%d",lv_obj_get_child_id(Brig));
    lv_obj_t * sub_battery_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_battery_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_battery_page);
    section = lv_menu_section_create(sub_battery_page);
    ContentBox(section," ","AXP173 Information",true,0,150);
    #if AXPManage 
        powInfo.AXPTemp = ContentBox(section,"Temp:",device->Pow.AXP173Temp().c_str(),true,0,-1);
        powInfo.BatVolt = ContentBox(section,"BatVolt:",device->Pow.BatVoltage().c_str(),true,0,-1);
        powInfo.BatCurr = ContentBox(section,"BatCurr:",device->Pow.BatCurrent().c_str(),true,0,-1);
        powInfo.BatPower = ContentBox(section,"Power:",device->Pow.BatCurrent().c_str(),true,0,-1);
        ContentBox(section,"BatExitst:",device->Pow.isChargedBat() ? "EXIST" : "NO",true,0,-1);
        powInfo.USBCharing = ContentBox(section,"USB:",device->Pow.isCharing() ? "Charing" : "Discharge",true,0,-1);
    #endif
    #if IP5Manage
        ContentBox(section,"IP5Manage","With NON ",true,0,-1);
    #endif
    


    // lv_obj_add_event_cb(lv_obj_get_child(lv_obj_get_child(LV_m,));

    lv_obj_t * sub_software_info_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_software_info_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    section = lv_menu_section_create(sub_software_info_page);
    
    // lv_obj_align_to(cont_col, cont_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    
    ContentBox(section,"Fir:","Chappie 2");
    ContentBox(section,"Code:","Yeely");
    ContentBox(section,"LVGL:",LVGL_VERSION_INFO);
    ContentBox(section,"Build:",VERSION_BUILD_TIME);
    ContentBox(section,"Ver: ","V1.1");
    lv_obj_t * sub_about_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_about_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_about_page);

    section = lv_menu_section_create(sub_about_page);
    ContentBox(section,"Info","About More",true,35);
    cont = create_text(section, NULL, "Software information", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_software_info_page);

    // lv_obj_t * sub_menu_mode_page = lv_menu_page_create(menu, NULL);
    // lv_obj_set_style_pad_hor(sub_menu_mode_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    // lv_menu_separator_create(sub_menu_mode_page);
    // section = lv_menu_section_create(sub_menu_mode_page);
    // cont = create_switch(section, LV_SYMBOL_AUDIO, "Sidebar enable", true);
    // lv_obj_add_event_cb(lv_obj_get_child(cont, 2), switch_handler, LV_EVENT_VALUE_CHANGED, menu);

    /*Create a root page*/
    root_page = lv_menu_page_create(menu, NULL);
    // lv_obj_set_style_pad_hor(root_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    section = lv_menu_section_create(root_page);
    
    
    Created_Setting_Home(//WiFi
        section,
        ResourcePool::GetImage("wifi_dark"),
        NULL,
        menu,
        sub_wifi_page
    );
    Created_Setting_Home(//Bluetooth
        section,
        ResourcePool::GetImage("bluetooth_dark"),
        NULL,
        menu,
        sub_bluetooth_page
    );
    Created_Setting_Home(//Time
        section,
        ResourcePool::GetImage("clock_dark"),
        NULL,
        menu,sub_time_page
    );
    Created_Setting_Home(//Display
        section,
        ResourcePool::GetImage("monitor_dark"),
        NULL,
        menu,sub_display_page
    );
    Created_Setting_Home(//Battery
        section,
        ResourcePool::GetImage("battery_dark"),
        NULL,
        menu,
        sub_battery_page
    );
    Created_Setting_Home(//About
        section,
        ResourcePool::GetImage("about_dark"),
        NULL,
        menu,
        sub_about_page
    );
    lv_menu_set_sidebar_page(menu, root_page);
    lv_event_send(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    Status_update_timer = lv_timer_create(Status_update,1000, NULL);
    Status_update(Status_update_timer);     // status bar time update
}

static lv_obj_t * ContentBox(lv_obj_t * ob,const char * ttitle,const char * content,bool select,int16_t radius,int16_t label_width,lv_align_t ctpostion){
    lv_obj_t * box = lv_obj_create(ob);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0093E9),0);
    lv_obj_set_style_bg_opa(box, 50,0);
    lv_obj_set_style_radius(box,radius,0);
    lv_obj_set_style_border_opa(box,0,0);
    lv_obj_set_width(box,180);
    lv_obj_set_height(box,50);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * tt =lv_label_create(box);
    lv_obj_t * ct =lv_label_create(box);
    
    lv_label_set_text(tt, ttitle);
    if (select){
        lv_obj_set_style_text_font(tt,ResourcePool::GetFont("UbuntuBold18"),0);
    }
    lv_obj_set_align(tt,LV_FLEX_ALIGN_START);

    lv_label_set_text(ct, content);
    if (label_width > 0){
        lv_obj_set_width(ct,label_width);
    }
    
    lv_label_set_long_mode(ct,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(ct,ResourcePool::GetFont("bahnschrift_17"),0);
    lv_obj_set_align(ct,ctpostion);
    
    return box;
}
static void Created_Setting_Home(lv_obj_t * obj,const void * icon,const char * txt,lv_obj_t * menu,lv_obj_t * son){
    lv_obj_t * cont = create_text(obj, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    // lv_obj_add_style(cont, &EXIT_btn_style, 0); //添加样式
    lv_menu_set_load_page_event(menu, cont, son);
}
static void WiFi_AP_Switch_Handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // lv_obj_t * menu = lv_event_get_user_data(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED || LV_EVENT_CLICKED) {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)){
            Ap_update_timer = lv_timer_create(Ap_update,500, NULL);
            Ap_update(Ap_update_timer);     // status bar time update
            device->Wf.APMode();
        }else {
            device->Wf.APClose();
            lv_timer_del(Ap_update_timer);
        }

    }
}
static void WiFi_Switch_Handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // lv_obj_t * menu = lv_event_get_user_data(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED || LV_EVENT_CLICKED) {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)){
            if(!device->Wf.wifiEnabled){
                device->Wf.enableWiFi();
            }
        }else {
            device->Wf.disableWiFi();
        }

    }
}
static void switch_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * menu = lv_event_get_user_data(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        if(lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            lv_menu_set_page(menu, NULL);
            lv_menu_set_sidebar_page(menu, root_page);
            lv_event_send(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
        }else {
            lv_menu_set_sidebar_page(menu, NULL);
            lv_menu_clear_history(menu); /* Clear history because we will be showing the root page later */
            lv_menu_set_page(menu, root_page);
        }
    }
}

static lv_obj_t * create_text(lv_obj_t * parent, const void * icon, const char * txt,
                                        lv_menu_builder_variant_t builder_variant)
{
    lv_obj_t * obj = lv_menu_cont_create(parent);

    lv_obj_t * img = NULL;
    lv_obj_t * label = NULL;

    if(icon) {
        img = lv_img_create(obj);
        // lv_obj_enable_style_refresh(false);
        lv_obj_align(img,LV_ALIGN_CENTER,5,0);
        lv_img_set_src(img, icon);
    }

    if(txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if(builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}
static void Bright_event_cb(lv_event_t * e){
    lv_obj_t * obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED){
        device->Lcd.setBrightness((uint8_t)(lv_slider_get_value(obj) * 1.5));
    }

}
static lv_obj_t * create_slider(lv_obj_t * parent, const char * icon, const char * txt, int32_t min, int32_t max, int32_t val)
{
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t * slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);

    if(icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return obj;
}

static lv_obj_t * create_switch(lv_obj_t * parent, const char * icon, const char * txt, bool chk)
{
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    
    lv_obj_t * sw = lv_switch_create(obj);
    lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : 0);

    return obj;
}

void Setting_Destroy(){
    lv_timer_del(Status_update_timer);

}

namespace App {

    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_Settings_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_Settings_appIcon()
    {
        // return NULL;
        return (void*)&ui_img_icon_setting_png;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    
    void App_Settings_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Settings_appName().c_str());
        SettingPage();
        /*Create a list*/
        

    }

    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_Settings_onLoop()
    {
        // Setting_Loop();
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_Settings_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Settings_appName().c_str());
        Setting_Destroy();
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_Settings_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
