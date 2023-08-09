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
        IMPORT_FONT(UbuntuBold18);
        /* Import Images */
        IMPORT_IMG(about_dark);
        IMPORT_IMG(alarm);
        IMPORT_IMG(axp);
        IMPORT_IMG(battery_dark);
        IMPORT_IMG(battery_info);
        IMPORT_IMG(battery);
        IMPORT_IMG(battery2);
        IMPORT_IMG(bluetooth_dark);
        IMPORT_IMG(clock_dark);
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
