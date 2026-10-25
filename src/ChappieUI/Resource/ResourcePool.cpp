#include "ResourcePool.h"

#include "ResourceManager/ResourceManager.h"

static ResourceManager Font_;
static ResourceManager Image_;

extern "C" {
#define IMPORT_FONT(name) \
do{\
    LV_FONT_DECLARE(font_##name)\
    Font_.AddResource(#name, (void*)&font_##name);\
}while(0)

#define IMPORT_IMG(name) \
do{\
    LV_IMG_DECLARE(img_src_##name)\
    Image_.AddResource(#name, (void*)&img_src_##name);\
}while (0)

    static void Resource_Init()
    {
        /* Import Fonts */
        IMPORT_FONT(bahnschrift_13);
        IMPORT_FONT(bahnschrift_17);
        // IMPORT_FONT(bahnschrift_32);
        IMPORT_FONT(bahnschrift_65);
        // IMPORT_FONT(agencyb_36);
        // IMPORT_FONT(agencyb_36_1);
        IMPORT_FONT(Digital16);
        IMPORT_FONT(UbuntuBold16);
        IMPORT_FONT(UbuntuBold18);
        IMPORT_FONT(MontserratBold48);
        IMPORT_FONT(UbuntuBold96);
        IMPORT_FONT(UbuntuReg48);
        // Apple fonts SF—Bold, SF—Regular
        // IMPORT_FONT(apple_regular_13);
        // IMPORT_FONT(apple_bold_17);


        /* Import Images */
        IMPORT_IMG(about_dark);
        IMPORT_IMG(alarm);
        IMPORT_IMG(axp);
        IMPORT_IMG(battery_dark);
        IMPORT_IMG(battery_info);
        IMPORT_IMG(battery);
        IMPORT_IMG(bluetooth_dark);
        IMPORT_IMG(clock_dark);
        IMPORT_IMG(disk_green);
        IMPORT_IMG(listening);
        IMPORT_IMG(monitor_dark);
        IMPORT_IMG(ok_dark);
        IMPORT_IMG(sd_card);
        IMPORT_IMG(storage);
        IMPORT_IMG(system_info);
        IMPORT_IMG(time_dark);
        IMPORT_IMG(time_info);
        IMPORT_IMG(trip);
        IMPORT_IMG(warming_dark);
        IMPORT_IMG(wifi_dark);
        
        /* Settings navigation icons: one 48x48 ALPHA_8BIT set drawn with a
         * single 2 px stroke, so every module icon shares the same canvas,
         * weight and optical centre. Coloured with img_recolor at draw time. */
        IMPORT_IMG(nav_wifi);
        IMPORT_IMG(nav_bluetooth);
        IMPORT_IMG(nav_time);
        IMPORT_IMG(nav_display);
        IMPORT_IMG(nav_battery);
        IMPORT_IMG(nav_buttons);
        IMPORT_IMG(nav_imu);
        IMPORT_IMG(nav_about);
        /* Small charging bolt drawn on top of the status-bar battery body. */
        IMPORT_IMG(bolt);

    }

} /* extern "C" */

void ResourcePool::Init()
{
    Resource_Init();
    Font_.SetDefault((void*)LV_FONT_DEFAULT);
}
lv_font_t* ResourcePool::GetFont(const char* name)
{
    return (lv_font_t*)Font_.GetResource(name);
}
const void* ResourcePool::GetImage(const char* name)
{
    return Image_.GetResource(name);
}

extern "C" void ResourcePool_Init(void)
{
    ResourcePool::Init();
}

extern "C" lv_font_t *ResourcePool_GetFont(const char *name)
{
    return ResourcePool::GetFont(name);
}

extern "C" const void *ResourcePool_GetImage(const char *name)
{
    return ResourcePool::GetImage(name);
}