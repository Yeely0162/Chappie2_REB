#ifndef CHAPPIEUI_RESOURCE_POOL_H
#define CHAPPIEUI_RESOURCE_POOL_H

#include "lvgl.h"

#ifdef __cplusplus
namespace ResourcePool
{
    void Init();
    lv_font_t *GetFont(const char *name);
    const void *GetImage(const char *name);
}

/* 同时向 C 文件提供兼容接口 */
extern "C" {
#endif

void ResourcePool_Init(void);
lv_font_t *ResourcePool_GetFont(const char *name);
const void *ResourcePool_GetImage(const char *name);

#ifdef __cplusplus
}
#endif

#endif