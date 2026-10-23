/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    240
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/
static LGFX_EmmaDisplay* _lcd;

/*
 * Panel scan-out rate.
 *
 * A full 280x240 RGB565 frame is 134,400 bytes = 13.4 ms on the 80 MHz SPI
 * bus. The ST7789 scans its 320-line memory in ~16.7 ms at the stock 60 Hz,
 * i.e. the 280 visible lines are scanned in ~13.6 ms. A write therefore takes
 * almost exactly as long as one panel scan: the scan pointer catches the write
 * pointer and the panel shows the new frame above the seam and the previous
 * frame below it (horizontal tearing during any animation).
 *
 * There is no TE/VSYNC pin and no MISO on this panel, so the scan phase cannot
 * be measured. The only remaining lever is the scan speed itself: FRCTRL2
 * (0xC6) lowers the panel frame rate, which lengthens the per-line scan time
 * and lets one contiguous GRAM write finish ahead of the scan.
 *
 * 0x03 is the value used here (lowest-but-one step). Stock is 0x0F. If the
 * panel shows flicker or judder on real hardware, raise this one value back
 * towards 0x0F - nothing else depends on it.
 */
#define CHAPPIE_PANEL_FRCTRL2 0x03

static void panel_set_scan_rate(LGFX_EmmaDisplay* lcd, uint8_t frctrl2)
{
    lcd->startWrite();
    lcd->writeCommand(0xC6);        /* FRCTRL2: frame rate control, normal mode */
    lcd->writeData(frctrl2);
    lcd->endWrite();
}

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(LGFX_EmmaDisplay* lcd)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    // disp_init();
    _lcd = lcd;
    lcd->clear();

    /* Slow the panel scan before the first LVGL frame is pushed (see the
     * comment next to CHAPPIE_PANEL_FRCTRL2 above). */
    panel_set_scan_rate(_lcd, CHAPPIE_PANEL_FRCTRL2);

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/

    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */

    // /* Example for 1) */
    // static lv_disp_draw_buf_t draw_buf_dsc_1;
    // static lv_color_t buf_1[MY_DISP_HOR_RES * 10];                          /*A buffer for 10 rows*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, MY_DISP_HOR_RES * 10);   /*Initialize the display buffer*/

    // /* Example for 2) */
    // static lv_disp_draw_buf_t draw_buf_dsc_2;
    // static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];                        /*A buffer for 10 rows*/
    // static lv_color_t buf_2_2[MY_DISP_HOR_RES * 10];                        /*An other buffer for 10 rows*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_2_1, buf_2_2, MY_DISP_HOR_RES * 10);   /*Initialize the display buffer*/

    /* Example for 3) also set disp_drv.full_refresh = 1 below*/
    static lv_disp_draw_buf_t draw_buf_dsc_3;
    // static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*A screen sized buffer*/
    // static lv_color_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*Another screen sized buffer*/

    /* Try to get buffer from PSRAM */
    lv_color_t * buf_3_1 = (lv_color_t *)ps_malloc(MY_DISP_HOR_RES * MY_DISP_VER_RES * sizeof(lv_color_t));
    lv_color_t * buf_3_2 = (lv_color_t *)ps_malloc(MY_DISP_HOR_RES * MY_DISP_VER_RES * sizeof(lv_color_t));
    /* If failed */
    if ((buf_3_1 == NULL) || (buf_3_2 == NULL)) {
        printf("[LVGL] malloc buffer from PSRAM fialed\n");
        while (1)
            delay(1000);
    } else {
        printf("\n[LVGL] malloc buffer from PSRAM successful\n");
        printf("[LVGL] free PSRAM: %d\r\n", ESP.getFreePsram());
    }

    lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2,
                          MY_DISP_HOR_RES * MY_DISP_VER_RES);   /*Initialize the display buffer*/

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set up the functions to access to your display*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    /*Set a display buffer*/
    disp_drv.draw_buf = &draw_buf_dsc_3;

    /* Buffer is screen-sized, so LVGL can still render any dirty area in a
     * single piece. Partial refresh is used instead of full_refresh: a full
     * refresh re-sends 134 KB for every animation frame even when only a row
     * or a status bar changed. With partial refresh the flush only covers the
     * invalidated area, which shortens each GRAM write and raises the frame
     * rate (the buffers stay screen-sized, so a full-screen change is still
     * rendered and flushed in one pass - no extra banding is introduced). */
    disp_drv.full_refresh = 0;

    /* Fill a memory array with a color if you have GPU.
     * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
     * But if you have a different GPU you can use with this callback.*/
    //disp_drv.gpu_fill_cb = gpu_fill;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // if(disp_flush_enabled) {
    //     /*The most simple case (but also the slowest) to put all pixels to the screen one-by-one*/

    //     int32_t x;
    //     int32_t y;
    //     for(y = area->y1; y <= area->y2; y++) {
    //         for(x = area->x1; x <= area->x2; x++) {
    //             /*Put a pixel to the display. For example:*/
    //             /*put_px(x, y, *color_p)*/
    //             color_p++;
    //         }
    //     }
    // }

    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );
    // _lcd->startWrite();
    _lcd->setAddrWindow( area->x1, area->y1, w, h );
    // _lcd->pushColors( ( uint16_t * )&color_p->full, w * h, true);
    _lcd->startWrite();
    _lcd->writePixels((uint16_t*)&color_p->full, w * h, true);
    _lcd->endWrite(); 
    // _lcd->endWrite();

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    lv_disp_flush_ready(disp_drv);
}

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}


#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
