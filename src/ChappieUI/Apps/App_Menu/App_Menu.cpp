#if 1
#include "App_Menu.h"
#include "../../../ChappieBsp/Chappie.h"

#define VERSION_BUILD_TIME    __DATE__ "-" __TIME__
void Status_update(lv_timer_t * timer);

static std::string app_name = "Menu";
static CHAPPIE* device;

LV_IMG_DECLARE(ui_img_icon_Menu_png);

/**
 * Translate the object as they scroll
 */
void lv_example_scroll_6(void)
{
    lv_obj_t * cont = lv_obj_create(lv_scr_act());  // 创建一个新的对象（容器）并将其放在当前活动的屏幕上

    lv_coord_t screen_width = lv_disp_get_hor_res(NULL); // 获取屏幕宽度
    lv_coord_t screen_height = lv_disp_get_ver_res(NULL); // 获取屏幕高度
    lv_obj_set_size(cont, screen_width, screen_height); // 设置cont对象的大小为屏幕大小
    lv_obj_center(cont);                  // 将对象（容器）居中于其父对象
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN); // 设置容器的流动方向为垂直列
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);        // 设置容器的滚动方向为垂直
    lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER); // 设置当滚动停止时，内容自动调整以使其在容器的中心
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF); // 关闭容器的滚动条

    lv_coord_t button_height = screen_height * 40 / 100;
    uint32_t i;
    for(i = 0; i < 4; i++) {
        lv_obj_t * btn = lv_btn_create(cont);  // 在cont容器中创建一个新的按钮
        lv_obj_set_size(btn, lv_pct(100), button_height);  // 设置按钮的宽度为容器宽度的100%，高度自适应内容


        lv_obj_t * label = lv_label_create(btn); // 在按钮上创建一个新的标签

        switch (i) {
        case 0:
            lv_label_set_text_fmt(label, "Wifi");
            lv_obj_center(label);
            break;
        case 1:
            lv_label_set_text_fmt(label, "Ble");
            lv_obj_center(label);
            break;
        case 2:
            lv_label_set_text_fmt(label, "kalicyh");
            lv_obj_center(label);
            break;
        default:
            lv_label_set_text_fmt(label, "setting");
            lv_obj_center(label);
            break;
    }
    }
}

namespace App {
    /**
     * @brief Return the App name laucnher, which will be show on launcher App list
     * 
     * @return std::string 
     */
    std::string App_Menu_appName()
    {
        return app_name;
    }


    /**
     * @brief Return the App Icon laucnher, NULL for default
     * 
     * @return void* 
     */
    void* App_Menu_appIcon()
    {
        return (void*) &ui_img_icon_Menu_png;
        // return NULL;
    }


    /**
     * @brief Called when App is on create
     * 
     */
    void App_Menu_onCreate()
    {
        UI_LOG("[%s] onCreate\n", App_Menu_appName().c_str());
        FastLED.showColor(CRGB::Orange, 5);
        lv_example_scroll_6();
        /*Create an Arc*/
    }


    /**
     * @brief Called repeatedly, end this function ASAP! or the App management will be affected
     * If the thing you want to do takes time, try create a taak or lvgl timer to handle them.
     * Try use millis() instead of delay() here
     * 
     */
    void App_Menu_onLoop()
    {
    }


    /**
     * @brief Called when App is about to be destroy
     * Please remember to release the resourse like lvgl timers in this function
     * 
     */
    void App_Menu_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", App_Menu_appName().c_str());
        FastLED.showColor(CRGB::Black);
    }


    /**
     * @brief Launcher will pass the BSP pointer through this function before onCreate
     * 
     */
    void App_Menu_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }
    
}

#endif
