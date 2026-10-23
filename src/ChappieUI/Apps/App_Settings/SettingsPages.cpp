#include "SettingsPages.h"
#include "SettingsTheme.h"
#include "../../Resource/ResourcePool.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

using namespace STheme;

namespace SettingsPages {

static CHAPPIE *g_dev = nullptr;
static PageId g_current = Root;
static NavHandler g_nav = nullptr;
static BackHandler g_back = nullptr;

static const char *WEEK[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

/* ========================================================================= */
/* Module table                                                              */
/* ========================================================================= */

/*
 * One entry per top-level module. The order is the order of the carousel and
 * is the only place that decides a module's title and icon, so the sub-page
 * header and the home carousel can never disagree about which icon belongs to
 * which module.
 */
constexpr int MODULE_COUNT = 7;

struct ModuleDef {
    PageId page;
    const char *title;
    const char *icon;
    const char *fields[3];
};

static const ModuleDef MODULES[MODULE_COUNT] = {
    {Wifi,      "Wi-Fi",     "wifi_dark",      {"Status",     "Network", "Mode"}},
    {Bluetooth, "Bluetooth", "bluetooth_dark", {"Status",     "Paired",  "Name"}},
    {Time,      "Time",      "clock_dark",     {"Time",       "Date",    "NTP"}},
    {Display,   "Display",   "monitor_dark",   {"Brightness", "Panel",   "Touch"}},
    {Battery,   "Battery",   "battery_dark",   {"Level",      "Voltage", "State"}},
    {Keys,      "Buttons",   "system_info",    {"A key",      "B key",   "Mapping"}},
    // {Test,      "Device",    "ok_dark",        {"Touch",      "Display", "Chip"}},
    {About,     "About",     "about_dark",     {"Model",      "Firmware","Heap"}},
};

static int module_index_for(PageId page)
{
    for (int i = 0; i < MODULE_COUNT; i++)
        if (MODULES[i].page == page)
            return i;
    return -1;
}

/**
 * @brief Resolve a module icon. A name missing from the resource pool must not
 *        leave an empty slot in the layout: fall back to the generic system
 *        icon and say so once on the serial log.
 */
static const void *module_icon(const char *name)
{
    const void *img = name ? ResourcePool::GetImage(name) : nullptr;
    if (!img) {
        printf("[Settings] icon '%s' missing from ResourcePool, using system_info\n",
               name ? name : "(null)");
        img = ResourcePool::GetImage("system_info");
    }
    return img;
}

/**
 * @brief Draw a module icon into an lv_img with a consistent zoom.
 *
 * The resource pool mixes 35x35 and 36x36 sources and three colour formats
 * (indexed, alpha mask, true colour + alpha). Recolouring to the theme text
 * colour normalises them to one white line style, and sizing from the image
 * descriptor keeps the drawn size identical instead of clipping the 36 px
 * source into a 35 px box.
 */
static void apply_icon(lv_obj_t *img, const char *name, uint16_t zoom)
{
    const lv_img_dsc_t *dsc = (const lv_img_dsc_t *)module_icon(name);
    if (!img || !dsc)
        return;
    lv_img_set_src(img, dsc);
    lv_img_set_zoom(img, zoom);
    lv_obj_set_size(img,
                    (lv_coord_t)((int32_t)dsc->header.w * zoom / 256),
                    (lv_coord_t)((int32_t)dsc->header.h * zoom / 256));
    lv_obj_set_style_img_recolor(img, ST_TEXT, 0);
    lv_obj_set_style_img_recolor_opa(img, 255, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief Sub-page shell: one centred title bar shared by every sub-page.
 *
 * The page name is centred on the real screen width (LV_ALIGN_TOP_MID on a
 * full-width label), exactly like the main menu's "SETTINGS" title. There is
 * no back button: B is the back key and is handled by handleBack(), which
 * keeps a single navigation path for both the physical key and the app.
 */
static lv_obj_t *shell(lv_obj_t *parent, PageId page)
{
    const int index = module_index_for(page);
    const char *title = index >= 0 ? MODULES[index].title : "Settings";

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, ST_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(root);
    lv_obj_set_size(hdr, SCREEN_W, SUBHEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, ST_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(hdr);
    lv_obj_set_size(label, SCREEN_W, 24);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, title);

    return root;
}

/**
 * @brief Module header for a sub-page, resolved from the shared module table
 *        so the icon can never drift from the carousel. The title is shown in
 *        the top bar, so the header only carries the icon and the rows.
 */
static STheme::ModuleHero hero_for(lv_obj_t *page, PageId id, int rowCount)
{
    const int index = module_index_for(id);
    const void *icon = index >= 0 ? module_icon(MODULES[index].icon) : nullptr;
    return STheme::moduleHero(page, icon, nullptr, rowCount);
}

static void set_text_buf(lv_obj_t *label, const char *fmt, ...)
{
    if (!label) return;
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    setText(label, buf);
}

static lv_obj_t *small_button(lv_obj_t *parent, const char *text, lv_color_t bg,
                              lv_color_t fg, lv_coord_t w, lv_coord_t h,
                              lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, fg, 0);
    lv_obj_set_style_text_font(label, fontSmall(), 0);
    lv_obj_center(label);
    if (cb)
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

/**
 * @brief Read the time. Returns false only when the I2C read failed or the
 *        BCD range is impossible. The time is deliberately independent of the
 *        date and of NTP: an unset RTC reads 00:00:00 and must still display.
 */
static bool rtc_read_time(I2C_BM8563_TimeTypeDef &t)
{
    t = {0, 0, 0};
    if (!g_dev) return false;
    if (!g_dev->Rtc.getTime(&t))
        return false;
    return (t.hours >= 0 && t.hours <= 23 &&
            t.minutes >= 0 && t.minutes <= 59 &&
            t.seconds >= 0 && t.seconds <= 59);
}

/**
 * @brief Read the date. This is only used to render the date line; a false
 *        result (unset or absent backup cell) must never hide the time.
 */
static bool rtc_read_date(I2C_BM8563_DateTypeDef &d)
{
    d = {0, 0, 0, 0};
    if (!g_dev) return false;
    if (!g_dev->Rtc.getDate(&d))
        return false;
    return (d.month >= 1 && d.month <= 12 &&
            d.date >= 1 && d.date <= 31 &&
            d.year >= 2000 && d.year <= 2099);
}

/* ========================================================================= */
/* Confirmation modal                                                        */
/* ========================================================================= */

enum ConfirmKind : uint8_t { CONFIRM_NONE = 0, CONFIRM_FORGET_WIFI, CONFIRM_CLEAR_BONDS };

static lv_obj_t *g_confirmOverlay = nullptr;
static ConfirmKind g_confirmKind = CONFIRM_NONE;

static void confirm_close()
{
    if (g_confirmOverlay) {
        closeModal(g_confirmOverlay);
        g_confirmOverlay = nullptr;
    }
    g_confirmKind = CONFIRM_NONE;
}

static void confirm_no_cb(lv_event_t *e)
{
    (void)e;
    confirm_close();
}

static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    ConfirmKind kind = g_confirmKind;
    confirm_close();
    if (!g_dev) return;
    if (kind == CONFIRM_FORGET_WIFI)
        g_dev->Wf.requestForget();
    else if (kind == CONFIRM_CLEAR_BONDS)
        g_dev->Ble.clearBonds();
}

static void open_confirm(const char *title, const char *message, const char *action,
                         ConfirmKind kind)
{
    confirm_close();
    g_confirmKind = kind;

    lv_obj_t *panel = modalPanel(&g_confirmOverlay);
    lv_obj_set_size(panel, 250, 142);

    lv_obj_t *t = lv_label_create(panel);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, ST_TEXT, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 4, 6);

    lv_obj_t *m = lv_label_create(panel);
    lv_label_set_text(m, message);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, 226);
    lv_obj_set_style_text_color(m, ST_MUTED, 0);
    lv_obj_set_style_text_font(m, fontSmall(), 0);
    lv_obj_align(m, LV_ALIGN_TOP_LEFT, 4, 36);

    lv_obj_t *no = small_button(panel, "Cancel", ST_PANEL_HI, ST_TEXT, 96, 32,
                                confirm_no_cb, nullptr);
    lv_obj_align(no, LV_ALIGN_BOTTOM_MID, -56, 0);

    lv_obj_t *yes = small_button(panel, action, ST_DANGER, ST_TEXT, 96, 32,
                                 confirm_yes_cb, nullptr);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_MID, 56, 0);
}

/* ========================================================================= */
/* Root page                                                                 */
/* ========================================================================= */

/*
 * Home page: a one-module-at-a-time carousel that follows the LILYGO factory
 * reference layout.
 *
 *   [ large white line icon ]   [ dual-colour bar ]   Title
 *                                                    Label   Value
 *                                                    Label   Value
 *                                                    Label   Value
 *                                                    Label   Value
 *
 * Swipe left/right (or use the chevrons) to change module; tap the information
 * block to open the module's detail page. There is no brand strip and no list
 * of generic rows on this page.
 */
namespace {

/*
 * Carousel geometry. The selected module occupies one row; the previous and
 * the next module are parked above and below it and slide in/out during a
 * transition. The rail is a fixed sibling of the rows, never a child of them,
 * so it can never be mistaken for (or reused as) a module icon.
 */
constexpr lv_coord_t CAR_VIEW_H  = 178;
constexpr lv_coord_t CAR_ROW_H   = 158;
constexpr lv_coord_t CAR_ROW_GAP = (CAR_VIEW_H - CAR_ROW_H) / 2;
constexpr lv_coord_t CAR_ICON_X  = 14;
constexpr uint16_t   CAR_ICON_ZOOM = 410;   /* 35 px source -> 56 px */
constexpr lv_coord_t CAR_RAIL_X  = 96;
constexpr lv_coord_t CAR_RAIL_Y  = 14;
constexpr lv_coord_t CAR_RAIL_W  = 5;
constexpr lv_coord_t CAR_RAIL_H  = 130;
constexpr lv_coord_t CAR_INFO_X  = 108;
constexpr lv_coord_t CAR_TITLE_W = 160;
constexpr lv_coord_t CAR_FIELD_X = 176;
constexpr lv_coord_t CAR_FIELD_W = 92;
constexpr uint32_t   CAR_ANIM_MS = 220;

struct CarRow {
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *field[3];
    lv_obj_t *value[3];
};

static CarRow g_carRows[3];
static int g_carSlotIndex[3] = {-1, -1, -1};
static lv_obj_t *g_carView = nullptr;
static lv_obj_t *g_carRailFill = nullptr;
static lv_obj_t *g_carIndexLabel = nullptr;
static lv_anim_t g_carAnim;
static int g_carSel = 0;
static int g_carFrom = 0;
static int g_carDir = 0;
/* One step requested while a transition is still running; started when the
 * current animation finishes so old and new state never fight. */
static int g_carPendingDir = 0;
/* Survives the root page being rebuilt, so returning from a sub-page restores
 * the module the user was on. */
static int g_carHomeIndex = 0;
static int32_t g_carRailFrom = 0;   /* rail fill endpoints, 1/256 index units */
static int32_t g_carRailTo = 0;
static bool g_carAnimating = false;

static void car_refresh_all();
static void car_step(int dir);

/** Wrap a carousel index around the module table. */
static int car_wrap(int index)
{
    while (index < 0)
        index += MODULE_COUNT;
    return index % MODULE_COUNT;
}

/*
 * Row fade.
 *
 * Whole-object opacity (lv_obj_set_style_opa) makes LVGL 8 render the object
 * through a layer: _lv_obj_get_layer_type() returns LV_LAYER_TYPE_SIMPLE for
 * any opacity below COVER, and lv_draw_sw_layer_create() refuses alpha layers
 * unless LV_COLOR_SCREEN_TRANSP is 1. On this build it is 0, so every animated
 * row logged
 *     "Rendering this widget needs LV_COLOR_SCREEN_TRANSP 1"
 *     "Couldn't create a new layer context"
 * and was skipped for the whole transition - i.e. the carousel appeared to
 * jump to the next item with no animation.
 *
 * Text and image opacity are blended per glyph/pixel by the draw code and do
 * not need a layer, so the fade is applied to the row's children instead.
 * The row itself stays fully opaque, which also keeps the effect compatible
 * with the current LV_COLOR_DEPTH 16 / LV_COLOR_SCREEN_TRANSP 0 configuration.
 */
static void car_set_row_opa(int slot, lv_opa_t opa)
{
    CarRow &r = g_carRows[slot];
    if (!r.row)
        return;

    if (r.icon)
        lv_obj_set_style_img_opa(r.icon, opa, 0);
    if (r.title)
        lv_obj_set_style_text_opa(r.title, opa, 0);
    for (int i = 0; i < 3; i++) {
        if (r.field[i])
            lv_obj_set_style_text_opa(r.field[i], opa, 0);
        if (r.value[i])
            lv_obj_set_style_text_opa(r.value[i], opa, 0);
    }
}

static void car_set_position(int slot, lv_coord_t y, lv_opa_t opa)
{
    if (!g_carRows[slot].row)
        return;
    lv_obj_set_y(g_carRows[slot].row, y);
    car_set_row_opa(slot, opa);
}

static void car_reset_positions()
{
    for (int slot = 0; slot < 3; slot++)
        car_set_position(slot, CAR_ROW_GAP + (lv_coord_t)(slot - 1) * CAR_ROW_H, LV_OPA_COVER);
}

/**
 * @brief Set the orange part of the rail.
 *
 * `progress` is the animation value (0..256). Every module owns one equal
 * segment of the rail, so the orange fill grows by exactly one segment per
 * downward step and retracts by one segment when going back. Wrapping from
 * the last module to the first (and back) is interpolated across the end of
 * the index range instead of jumping, so the bar never snaps.
 */
static void car_update_rail(int32_t progress)
{
    if (!g_carRailFill)
        return;

    int32_t units = g_carRailFrom + (g_carRailTo - g_carRailFrom) * progress / 256;
    int32_t h = (CAR_RAIL_H * (units + 256)) / (MODULE_COUNT * 256);
    if (h < 2)
        h = 2;
    if (h > CAR_RAIL_H)
        h = CAR_RAIL_H;
    lv_obj_set_height(g_carRailFill, (lv_coord_t)h);
}

static void car_apply_progress(int32_t p)
{
    if (p < 0)
        p = 0;
    if (p > 256)
        p = 256;

    const lv_coord_t dy = (lv_coord_t)(((int32_t)CAR_ROW_H * p) / 256);
    const lv_opa_t out = (lv_opa_t)(255 - (255 * p) / 256);
    const lv_opa_t in = (lv_opa_t)((255 * p) / 256);

    if (g_carDir >= 0) {
        /* Next module: the selected row leaves upwards, the row below it
         * takes its place while fading in. */
        car_set_position(1, CAR_ROW_GAP - dy, out);
        car_set_position(2, CAR_ROW_GAP + CAR_ROW_H - dy, in);
        car_set_position(0, CAR_ROW_GAP - CAR_ROW_H - dy, 0);
    } else {
        /* Previous module: the selected row leaves downwards. */
        car_set_position(1, CAR_ROW_GAP + dy, out);
        car_set_position(0, CAR_ROW_GAP - CAR_ROW_H + dy, in);
        car_set_position(2, CAR_ROW_GAP + CAR_ROW_H + dy, 0);
    }
    car_update_rail(p);
}

static void car_anim_exec(void *var, int32_t value)
{
    (void)var;
    car_apply_progress(value);
}

static void car_bind_slot(int slot, int index);
static void car_update_index_label();

static void car_anim_ready(lv_anim_t *a)
{
    (void)a;
    g_carSel = car_wrap(g_carFrom + g_carDir);
    g_carFrom = g_carSel;
    g_carHomeIndex = g_carSel;
    g_carRailFrom = g_carSel * 256;
    g_carRailTo = g_carRailFrom;
    g_carDir = 0;
    g_carAnimating = false;

    for (int slot = 0; slot < 3; slot++)
        car_bind_slot(slot, g_carSel + slot - 1);
    car_reset_positions();
    car_update_rail(0);
    car_update_index_label();
    car_refresh_all();

    /* A swipe that arrived during this transition is played now, from the
     * fully committed state. */
    if (g_carPendingDir != 0) {
        const int next = g_carPendingDir;
        g_carPendingDir = 0;
        car_step(next);
    }
}

/** Move the selection by one module. dir > 0 = next, dir < 0 = previous. */
static void car_step(int dir)
{
    if (!g_carView)
        return;

    if (g_carAnimating) {
        /* Keep only the latest request; it is started by car_anim_ready(). */
        g_carPendingDir = dir;
        return;
    }

    g_carDir = dir;
    g_carFrom = g_carSel;

    /* Rail endpoints. A normal step moves one segment; a wrap moves across
     * the end of the scale so the orange fill keeps changing continuously. */
    g_carRailFrom = g_carSel * 256;
    if (dir > 0) {
        if (g_carSel == MODULE_COUNT - 1) {
            g_carRailFrom = MODULE_COUNT * 256;
            g_carRailTo = 0;
        } else {
            g_carRailTo = (g_carSel + 1) * 256;
        }
    } else {
        if (g_carSel == 0) {
            g_carRailTo = MODULE_COUNT * 256;
        } else {
            g_carRailTo = (g_carSel - 1) * 256;
        }
    }

    g_carAnimating = true;

    lv_anim_del(g_carView, car_anim_exec);
    lv_anim_init(&g_carAnim);
    lv_anim_set_var(&g_carAnim, g_carView);
    lv_anim_set_values(&g_carAnim, 0, 256);
    lv_anim_set_time(&g_carAnim, CAR_ANIM_MS);
    lv_anim_set_exec_cb(&g_carAnim, car_anim_exec);
    lv_anim_set_path_cb(&g_carAnim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&g_carAnim, car_anim_ready);
    lv_anim_start(&g_carAnim);
}

/*
 * Touch handling
 * --------------
 * LVGL 8 sends LV_EVENT_CLICKED on release whenever the pressed object is not
 * being scrolled (lv_indev.c: `if(scroll_obj == NULL) send CLICKED`). The panel
 * is not scrollable, so every swipe also produced a click: that is why the
 * carousel used to enter the module it had just swiped to. LVGL's own gesture
 * event only fires after 50 px of travel, and a slow or short swipe never
 * reaches it.
 *
 * All input is therefore handled by the carousel viewport itself (the rows are
 * deliberately not clickable, so the press never migrates while the finger
 * moves):
 *   - a vertical drag >= CAR_SWIPE_PX moves exactly one module and latches for
 *     the rest of that touch;
 *   - a release with less than CAR_TAP_PX of travel is a tap: the centre row
 *     opens its page, a neighbour row only becomes selected;
 *   - no CLICKED handler exists anywhere, so a swipe can never open a page.
 */
constexpr lv_coord_t CAR_SWIPE_PX = 22;
constexpr lv_coord_t CAR_TAP_PX   = 14;

static lv_point_t g_carPressPoint;
static int g_carPressSlot = -1;
static bool g_carSessionActive = false;
static bool g_carSwiped = false;
static uint32_t g_carTapBlockUntil = 0;

static lv_indev_t *car_event_indev(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev)
        indev = lv_indev_get_act();
    return indev;
}

/** Map a point inside the carousel viewport to the row shown there. */
static int car_slot_at(lv_coord_t y)
{
    if (y < CAR_ROW_GAP)
        return 0;
    if (y < CAR_ROW_GAP + CAR_ROW_H)
        return 1;
    return 2;
}

static void car_press_cb(lv_event_t *e)
{
    /* Also recorded while a transition runs, so a quick second swipe can be
     * queued (see car_step) instead of being swallowed. A tap is still
     * ignored by car_release_cb while animating. */
    lv_indev_t *indev = car_event_indev(e);
    if (!indev)
        return;

    lv_indev_get_point(indev, &g_carPressPoint);

    lv_area_t a;
    lv_obj_get_coords(g_carView, &a);
    g_carPressSlot = car_slot_at((lv_coord_t)(g_carPressPoint.y - a.y1));

    g_carSessionActive = true;
    g_carSwiped = false;
}

static void car_pressing_cb(lv_event_t *e)
{
    if (!g_carSessionActive || g_carSwiped)
        return;

    lv_indev_t *indev = car_event_indev(e);
    if (!indev)
        return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const int32_t dx = p.x - g_carPressPoint.x;
    const int32_t dy = p.y - g_carPressPoint.y;

    if (LV_ABS(dy) >= CAR_SWIPE_PX && LV_ABS(dy) > LV_ABS(dx)) {
        g_carSwiped = true;
        g_carTapBlockUntil = millis() + 400;   /* release must not tap */
        car_step(dy < 0 ? 1 : -1);             /* finger up = next module */
    }
}

static void car_release_cb(lv_event_t *e)
{
    if (!g_carSessionActive)
        return;

    const bool swiped = g_carSwiped;
    const int slot = g_carPressSlot;
    g_carSessionActive = false;
    g_carSwiped = false;
    g_carPressSlot = -1;

    /* This touch was a swipe: never treat its release as a click. */
    if (swiped || g_carAnimating || millis() < g_carTapBlockUntil)
        return;

    lv_indev_t *indev = car_event_indev(e);
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        if (LV_ABS(p.x - g_carPressPoint.x) > CAR_TAP_PX ||
            LV_ABS(p.y - g_carPressPoint.y) > CAR_TAP_PX)
            return;                     /* neither a swipe step nor a clean tap */
    }

    if (slot == 1) {
        const int index = g_carSlotIndex[1];
        if (index >= 0 && g_nav)
            g_nav((uint8_t)MODULES[index].page);
    } else if (slot == 0) {
        car_step(-1);
    } else if (slot == 2) {
        car_step(1);
    }
}

static void car_press_lost_cb(lv_event_t *e)
{
    /* Only happens if the finger leaves the viewport; the swipe cooldown set
     * in car_pressing_cb() still blocks a stray tap afterwards. */
    (void)e;
    g_carSessionActive = false;
    g_carSwiped = false;
    g_carPressSlot = -1;
}

static void car_bind_slot(int slot, int index)
{
    index = car_wrap(index);
    g_carSlotIndex[slot] = index;

    CarRow &r = g_carRows[slot];
    if (!r.row)
        return;

    const ModuleDef &m = MODULES[index];
    apply_icon(r.icon, m.icon, CAR_ICON_ZOOM);
    lv_obj_align(r.icon, LV_ALIGN_LEFT_MID, CAR_ICON_X, 0);
    lv_label_set_text(r.title, m.title);
    for (int i = 0; i < 3; i++)
        lv_label_set_text(r.field[i], m.fields[i]);
}

static void car_update_index_label()
{
    if (!g_carIndexLabel)
        return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d/%02d", g_carSel + 1, MODULE_COUNT);
    lv_label_set_text(g_carIndexLabel, buf);
}

/**
 * @brief Fill the three information lines of a module from the live system
 *        state. Nothing here is a placeholder: every value is read from the
 *        same service the sub-page uses.
 */
static void car_format_values(const ModuleDef &m, char out[3][48])
{
    for (int i = 0; i < 3; i++)
        out[i][0] = '\0';
    if (!g_dev)
        return;

    switch (m.page) {
    case Wifi: {
        snprintf(out[0], 48, "%s", g_dev->Wf.stateText());
        String cur = g_dev->Wf.currentSSID();
        String cfg = g_dev->Wf.configuredSSID();
        if (g_dev->Wf.isConnected() && cur.length())
            snprintf(out[1], 48, "%s", cur.c_str());
        else if (cfg.length())
            snprintf(out[1], 48, "%s (saved)", cfg.c_str());
        else
            snprintf(out[1], 48, "--");
        const char *mode = "Off";
        if (g_dev->Wf.isAPActive() && (g_dev->Wf.isConnected() || g_dev->Wf.isBusy()))
            mode = "AP + STA";
        else if (g_dev->Wf.isAPActive())
            mode = "AP";
        else if (g_dev->Wf.isWiFiEnabled())
            mode = "STA";
        snprintf(out[2], 48, "%s", mode);
        break;
    }
    case Bluetooth:
        snprintf(out[0], 48, "%s", g_dev->Ble.stateText());
        snprintf(out[1], 48, "%d", g_dev->Ble.getBondCount());
        snprintf(out[2], 48, "%s", g_dev->Ble.getDeviceName().c_str());
        break;
    case Time: {
        I2C_BM8563_TimeTypeDef t;
        I2C_BM8563_DateTypeDef d;
        if (rtc_read_time(t))
            snprintf(out[0], 48, "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
        else
            snprintf(out[0], 48, "--:--:--");
        if (rtc_read_date(d))
            snprintf(out[1], 48, "%04d-%02d-%02d", d.year, d.month, d.date);
        else
            snprintf(out[1], 48, "%s", rtc_read_time(t) ? "Not set" : "--");
        snprintf(out[2], 48, "%s", g_dev->Wf.ntpStateText());
        break;
    }
    case Display:
        snprintf(out[0], 48, "%u%%",
                 (unsigned)((uint16_t)g_dev->Lcd.getBrightness() * 100 / 255));
        snprintf(out[1], 48, "280 x 240");
        snprintf(out[2], 48, "%s", g_dev->Tp.isReady() ? "CST816D OK" : "Not detected");
        break;
    case Battery:
        if (!g_dev->Pow.dataValid()) {
            snprintf(out[0], 48, "--");
            snprintf(out[1], 48, "--");
            snprintf(out[2], 48, "No data");
        } else {
            snprintf(out[0], 48, "%u%%", (unsigned)g_dev->Pow.percent());
            snprintf(out[1], 48, "%.2f V", g_dev->Pow.voltageV());
            snprintf(out[2], 48, "%s", g_dev->Pow.chargeText());
        }
        break;
    case Keys:
        snprintf(out[0], 48, "Power dialog");
        snprintf(out[1], 48, "Back / home");
        snprintf(out[2], 48, "A:9  B:0");
        break;
    // case Test:
    //     snprintf(out[0], 48, "%s", g_dev->Tp.isReady() ? "CST816D OK" : "Not detected");
    //     snprintf(out[1], 48, "ST7789V");
    //     snprintf(out[2], 48, "ESP32-S3");
        break;
    case About:
        snprintf(out[0], 48, "Chappie 2");
        snprintf(out[1], 48, "V1.1");
        snprintf(out[2], 48, "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
        break;
    default:
        break;
    }
}

static void car_refresh_all()
{
    if (!g_dev)
        return;

    for (int slot = 0; slot < 3; slot++) {
        const int index = g_carSlotIndex[slot];
        if (index < 0 || !g_carRows[slot].value[0])
            continue;

        char values[3][48];
        car_format_values(MODULES[index], values);
        for (int i = 0; i < 3; i++)
            setEllipsis(g_carRows[slot].value[i], values[i], CAR_FIELD_W - 2);
    }
}

static void refresh_root()
{
    car_refresh_all();
}

/**
 * @brief Build one carousel row: module icon on the left, title and three
 *        label/value lines on the right.
 */
static void car_create_row(int slot, lv_obj_t *parent)
{
    CarRow &r = g_carRows[slot];

    r.row = lv_obj_create(parent);
    lv_obj_set_size(r.row, SCREEN_W, CAR_ROW_H);
    lv_obj_set_pos(r.row, 0, CAR_ROW_GAP + (lv_coord_t)(slot - 1) * CAR_ROW_H);
    lv_obj_set_style_bg_opa(r.row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r.row, 0, 0);
    lv_obj_set_style_radius(r.row, 0, 0);
    lv_obj_set_style_pad_all(r.row, 0, 0);
    lv_obj_clear_flag(r.row, LV_OBJ_FLAG_SCROLLABLE);
    /* Not clickable on purpose: presses must reach the carousel viewport,
     * which owns the whole swipe/tap state machine. */
    lv_obj_clear_flag(r.row, LV_OBJ_FLAG_CLICKABLE);

    r.icon = lv_img_create(r.row);
    apply_icon(r.icon, MODULES[0].icon, CAR_ICON_ZOOM);   /* 35 px -> 56 px */
    lv_obj_align(r.icon, LV_ALIGN_LEFT_MID, CAR_ICON_X, 0);

    r.title = lv_label_create(r.row);
    lv_obj_set_pos(r.title, CAR_INFO_X, 12);
    lv_obj_set_size(r.title, CAR_TITLE_W, 28);
    lv_obj_set_style_text_color(r.title, ST_TEXT, 0);
    lv_obj_set_style_text_font(r.title, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(r.title, 1, 0);
    lv_label_set_long_mode(r.title, LV_LABEL_LONG_DOT);
    lv_label_set_text(r.title, "");

    for (int i = 0; i < 3; i++) {
        const lv_coord_t y = 48 + (lv_coord_t)i * 30;

        r.field[i] = lv_label_create(r.row);
        lv_obj_set_pos(r.field[i], CAR_INFO_X, y);
        lv_obj_set_size(r.field[i], 66, 22);
        lv_obj_set_style_text_color(r.field[i], ST_MUTED, 0);
        lv_obj_set_style_text_font(r.field[i], fontSmall(), 0);
        lv_label_set_long_mode(r.field[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(r.field[i], "");

        r.value[i] = lv_label_create(r.row);
        lv_obj_set_pos(r.value[i], CAR_FIELD_X, y);
        lv_obj_set_size(r.value[i], CAR_FIELD_W, 22);
        lv_obj_set_style_text_align(r.value[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(r.value[i], ST_TEXT, 0);
        lv_obj_set_style_text_font(r.value[i], fontValue(), 0);
        lv_label_set_long_mode(r.value[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(r.value[i], "");
    }
}

} // namespace

lv_obj_t *buildRoot(lv_obj_t *parent, CHAPPIE *dev)
{
    g_dev = dev;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, ST_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Centred on the real screen width; no fixed left margin. */
    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_size(title, SCREEN_W, 24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, ST_TEXT, 0);
    lv_obj_set_style_text_font(title, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_label_set_text(title, "SETTINGS");

    /* Carousel viewport: clips the rows that slide in and out. */
    g_carView = lv_obj_create(root);
    lv_obj_set_pos(g_carView, 0, HEADER_H);
    lv_obj_set_size(g_carView, SCREEN_W, CAR_VIEW_H);
    lv_obj_set_style_bg_opa(g_carView, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_carView, 0, 0);
    lv_obj_set_style_radius(g_carView, 0, 0);
    lv_obj_set_style_pad_all(g_carView, 0, 0);
    lv_obj_clear_flag(g_carView, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_carView, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_carView, car_press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_carView, car_pressing_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(g_carView, car_release_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(g_carView, car_press_lost_cb, LV_EVENT_PRESS_LOST, nullptr);

    for (int slot = 0; slot < 3; slot++)
        car_create_row(slot, g_carView);

    /*
     * Rail. A separate component of the page, never a child of a row and never
     * reused as a module icon. Teal is the remaining track, orange is the
     * selected part and is animated by car_update_rail().
     */
    lv_obj_t *rail = lv_obj_create(g_carView);
    lv_obj_set_pos(rail, CAR_RAIL_X, CAR_RAIL_Y);
    lv_obj_set_size(rail, CAR_RAIL_W, CAR_RAIL_H);
    lv_obj_set_style_bg_color(rail, ST_TEAL, 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rail, CAR_RAIL_W / 2, 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_CLICKABLE);

    g_carRailFill = lv_obj_create(rail);
    lv_obj_set_pos(g_carRailFill, 0, 0);
    lv_obj_set_size(g_carRailFill, CAR_RAIL_W, 2);
    lv_obj_set_style_bg_color(g_carRailFill, ST_ORANGE, 0);
    lv_obj_set_style_bg_opa(g_carRailFill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_carRailFill, CAR_RAIL_W / 2, 0);
    lv_obj_set_style_border_width(g_carRailFill, 0, 0);
    lv_obj_set_style_pad_all(g_carRailFill, 0, 0);
    lv_obj_clear_flag(g_carRailFill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_carRailFill, LV_OBJ_FLAG_CLICKABLE);

    /* Position indicator. 17 px instead of the 13 px caption font (about
     * +30%) and in the primary text colour, so it is readable on the 240 px
     * high panel without touching the carousel geometry (label 22 px high at
     * y=210, screen ends at 240). */
    g_carIndexLabel = lv_label_create(root);
    lv_obj_set_pos(g_carIndexLabel, 0, HEADER_H + CAR_VIEW_H + 2);
    lv_obj_set_size(g_carIndexLabel, SCREEN_W, 22);
    lv_obj_set_style_text_align(g_carIndexLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_carIndexLabel, ST_TEXT, 0);
    lv_obj_set_style_text_font(g_carIndexLabel, fontValue(), 0);
    lv_obj_set_style_text_letter_space(g_carIndexLabel, 1, 0);
    lv_label_set_text(g_carIndexLabel, "");

    g_carSel = car_wrap(g_carHomeIndex);
    g_carFrom = g_carSel;
    g_carDir = 0;
    g_carPendingDir = 0;
    g_carRailFrom = g_carSel * 256;
    g_carRailTo = g_carRailFrom;
    g_carAnimating = false;
    for (int slot = 0; slot < 3; slot++)
        car_bind_slot(slot, g_carSel + slot - 1);
    car_reset_positions();
    car_update_rail(0);
    car_update_index_label();
    car_refresh_all();

    return root;
}

/* ========================================================================= */
/* Wi-Fi page                                                                */
/* ========================================================================= */

static struct {
    lv_obj_t *sw = nullptr;
    lv_obj_t *apSw = nullptr;
    lv_obj_t *state = nullptr;
    lv_obj_t *network = nullptr;
    lv_obj_t *ip = nullptr;
    lv_obj_t *err = nullptr;
    lv_obj_t *apName = nullptr;
    lv_obj_t *apAddr = nullptr;
    lv_obj_t *apClients = nullptr;
    lv_obj_t *scanMsg = nullptr;
    lv_obj_t *scanBtn = nullptr;
    lv_obj_t *list = nullptr;
    lv_obj_t *mode = nullptr;
    lv_obj_t *gw = nullptr;
    lv_obj_t *mask = nullptr;
    lv_obj_t *dns = nullptr;
    lv_obj_t *mac = nullptr;
    uint32_t scanGen = 0;
    int shownCount = -1;
} g_wifi;

static lv_obj_t *g_pwOverlay = nullptr;
static lv_obj_t *g_pwTa = nullptr;
static lv_obj_t *g_pwErr = nullptr;
static lv_obj_t *g_pwShowLabel = nullptr;
static String g_pwSsid;
static bool g_pwVisible = false;

static void close_password_dialog()
{
    if (g_pwOverlay) {
        closeModal(g_pwOverlay);
        g_pwOverlay = nullptr;
    }
    g_pwTa = nullptr;
    g_pwErr = nullptr;
    g_pwShowLabel = nullptr;
    g_pwVisible = false;
}

static void scan_item_cb(lv_event_t *e);

static void rebuild_scan_list()
{
    if (!g_wifi.list || !g_dev) return;
    lv_obj_clean(g_wifi.list);

    size_t n = g_dev->Wf.scanCount();
    g_wifi.shownCount = (int)n;
    for (size_t i = 0; i < n; i++) {
        String ssid;
        int32_t rssi = 0;
        bool secure = false;
        if (!g_dev->Wf.scanEntry(i, ssid, rssi, secure))
            continue;

        lv_obj_t *row = lv_obj_create(g_wifi.list);
        lv_obj_set_size(row, LV_PCT(100), 34);
        lv_obj_set_style_bg_color(row, ST_PANEL_HI, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Explicit widths keep the name on one line. A flex row let a long
         * SSID such as "Chappie2-sasfl" wrap at the hyphen and push the row
         * past its 34 px height. */
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_pos(name, 10, 5);
        lv_obj_set_size(name, 146, 24);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        setEllipsis(name, ssid.c_str(), 144);
        lv_obj_set_style_text_color(name, ST_TEXT, 0);
        lv_obj_set_style_text_font(name, fontSmall(), 0);

        lv_obj_t *lock = lv_label_create(row);
        lv_obj_set_pos(lock, 160, 5);
        lv_obj_set_size(lock, 34, 24);
        lv_label_set_text(lock, secure ? "WPA" : "OPEN");
        lv_obj_set_style_text_color(lock, secure ? ST_MUTED : ST_TEAL, 0);
        lv_obj_set_style_text_font(lock, fontSmall(), 0);

        char rb[16];
        snprintf(rb, sizeof(rb), "%d", (int)rssi);
        lv_obj_t *sig = lv_label_create(row);
        lv_obj_set_pos(sig, 196, 5);
        lv_obj_set_size(sig, 58, 24);
        lv_obj_set_style_text_align(sig, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(sig, rb);
        lv_obj_set_style_text_color(sig, ST_MUTED, 0);
        lv_obj_set_style_text_font(sig, fontSmall(), 0);

        lv_obj_add_event_cb(row, scan_item_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
    }
}

static void pw_connect_cb(lv_event_t *e)
{
    (void)e;
    if (!g_dev || !g_pwTa) return;
    const char *txt = lv_textarea_get_text(g_pwTa);
    String pass(txt ? txt : "");
    /* This dialog is only opened for secured networks. */
    if (pass.length() < 8) {
        if (g_pwErr) lv_label_set_text(g_pwErr, "Password must be at least 8 characters");
        return;
    }
    if (pass.length() > 63) {
        if (g_pwErr) lv_label_set_text(g_pwErr, "Password is too long");
        return;
    }
    String ssid = g_pwSsid;
    close_password_dialog();
    g_dev->Wf.requestConnect(ssid, pass, false);
    if (g_wifi.scanMsg) {
        char b[64];
        snprintf(b, sizeof(b), "Connecting to %s...", ssid.c_str());
        setText(g_wifi.scanMsg, b);
    }
}

static void pw_show_cb(lv_event_t *e)
{
    (void)e;
    if (!g_pwTa) return;
    g_pwVisible = !g_pwVisible;
    lv_textarea_set_password_mode(g_pwTa, !g_pwVisible);
    if (g_pwShowLabel)
        lv_label_set_text(g_pwShowLabel, g_pwVisible ? "Hide" : "Show");
}

static void pw_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_password_dialog();
}

static void pw_kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY)
        pw_connect_cb(e);
    else if (code == LV_EVENT_CANCEL)
        close_password_dialog();
}

static void open_password_dialog(const String &ssid)
{
    close_password_dialog();
    g_pwSsid = ssid;

    lv_obj_t *panel = modalPanel(&g_pwOverlay);
    lv_obj_set_size(panel, SCREEN_W, SCREEN_H);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Connect to network");
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 264);
    lv_obj_set_style_text_color(title, ST_MUTED, 0);
    lv_obj_set_style_text_font(title, fontSmall(), 0);
    lv_obj_set_pos(title, 0, 0);

    lv_obj_t *ssidLabel = lv_label_create(panel);
    lv_label_set_text(ssidLabel, ssid.c_str());
    lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssidLabel, 264);
    lv_obj_set_style_text_color(ssidLabel, ST_TEXT, 0);
    lv_obj_set_style_text_font(ssidLabel, fontValue(), 0);
    lv_obj_set_pos(ssidLabel, 0, 14);

    g_pwTa = lv_textarea_create(panel);
    lv_obj_set_size(g_pwTa, 264, 32);
    lv_obj_set_pos(g_pwTa, 0, 34);
    lv_textarea_set_one_line(g_pwTa, true);
    lv_textarea_set_password_mode(g_pwTa, true);
    lv_textarea_set_placeholder_text(g_pwTa, "Wi-Fi password");
    lv_obj_set_style_bg_color(g_pwTa, ST_BG_ALT, 0);
    lv_obj_set_style_border_color(g_pwTa, lv_color_hex(0x262C34), 0);
    lv_obj_set_style_border_width(g_pwTa, 1, 0);
    lv_obj_set_style_radius(g_pwTa, 8, 0);
    lv_obj_set_style_text_color(g_pwTa, ST_TEXT, 0);
    lv_obj_set_style_text_font(g_pwTa, fontValue(), 0);
    lv_obj_set_style_pad_all(g_pwTa, 6, 0);

    g_pwErr = lv_label_create(panel);
    lv_label_set_text(g_pwErr, "");
    lv_label_set_long_mode(g_pwErr, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_pwErr, 264);
    lv_obj_set_style_text_color(g_pwErr, ST_RED, 0);
    lv_obj_set_style_text_font(g_pwErr, fontSmall(), 0);
    lv_obj_set_pos(g_pwErr, 0, 68);

    lv_obj_t *cancel = small_button(panel, "Cancel", ST_PANEL_HI, ST_TEXT, 70, 26,
                                    pw_cancel_cb, nullptr);
    lv_obj_set_pos(cancel, 0, 84);

    lv_obj_t *show = small_button(panel, "Show", ST_PANEL_HI, ST_TEXT, 54, 26,
                                  pw_show_cb, nullptr);
    lv_obj_set_pos(show, 76, 84);
    g_pwShowLabel = lv_obj_get_child(show, 0);

    lv_obj_t *connectBtn = small_button(panel, "Connect", ST_TEAL, ST_BG, 122, 26,
                                        pw_connect_cb, nullptr);
    lv_obj_set_pos(connectBtn, 140, 84);

    lv_obj_t *kb = lv_keyboard_create(panel);
    lv_obj_set_size(kb, 264, 112);
    lv_obj_set_pos(kb, 0, 112);
    lv_obj_set_style_bg_color(kb, ST_BG_ALT, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_keyboard_set_textarea(kb, g_pwTa);
    lv_obj_add_event_cb(kb, pw_kb_cb, LV_EVENT_ALL, nullptr);

    lv_obj_add_state(g_pwTa, LV_STATE_FOCUSED);
}

static void scan_item_cb(lv_event_t *e)
{
    if (!g_dev) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (index == 0) return;
    index -= 1;

    String ssid;
    int32_t rssi = 0;
    bool secure = false;
    if (!g_dev->Wf.scanEntry(index, ssid, rssi, secure))
        return;

    if (secure) {
        open_password_dialog(ssid);
    } else {
        g_dev->Wf.requestConnect(ssid, String(""), false);
        if (g_wifi.scanMsg) {
            char b[64];
            snprintf(b, sizeof(b), "Connecting to %s...", ssid.c_str());
            setText(g_wifi.scanMsg, b);
        }
    }
}

static void wifi_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (!g_dev) return;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        g_dev->Wf.enableWiFi();
    else
        g_dev->Wf.disableWiFi();
}

static void ap_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (!g_dev) return;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        g_dev->Wf.APMode();
    else
        g_dev->Wf.APClose();
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!g_dev) return;
    /* One scan at a time: requestScan() also guards this, but bailing out
     * here keeps the button label from flickering on a repeated tap. */
    if (g_dev->Wf.scanning())
        return;
    g_dev->Wf.requestScan();
    if (g_wifi.scanMsg)
        setText(g_wifi.scanMsg, "Scanning...");
}

static void disconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_dev)
        g_dev->Wf.requestDisconnect();
}

static void forget_btn_cb(lv_event_t *e)
{
    (void)e;
    open_confirm("Forget network",
                 "The saved SSID and password will be deleted from the device.",
                 "Forget", CONFIRM_FORGET_WIFI);
}

static void refresh_wifi()
{
    if (!g_dev) return;

    setSwitch(g_wifi.sw, g_dev->Wf.isWiFiEnabled());
    setSwitch(g_wifi.apSw, g_dev->Wf.isAPActive());

    setValue(g_wifi.state, g_dev->Wf.stateText());
    bool failed = (g_dev->Wf.state() == WifiUiState::Error);
    setDanger(g_wifi.state, failed);
    if (g_wifi.err) {
        if (failed) {
            String reason = g_dev->Wf.lastErrorText();
            setText(g_wifi.err, reason.c_str());
            lv_obj_clear_flag(g_wifi.err, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_wifi.err, LV_OBJ_FLAG_HIDDEN);
        }
    }

    String cur = g_dev->Wf.currentSSID();
    String cfg = g_dev->Wf.configuredSSID();
    String net;
    if (g_dev->Wf.isConnected()) {
        net = cur;
    } else if (cfg.length() > 0) {
        net = cfg + " (saved)";
    } else {
        net = "--";
    }
    setEllipsis(g_wifi.network, net.c_str(), 134);
    setValue(g_wifi.ip, g_dev->Wf.LocalIP().c_str());

    bool ap = g_dev->Wf.isAPActive();
    setValue(g_wifi.apAddr, ap ? "http://192.168.4.1" : "--");
    if (ap) {
        char b[48];
        snprintf(b, sizeof(b), "%u connected", (unsigned)g_dev->Wf.apClients());
        setValue(g_wifi.apClients, b);
    } else {
        setValue(g_wifi.apClients, "--");
    }

    const char *mode = "Off";
    if (g_dev->Wf.isAPActive() && (g_dev->Wf.isConnected() || g_dev->Wf.isBusy()))
        mode = "AP + STA";
    else if (g_dev->Wf.isAPActive())
        mode = "AP";
    else if (g_dev->Wf.isWiFiEnabled())
        mode = "STA";
    setValue(g_wifi.mode, mode);
    setValue(g_wifi.gw, g_dev->Wf.GateWay().c_str());
    setValue(g_wifi.mask, g_dev->Wf.SubnetMask().c_str());
    setValue(g_wifi.dns, g_dev->Wf.DnsIP().c_str());
    setValue(g_wifi.mac, g_dev->Wf.WiFiMac());

    String scanErr = g_dev->Wf.scanErrorText();
    if (g_dev->Wf.scanning()) {
        setText(g_wifi.scanMsg, "Scanning...");
    } else if (scanErr.length() > 0) {
        setText(g_wifi.scanMsg, scanErr.c_str());
    } else if (g_dev->Wf.scanCount() == 0 && g_dev->Wf.scanGeneration() > 1) {
        setText(g_wifi.scanMsg, "No networks found");
    } else if (g_dev->Wf.scanCount() > 0) {
        setText(g_wifi.scanMsg, "Tap a network to connect");
    }

    /* Disable the button while a scan is in flight so the device UI and the
     * phone portal cannot queue two scans. */
    if (g_wifi.scanBtn) {
        if (g_dev->Wf.scanning())
            lv_obj_add_state(g_wifi.scanBtn, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(g_wifi.scanBtn, LV_STATE_DISABLED);
    }

    if (g_dev->Wf.scanGeneration() != g_wifi.scanGen) {
        g_wifi.scanGen = g_dev->Wf.scanGeneration();
        rebuild_scan_list();
        if (g_dev->Wf.scanCount() > 0)
            setText(g_wifi.scanMsg, "Tap a network to connect");
    }
}

static lv_obj_t *buildWifi(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Wifi);
    lv_obj_t *page = STheme::page(root);

    switchRow(page, "Wi-Fi", g_dev->Wf.isWiFiEnabled(), wifi_toggle_cb, nullptr, &g_wifi.sw);
    g_wifi.state = infoRow(page, "Status", g_dev->Wf.stateText()).value;
    g_wifi.network = infoRow(page, "Network", "--").value;
    g_wifi.ip = infoRow(page, "IP address", "--").value;
    
    g_wifi.err = lv_label_create(page);
    lv_label_set_text(g_wifi.err, "");
    lv_label_set_long_mode(g_wifi.err, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_wifi.err, LV_PCT(100));
    lv_obj_set_style_text_color(g_wifi.err, ST_RED, 0);
    lv_obj_set_style_text_font(g_wifi.err, fontSmall(), 0);
    lv_obj_set_style_pad_left(g_wifi.err, 4, 0);
    lv_obj_add_flag(g_wifi.err, LV_OBJ_FLAG_HIDDEN);

    button(page, "Disconnect", ST_PANEL_HI, disconnect_btn_cb, nullptr);

    section(page, "SETUP HOTSPOT");
    switchRow(page, "Config AP", g_dev->Wf.isAPActive(), ap_toggle_cb, nullptr, &g_wifi.apSw);
    g_wifi.apName = infoRow(page, "AP name", ConfigWiFi::AP_SSID).value;
    g_wifi.apAddr = infoRow(page, "Address", "--").value;
    g_wifi.apClients = infoRow(page, "Clients", "--").value;

    g_wifi.scanBtn = button(page, "Scan nearby networks", ST_BLUE, scan_btn_cb, nullptr);
    g_wifi.scanMsg = lv_label_create(page);
    lv_label_set_text(g_wifi.scanMsg, "Tap scan to list 2.4 GHz networks");
    lv_label_set_long_mode(g_wifi.scanMsg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_wifi.scanMsg, LV_PCT(100));
    lv_obj_set_style_text_color(g_wifi.scanMsg, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_wifi.scanMsg, fontSmall(), 0);
    lv_obj_set_style_pad_left(g_wifi.scanMsg, 4, 0);

    g_wifi.list = lv_obj_create(page);
    lv_obj_set_size(g_wifi.list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(g_wifi.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_wifi.list, 0, 0);
    lv_obj_set_style_pad_all(g_wifi.list, 0, 0);
    lv_obj_set_style_pad_row(g_wifi.list, 4, 0);
    lv_obj_set_flex_flow(g_wifi.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_wifi.list, LV_OBJ_FLAG_SCROLLABLE);

    section(page, "ADVANCED");
    lv_obj_t *details = collapsible(page, "Network details", false);
    g_wifi.mode = infoRow(details, "Mode", "Off").value;
    g_wifi.gw = infoRow(details, "Gateway", "--").value;
    g_wifi.mask = infoRow(details, "Subnet", "--").value;
    g_wifi.dns = infoRow(details, "DNS", "--").value;
    g_wifi.mac = infoRow(details, "MAC", "--").value;

    section(page, "PROVISIONING");
    lv_obj_t *hint = lv_label_create(page);
    char hintText[208];
    snprintf(hintText, sizeof(hintText),
             "1. Turn on Config AP\n2. Join the %s hotspot\n"
             "3. Open http://192.168.4.1\n4. Pick a network and connect\n\n"
             "AP password: %s",
             ConfigWiFi::AP_SSID, ConfigWiFi::AP_PASSWORD);
    lv_label_set_text(hint, hintText);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_color(hint, ST_MUTED, 0);
    lv_obj_set_style_text_font(hint, fontSmall(), 0);
    lv_obj_set_style_pad_left(hint, 4, 0);

    button(page, "Forget saved network", ST_PANEL_HI, forget_btn_cb, nullptr);

    refresh_wifi();
    return root;
}

/* ========================================================================= */
/* Bluetooth page                                                            */
/* ========================================================================= */

static struct {
    lv_obj_t *sw = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *name = nullptr;
    lv_obj_t *addr = nullptr;
    lv_obj_t *bonds = nullptr;
    lv_obj_t *error = nullptr;
    lv_obj_t *list = nullptr;
    lv_obj_t *clearBtn = nullptr;
    int bondCount = -1;
    uint32_t armMs = 0;
} g_ble;

static void ble_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (!g_dev) return;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        g_dev->Ble.requestEnable();
    else
        g_dev->Ble.requestDisable();
}

static void bond_remove_cb(lv_event_t *e)
{
    if (!g_dev) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (index == 0) return;
    g_dev->Ble.removeBond((int)(index - 1));
    g_ble.bondCount = -1;
}

static void clear_bonds_cb(lv_event_t *e)
{
    (void)e;
    if (!g_dev) return;
    if (!g_dev->Ble.isEnabled()) {
        if (g_ble.error) lv_label_set_text(g_ble.error, "Turn on Bluetooth first");
        return;
    }
    uint32_t now = millis();
    if (g_ble.armMs && (now - g_ble.armMs) < 4000) {
        g_dev->Ble.clearBonds();
        g_ble.armMs = 0;
        g_ble.bondCount = -1;
        if (g_ble.clearBtn) {
            lv_obj_t *l = lv_obj_get_child(g_ble.clearBtn, 0);
            if (l) lv_label_set_text(l, "Clear pairings");
        }
    } else {
        g_ble.armMs = now;
        if (g_ble.clearBtn) {
            lv_obj_t *l = lv_obj_get_child(g_ble.clearBtn, 0);
            if (l) lv_label_set_text(l, "Tap again to confirm");
        }
    }
}

static void rebuild_bond_list()
{
    if (!g_ble.list || !g_dev) return;
    lv_obj_clean(g_ble.list);

    int n = g_dev->Ble.getBondCount();
    g_ble.bondCount = n;
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(g_ble.list);
        lv_label_set_text(empty, "No paired devices");
        lv_obj_set_style_text_color(empty, ST_MUTED, 0);
        lv_obj_set_style_text_font(empty, fontSmall(), 0);
        lv_obj_set_style_pad_left(empty, 4, 0);
        return;
    }

    for (int i = 0; i < n; i++) {
        String addr;
        if (!g_dev->Ble.getBondAddress(i, addr))
            continue;
        lv_obj_t *row = lv_obj_create(g_ble.list);
        lv_obj_set_size(row, LV_PCT(100), 32);
        lv_obj_set_style_bg_color(row, ST_PANEL_HI, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        setEllipsis(l, addr.c_str(), 168);
        lv_obj_set_style_text_color(l, ST_TEXT, 0);
        lv_obj_set_style_text_font(l, fontSmall(), 0);

        lv_obj_t *del = small_button(row, "Remove", ST_PANEL, ST_RED, 64, 24,
                                     bond_remove_cb, (void *)(uintptr_t)(i + 1));
        (void)del;
    }
}

static void refresh_ble()
{
    if (!g_dev) return;
    setSwitch(g_ble.sw, g_dev->Ble.isEnabled());
    setValue(g_ble.status, g_dev->Ble.stateText());
    setEllipsis(g_ble.name, g_dev->Ble.getDeviceName().c_str(), 134);
    String addr = g_dev->Ble.getConnectedAddress();
    setEllipsis(g_ble.addr, addr.length() ? addr.c_str() : "--", 134);

    char b[32];
    snprintf(b, sizeof(b), "%d", g_dev->Ble.getBondCount());
    setValue(g_ble.bonds, b);

    const char *err = g_dev->Ble.errorText();
    if (err && err[0]) {
        lv_obj_clear_flag(g_ble.error, LV_OBJ_FLAG_HIDDEN);
        setText(g_ble.error, err);
    } else {
        lv_obj_add_flag(g_ble.error, LV_OBJ_FLAG_HIDDEN);
    }

    int n = g_dev->Ble.getBondCount();
    if (n != g_ble.bondCount)
        rebuild_bond_list();

    if (g_ble.armMs && (millis() - g_ble.armMs) >= 4000) {
        g_ble.armMs = 0;
        if (g_ble.clearBtn) {
            lv_obj_t *l = lv_obj_get_child(g_ble.clearBtn, 0);
            if (l) lv_label_set_text(l, "Clear pairings");
        }
    }
}

static lv_obj_t *buildBluetooth(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Bluetooth);
    lv_obj_t *page = STheme::page(root);

    switchRow(page, "Bluetooth", g_dev->Ble.isEnabled(), ble_toggle_cb, nullptr, &g_ble.sw);
    g_ble.status = infoRow(page, "Status", "Off").value;
    g_ble.name = infoRow(page, "Device name", "--").value;
    g_ble.addr = infoRow(page, "Connected", "--").value;
    g_ble.bonds = infoRow(page, "Paired devices", "0").value;

    g_ble.error = lv_label_create(page);
    lv_label_set_text(g_ble.error, "");
    lv_label_set_long_mode(g_ble.error, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ble.error, LV_PCT(100));
    lv_obj_set_style_text_color(g_ble.error, ST_RED, 0);
    lv_obj_set_style_text_font(g_ble.error, fontSmall(), 0);
    lv_obj_add_flag(g_ble.error, LV_OBJ_FLAG_HIDDEN);

    section(page, "PAIRED DEVICES");
    g_ble.list = lv_obj_create(page);
    lv_obj_set_size(g_ble.list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(g_ble.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_ble.list, 0, 0);
    lv_obj_set_style_pad_all(g_ble.list, 0, 0);
    lv_obj_set_style_pad_row(g_ble.list, 4, 0);
    lv_obj_set_flex_flow(g_ble.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_ble.list, LV_OBJ_FLAG_SCROLLABLE);

    g_ble.clearBtn = button(page, "Clear pairings", ST_PANEL_HI, clear_bonds_cb, nullptr);
    g_ble.bondCount = -1;
    refresh_ble();
    return root;
}

/* ========================================================================= */
/* Time page                                                                 */
/* ========================================================================= */

static struct {
    lv_obj_t *time = nullptr;
    lv_obj_t *date = nullptr;
    lv_obj_t *rtc = nullptr;
    lv_obj_t *ntp = nullptr;
    lv_obj_t *ntpErr = nullptr;
    lv_obj_t *autoSw = nullptr;
    lv_obj_t *server = nullptr;
    lv_obj_t *syncBtn = nullptr;
} g_time;

static void ntp_auto_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (g_dev)
        g_dev->Wf.setAutoTime(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void ntp_server_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    if (g_dev)
        g_dev->Wf.setNtpServerIndex((uint8_t)lv_dropdown_get_selected(dd));
}

static void ntp_sync_cb(lv_event_t *e)
{
    (void)e;
    if (g_dev)
        g_dev->Wf.requestNtpSync();
}

static void refresh_time()
{
    if (!g_dev) return;

    I2C_BM8563_TimeTypeDef t;
    I2C_BM8563_DateTypeDef d;
    const bool timeOk = rtc_read_time(t);
    const bool dateOk = rtc_read_date(d);

    /* The clock is shown whenever the BM8563 answered, even when it has
     * never been set (00:00:00) or NTP is off. Only a failed I2C transfer
     * or impossible BCD data falls back to "--". */
    if (timeOk) {
        set_text_buf(g_time.time, "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
        bool low = g_dev->Rtc.getVoltLow();
        setText(g_time.rtc, low ? "Low backup cell" : "OK");
        setDanger(g_time.rtc, low);
    } else {
        setText(g_time.time, "--:--:--");
        setText(g_time.rtc, "Read error");
        setDanger(g_time.rtc, true);
    }

    if (dateOk) {
        set_text_buf(g_time.date, "%04d-%02d-%02d  %s", d.year, d.month, d.date,
                     WEEK[(d.weekDay >= 0 && d.weekDay <= 6) ? d.weekDay : 0]);
    } else {
        /* Date is separate from time validity: an unset date must not turn
         * the clock into "--:--". */
        setText(g_time.date, timeOk ? "Not set" : "--");
    }

    setSwitch(g_time.autoSw, g_dev->Wf.AutoTime);
    setText(g_time.ntp, g_dev->Wf.ntpStateText());

    String ntpErr = g_dev->Wf.ntpLastErrorText();
    if (g_dev->Wf.ntpState() == NtpUiState::Failed && ntpErr.length() > 0) {
        lv_obj_clear_flag(g_time.ntpErr, LV_OBJ_FLAG_HIDDEN);
        setText(g_time.ntpErr, ntpErr.c_str());
    } else if (!g_dev->Wf.isConnected() && g_dev->Wf.AutoTime) {
        lv_obj_clear_flag(g_time.ntpErr, LV_OBJ_FLAG_HIDDEN);
        setText(g_time.ntpErr, "Connect Wi-Fi to sync");
    } else {
        lv_obj_add_flag(g_time.ntpErr, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_time.server) {
        uint16_t want = g_dev->Wf.ntpServerIndex();
        if (!lv_dropdown_is_open(g_time.server) && lv_dropdown_get_selected(g_time.server) != want)
            lv_dropdown_set_selected(g_time.server, want);
    }

    if (g_time.syncBtn) {
        if (!g_dev->Wf.isConnected())
            lv_obj_add_state(g_time.syncBtn, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(g_time.syncBtn, LV_STATE_DISABLED);
    }
}

static lv_obj_t *buildTime(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Time);
    lv_obj_t *page = STheme::page(root);

    g_time.time = infoRow(page, "Time", "--:--:--").value;
    g_time.date = infoRow(page, "Date", "--").value;
    g_time.rtc = infoRow(page, "RTC (BM8563)", "Unavailable").value;    
    
    section(page, "NETWORK TIME");
    switchRow(page, "Auto NTP sync", g_dev->Wf.AutoTime, ntp_auto_cb, nullptr, &g_time.autoSw);
    g_time.ntp = infoRow(page, "NTP status", "Idle").value;

    g_time.ntpErr = lv_label_create(page);
    lv_label_set_text(g_time.ntpErr, "");
    lv_label_set_long_mode(g_time.ntpErr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_time.ntpErr, LV_PCT(100));
    lv_obj_set_style_text_color(g_time.ntpErr, ST_ORANGE, 0);
    lv_obj_set_style_text_font(g_time.ntpErr, fontSmall(), 0);
    lv_obj_add_flag(g_time.ntpErr, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *box = lv_obj_create(page);
    lv_obj_set_size(box, LV_PCT(100), 62);
    lv_obj_set_style_bg_color(box, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 9, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_left(box, 12, 0);
    lv_obj_set_style_pad_right(box, 12, 0);
    lv_obj_set_style_pad_top(box, 8, 0);
    lv_obj_set_style_pad_bottom(box, 8, 0);
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sl = lv_label_create(box);
    lv_label_set_text(sl, "Server");
    lv_obj_set_style_text_color(sl, ST_MUTED, 0);
    lv_obj_set_style_text_font(sl, fontSmall(), 0);

    g_time.server = lv_dropdown_create(box);
    lv_obj_set_width(g_time.server, LV_PCT(100));
    lv_obj_set_height(g_time.server, 28);
    styleDropdown(g_time.server);

    String opts;
    for (uint8_t i = 0; i < g_dev->Wf.ntpServerCount(); i++) {
        if (i) opts += '\n';
        opts += g_dev->Wf.ntpServerName(i);
    }
    lv_dropdown_set_options(g_time.server, opts.c_str());
    lv_dropdown_set_selected(g_time.server, g_dev->Wf.ntpServerIndex());
    lv_obj_add_event_cb(g_time.server, ntp_server_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    g_time.syncBtn = button(page, "Sync now", ST_BLUE, ntp_sync_cb, nullptr);
    refresh_time();
    return root;
}

/* ========================================================================= */
/* Battery page                                                              */
/* ========================================================================= */

static struct {
    lv_obj_t *pct = nullptr;
    lv_obj_t *bar = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *volt = nullptr;
    lv_obj_t *curr = nullptr;
    lv_obj_t *vbus = nullptr;
    lv_obj_t *acin = nullptr;
    lv_obj_t *present = nullptr;
    lv_obj_t *valid = nullptr;
    lv_obj_t *temp = nullptr;
    lv_obj_t *vbusV = nullptr;
    lv_obj_t *vbusC = nullptr;
    lv_obj_t *chgVolt = nullptr;
    lv_obj_t *chgCur = nullptr;
    lv_obj_t *dd = nullptr;
    lv_obj_t *applyMsg = nullptr;
    int16_t pending = -1;
} g_bat;

static void charge_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    g_bat.pending = (int16_t)lv_dropdown_get_selected(dd);
}

static void charge_apply_cb(lv_event_t *e)
{
    (void)e;
    if (!g_dev) return;
    int16_t level = g_bat.pending;
    if (level < 0)
        level = (int16_t)g_dev->Pow.chargeCurrentLevel();
    bool ok = g_dev->Pow.setChargeCurrentLevel((uint8_t)level);
    if (ok)
        g_bat.pending = -1;
    if (g_bat.applyMsg) {
        if (ok) {
            char b[64];
            snprintf(b, sizeof(b), "Applied: %u mA (readback confirmed)",
                     (unsigned)ChappiePower::chargeCurrentMAForLevel((uint8_t)level));
            setText(g_bat.applyMsg, b);
            lv_obj_set_style_text_color(g_bat.applyMsg, ST_TEAL, 0);
        } else {
            setText(g_bat.applyMsg, "Write failed - value not applied");
            lv_obj_set_style_text_color(g_bat.applyMsg, ST_RED, 0);
        }
    }
}

static void refresh_battery()
{
    if (!g_dev) return;

    uint8_t pct = g_dev->Pow.percent();
    set_text_buf(g_bat.pct, "%u%%", (unsigned)pct);
    if (g_bat.bar)
        lv_bar_set_value(g_bat.bar, pct, LV_ANIM_OFF);
    if (g_bat.pct)
        lv_obj_set_style_text_color(g_bat.pct, pct <= 15 ? ST_RED : ST_TEXT, 0);

    setValue(g_bat.status, g_dev->Pow.chargeText());
    lv_obj_set_style_text_color(g_bat.status,
        g_dev->Pow.isCharging() ? ST_TEAL : (pct <= 15 ? ST_RED : ST_TEXT), 0);

    char b[48];
    snprintf(b, sizeof(b), "%.2f V", g_dev->Pow.voltageV());
    setValue(g_bat.volt, g_dev->Pow.dataValid() ? b : "--");

    snprintf(b, sizeof(b), "%+.0f mA", g_dev->Pow.currentMA());
    setValue(g_bat.curr, g_dev->Pow.dataValid() ? b : "--");

    setValue(g_bat.vbus, g_dev->Pow.vbusPresent() ? "Present" : "None");
    setValue(g_bat.acin, g_dev->Pow.acinPresent() ? "Present" : "None");
    setValue(g_bat.present, g_dev->Pow.batteryPresent() ? "Yes" : "No");
    setValue(g_bat.valid, g_dev->Pow.dataValid() ? "Valid" : "Invalid");

    snprintf(b, sizeof(b), "%.1f C", g_dev->Pow.pmicTempC());
    setValue(g_bat.temp, g_dev->Pow.dataValid() ? b : "--");

    snprintf(b, sizeof(b), "%.2f V", g_dev->Pow.vbusVoltageV());
    setValue(g_bat.vbusV, b);
    snprintf(b, sizeof(b), "%.0f mA", g_dev->Pow.vbusCurrentMA());
    setValue(g_bat.vbusC, b);

    snprintf(b, sizeof(b), "%.2f V", g_dev->Pow.chargeVoltageSettingV());
    setValue(g_bat.chgVolt, b);
    snprintf(b, sizeof(b), "%.0f mA", g_dev->Pow.chargeCurrentSettingMA());
    setValue(g_bat.chgCur, b);

    /* Do not override a selection the user has not applied yet. */
    if (g_bat.dd && g_bat.pending < 0 && !lv_dropdown_is_open(g_bat.dd)) {
        uint16_t want = g_dev->Pow.chargeCurrentLevel();
        if (lv_dropdown_get_selected(g_bat.dd) != want)
            lv_dropdown_set_selected(g_bat.dd, want);
    }
}

static lv_obj_t *buildBattery(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Battery);
    lv_obj_t *page = STheme::page(root);

    /* Large readout */
    lv_obj_t *hero = lv_obj_create(page);
    lv_obj_set_size(hero, LV_PCT(100), 78);
    lv_obj_set_style_bg_color(hero, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hero, 10, 0);
    lv_obj_set_style_border_width(hero, 0, 0);
    lv_obj_set_style_pad_all(hero, 8, 0);
    lv_obj_set_style_pad_row(hero, 2, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(hero);
    lv_obj_set_size(top, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    g_bat.pct = lv_label_create(top);
    lv_label_set_text(g_bat.pct, "--%");
    lv_obj_set_style_text_color(g_bat.pct, ST_TEXT, 0);
    lv_obj_set_style_text_font(g_bat.pct, &lv_font_montserrat_24, 0);

    g_bat.status = lv_label_create(top);
    lv_label_set_text(g_bat.status, "--");
    lv_obj_set_style_text_color(g_bat.status, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_bat.status, fontValue(), 0);

    g_bat.bar = lv_bar_create(hero);
    lv_obj_set_size(g_bat.bar, LV_PCT(100), 10);
    lv_bar_set_range(g_bat.bar, 0, 100);
    lv_obj_set_style_bg_color(g_bat.bar, lv_color_hex(0x2A3038), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_bat.bar, ST_TEAL, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_bat.bar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_bat.bar, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    g_bat.volt = infoRow(page, "Battery voltage", "--").value;
    g_bat.curr = infoRow(page, "Battery current", "--").value;
    g_bat.vbus = infoRow(page, "VBUS input", "--").value;
    g_bat.acin = infoRow(page, "ACIN input", "--").value;

    section(page, "POWER DETAILS");
    lv_obj_t *details = collapsible(page, "Power details", false);
    g_bat.present = infoRow(details, "Battery present", "--").value;
    g_bat.valid = infoRow(details, "ADC data", "--").value;
    g_bat.temp = infoRow(details, "PMIC temperature", "--").value;
    g_bat.vbusV = infoRow(details, "VBUS voltage", "--").value;
    g_bat.vbusC = infoRow(details, "VBUS current", "--").value;

    section(page, "ADVANCED POWER CONFIG");
    lv_obj_t *adv = collapsible(page, "Advanced power config", false);

    lv_obj_t *warn = lv_label_create(adv);
    lv_label_set_text(warn,
        "Only the charge-current step is exposed: it is the field the AXP173 "
        "datasheet defines unambiguously. Verify the cell datasheet before "
        "raising it. Charge voltage (including 4.36 V) is read-only here.");
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(warn, LV_PCT(100));
    lv_obj_set_style_text_color(warn, ST_ORANGE, 0);
    lv_obj_set_style_text_font(warn, fontSmall(), 0);
    lv_obj_set_style_pad_left(warn, 4, 0);

    g_bat.chgVolt = infoRow(adv, "Charge voltage", "--").value;
    g_bat.chgCur = infoRow(adv, "Charge current", "--").value;

    g_bat.dd = lv_dropdown_create(adv);
    lv_obj_set_width(g_bat.dd, LV_PCT(100));
    lv_obj_set_height(g_bat.dd, 28);
    styleDropdown(g_bat.dd);

    String opts;
    for (uint8_t i = 0; i < 16; i++) {
        if (i) opts += '\n';
        opts += String(ChappiePower::chargeCurrentMAForLevel(i));
        opts += " mA";
    }
    lv_dropdown_set_options(g_bat.dd, opts.c_str());
    lv_dropdown_set_selected(g_bat.dd, g_dev->Pow.chargeCurrentLevel());
    g_bat.pending = (int16_t)g_dev->Pow.chargeCurrentLevel();
    lv_obj_add_event_cb(g_bat.dd, charge_dd_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    g_bat.applyMsg = lv_label_create(adv);
    lv_label_set_text(g_bat.applyMsg, "");
    lv_label_set_long_mode(g_bat.applyMsg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_bat.applyMsg, LV_PCT(100));
    lv_obj_set_style_text_color(g_bat.applyMsg, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_bat.applyMsg, fontSmall(), 0);
    lv_obj_set_style_pad_left(g_bat.applyMsg, 4, 0);

    button(adv, "Apply charge current", ST_PANEL_HI, charge_apply_cb, nullptr);

    refresh_battery();
    return root;
}

/* ========================================================================= */
/* Buttons page                                                              */
/* ========================================================================= */

static lv_obj_t *buildKeys(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Keys);
    lv_obj_t *page = STheme::page(root);

    section(page, "A BUTTON - GPIO9 / PWR_CTRL");
    infoRow(page, "Short press", "Power dialog");
    infoRow(page, "In dialog", "Focused action");

    section(page, "B BUTTON - GPIO0 / KEY_DWN1");
    infoRow(page, "Short press", "Back / home");
    infoRow(page, "In dialog", "Cancel");

    section(page, "NOTES");
    lv_obj_t *note = lv_label_create(page);
    lv_label_set_text(note,
        "A opens the system power dialog from any page. B keeps the existing "
        "back behaviour and closes the dialog first. No long-press action is "
        "mapped: the AXP173 power-key timing must not be confused with a "
        "software long press.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_style_text_color(note, ST_MUTED, 0);
    lv_obj_set_style_text_font(note, fontSmall(), 0);
    lv_obj_set_style_pad_left(note, 4, 0);

    return root;
}

/* ========================================================================= */
/* Device page and full-screen hardware tests                                */
/* ========================================================================= */

static struct {
    lv_obj_t *touch = nullptr;
} g_test;

/* ---- Full-screen touch test --------------------------------------------- */

#define TOUCH_TRAIL_MAX 24

static lv_obj_t *g_touchTest = nullptr;
static lv_obj_t *g_touchTestCoord = nullptr;
static lv_obj_t *g_touchTestDot = nullptr;
static lv_obj_t *g_touchTestTrail[TOUCH_TRAIL_MAX] = {nullptr};
static int g_touchTestTrailIdx = 0;
static int g_touchTestLastX = -100;
static int g_touchTestLastY = -100;

static void touch_test_delete_cb(lv_event_t *e)
{
    (void)e;
    g_touchTest = nullptr;
    g_touchTestCoord = nullptr;
    g_touchTestDot = nullptr;
    for (int i = 0; i < TOUCH_TRAIL_MAX; i++)
        g_touchTestTrail[i] = nullptr;
    g_touchTestTrailIdx = 0;
}

static void touch_test_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (g_touchTestCoord)
            lv_label_set_text(g_touchTestCoord, "Touch released");
        if (g_touchTestDot)
            lv_obj_add_flag(g_touchTestDot, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING)
        return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
        return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_obj_t *area = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    int x = p.x - a.x1;
    int y = p.y - a.y1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > a.x2 - a.x1) x = a.x2 - a.x1;
    if (y > a.y2 - a.y1) y = a.y2 - a.y1;

    set_text_buf(g_touchTestCoord, "X %d   Y %d", x, y);

    if (g_touchTestDot) {
        lv_obj_clear_flag(g_touchTestDot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_touchTestDot, x - 8, y - 8);
    }

    /* Trail uses a fixed pool of dots and only moves them, so a long drag
     * never allocates new objects. */
    if (abs(x - g_touchTestLastX) + abs(y - g_touchTestLastY) >= 6) {
        lv_obj_t *dot = g_touchTestTrail[g_touchTestTrailIdx];
        g_touchTestTrailIdx = (g_touchTestTrailIdx + 1) % TOUCH_TRAIL_MAX;
        if (dot) {
            lv_obj_set_pos(dot, x - 3, y - 3);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
        g_touchTestLastX = x;
        g_touchTestLastY = y;
    }
}

static void open_touch_test()
{
    if (g_touchTest)
        return;

    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, ST_BG, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    g_touchTest = o;
    g_touchTestTrailIdx = 0;
    g_touchTestLastX = -100;
    g_touchTestLastY = -100;

    lv_obj_add_event_cb(o, touch_test_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(o, touch_test_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(o, touch_test_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(o, touch_test_event_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(o, touch_test_event_cb, LV_EVENT_PRESS_LOST, nullptr);

    for (int i = 0; i < TOUCH_TRAIL_MAX; i++) {
        lv_obj_t *dot = lv_obj_create(o);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_color(dot, ST_TEAL, 0);
        lv_obj_set_style_bg_opa(dot, 140, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        g_touchTestTrail[i] = dot;
    }

    g_touchTestDot = lv_obj_create(o);
    lv_obj_set_size(g_touchTestDot, 16, 16);
    lv_obj_set_style_radius(g_touchTestDot, 8, 0);
    lv_obj_set_style_bg_opa(g_touchTestDot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_touchTestDot, 2, 0);
    lv_obj_set_style_border_color(g_touchTestDot, ST_ORANGE, 0);
    lv_obj_clear_flag(g_touchTestDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_touchTestDot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_touchTestDot, LV_OBJ_FLAG_HIDDEN);

    g_touchTestCoord = lv_label_create(o);
    lv_obj_set_pos(g_touchTestCoord, 8, 4);
    lv_obj_set_size(g_touchTestCoord, 264, 22);
    lv_obj_set_style_text_color(g_touchTestCoord, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_touchTestCoord, fontValue(), 0);
    lv_label_set_text(g_touchTestCoord, "Touch anywhere");

    lv_obj_t *hint = lv_label_create(o);
    lv_obj_set_pos(hint, 0, SCREEN_H - 22);
    lv_obj_set_size(hint, SCREEN_W, 18);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, ST_MUTED, 0);
    lv_obj_set_style_text_font(hint, fontSmall(), 0);
    lv_label_set_text(hint, "B: exit");
}

/* ---- Full-screen display test ------------------------------------------- */

#define DISP_TEST_COUNT 5

static const lv_color_t DISP_TEST_COLORS[DISP_TEST_COUNT] = {
    lv_color_hex(0xFF0000),
    lv_color_hex(0x00FF00),
    lv_color_hex(0x0000FF),
    lv_color_hex(0xFFFFFF),
    lv_color_hex(0x000000),
};
static const char *const DISP_TEST_NAMES[DISP_TEST_COUNT] = {
    "RED", "GREEN", "BLUE", "WHITE", "BLACK",
};

static lv_obj_t *g_dispTest = nullptr;
static lv_obj_t *g_dispTestHint = nullptr;
static lv_obj_t *g_dispTestHelp = nullptr;
static int g_dispTestIndex = 0;

static void display_test_delete_cb(lv_event_t *e)
{
    (void)e;
    g_dispTest = nullptr;
    g_dispTestHint = nullptr;
    g_dispTestHelp = nullptr;
}

static void display_test_apply()
{
    if (!g_dispTest)
        return;

    lv_obj_set_style_bg_color(g_dispTest, DISP_TEST_COLORS[g_dispTestIndex], 0);
    /* Keep the hint readable on every pattern. */
    const lv_color_t fg = (g_dispTestIndex == 3 || g_dispTestIndex == 1) ? lv_color_hex(0x000000)
                                                                       : ST_TEXT;
    if (g_dispTestHint) {
        lv_obj_set_style_text_color(g_dispTestHint, fg, 0);
        set_text_buf(g_dispTestHint, "%s  %d/%d", DISP_TEST_NAMES[g_dispTestIndex],
                     g_dispTestIndex + 1, DISP_TEST_COUNT);
    }
    if (g_dispTestHelp)
        lv_obj_set_style_text_color(g_dispTestHelp, fg, 0);
}

static void display_test_click_cb(lv_event_t *e)
{
    (void)e;
    g_dispTestIndex = (g_dispTestIndex + 1) % DISP_TEST_COUNT;
    display_test_apply();
}

static void open_display_test()
{
    if (g_dispTest)
        return;

    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, DISP_TEST_COLORS[0], 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    g_dispTest = o;
    g_dispTestIndex = 0;

    lv_obj_add_event_cb(o, display_test_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(o, display_test_click_cb, LV_EVENT_CLICKED, nullptr);

    g_dispTestHint = lv_label_create(o);
    lv_obj_set_pos(g_dispTestHint, 0, 106);
    lv_obj_set_size(g_dispTestHint, SCREEN_W, 26);
    lv_obj_set_style_text_align(g_dispTestHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_dispTestHint, fontValue(), 0);
    lv_label_set_text(g_dispTestHint, "");

    lv_obj_t *hint = lv_label_create(o);
    lv_obj_set_pos(hint, 0, 134);
    lv_obj_set_size(hint, SCREEN_W, 18);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, fontSmall(), 0);
    lv_label_set_text(hint, "Tap: next pattern    B: exit");
    g_dispTestHelp = hint;
    display_test_apply();
}

/* ---- Page --------------------------------------------------------------- */

static void touch_test_open_cb(lv_event_t *e)
{
    (void)e;
    open_touch_test();
}

static void display_test_open_cb(lv_event_t *e)
{
    (void)e;
    open_display_test();
}

static lv_obj_t *entry_row(lv_obj_t *parent, const char *title, const char *subtitle,
                           lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 46);
    lv_obj_set_style_bg_color(row, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, ST_PANEL_HI, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 9, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_pos(t, 12, 5);
    lv_obj_set_size(t, 190, 22);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    setEllipsis(t, title, 188);
    lv_obj_set_style_text_color(t, ST_TEXT, 0);
    lv_obj_set_style_text_font(t, fontValue(), 0);

    lv_obj_t *s = lv_label_create(row);
    lv_obj_set_pos(s, 12, 25);
    lv_obj_set_size(s, 190, 16);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    setEllipsis(s, subtitle, 188);
    lv_obj_set_style_text_color(s, ST_MUTED, 0);
    lv_obj_set_style_text_font(s, fontSmall(), 0);

    lv_obj_t *go = lv_label_create(row);
    lv_obj_set_pos(go, 210, 12);
    lv_obj_set_size(go, 44, 22);
    lv_obj_set_style_text_align(go, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(go, "OPEN");
    lv_obj_set_style_text_color(go, ST_TEAL, 0);
    lv_obj_set_style_text_font(go, fontSmall(), 0);

    return row;
}

static void refresh_test()
{
    if (!g_dev) return;
    setValue(g_test.touch, g_dev->Tp.isReady() ? "OK" : "Not detected");
    setDanger(g_test.touch, !g_dev->Tp.isReady());
}

// static lv_obj_t *buildTest(lv_obj_t *parent)
// {
//     lv_obj_t *root = shell(parent, Test);
//     lv_obj_t *page = STheme::page(root);

//     section(page, "HARDWARE TEST");
//     entry_row(page, "Touch test", "Full screen, live coordinates", touch_test_open_cb);
//     entry_row(page, "Display test", "Full screen colour patterns", display_test_open_cb);

//     section(page, "DEVICE");
//     infoRow(page, "Multi-touch", "Not supported");
//     infoRow(page, "Touch IC", "CST816D");

//     esp_chip_info_t chip;
//     esp_chip_info(&chip);
//     char buf[32];
//     snprintf(buf, sizeof(buf), "ESP32-S3 rev %d", chip.revision);
//     infoRow(page, "Chip", buf);
//     uint32_t flash = 0;
//     esp_flash_get_size(NULL, &flash);
//     snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(flash / (1024 * 1024)));
//     infoRow(page, "Flash", buf);
//     snprintf(buf, sizeof(buf), "%u MB", (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
//     infoRow(page, "PSRAM", buf);
//     infoRow(page, "Display", "ST7789V 280x240");
//     infoRow(page, "PMIC", "AXP173");

//     refresh_test();
//     return root;
// }

/* ========================================================================= */
/* Display page                                                              */
/* ========================================================================= */

static struct {
    lv_obj_t *slider = nullptr;
    lv_obj_t *pct = nullptr;
    lv_obj_t *touch = nullptr;
    lv_obj_t *res = nullptr;
    lv_obj_t *driver = nullptr;
    lv_obj_t *touchIc = nullptr;
    lv_obj_t *lvgl = nullptr;
} g_disp;

static void brightness_cb(lv_event_t *e)
{
    if (!g_dev) return;
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t pct = lv_slider_get_value(slider);
    uint8_t duty = (uint8_t)((pct * 255) / 100);
    g_dev->Lcd.setBrightness(duty);
    set_text_buf(g_disp.pct, "%d%%", (int)pct);
}

static void refresh_display()
{
    if (!g_dev) return;
    uint16_t pct = (uint16_t)g_dev->Lcd.getBrightness() * 100 / 255;
    if (g_disp.slider)
        lv_slider_set_value(g_disp.slider, pct, LV_ANIM_OFF);
    set_text_buf(g_disp.pct, "%u%%", (unsigned)pct);
    setValue(g_disp.touch, g_dev->Tp.isReady() ? "OK" : "Not detected");
    setDanger(g_disp.touch, !g_dev->Tp.isReady());
}

static lv_obj_t *buildDisplay(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, Display);
    lv_obj_t *page = STheme::page(root);

    uint16_t pct = (uint16_t)g_dev->Lcd.getBrightness() * 100 / 255;
    sliderRow(page, "Brightness", 0, 100, pct, brightness_cb, nullptr,
               &g_disp.slider, &g_disp.pct);
    
    section(page, "HARDWARE TEST");
    entry_row(page, "Touch test", "Full screen, live coordinates", touch_test_open_cb);
    entry_row(page, "Display test", "Full screen colour patterns", display_test_open_cb);


    section(page, "PANEL");
    infoRow(page, "Multi-touch", "Not supported");
    g_disp.res = infoRow(page, "Display", "280 x 240").value;
    g_disp.driver = infoRow(page, "Driver IC", "ST7789V").value;
    g_disp.touchIc = infoRow(page, "Touch IC", "CST816D").value;
    g_disp.touch = infoRow(page, "Touch status", "OK").value;
    g_disp.lvgl = infoRow(page, "LVGL", LVGL_VERSION_INFO).value;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char buf[32];
    snprintf(buf, sizeof(buf), "ESP32-S3 rev %d", chip.revision);
    infoRow(page, "Chip", buf);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(flash / (1024 * 1024)));
    infoRow(page, "Flash", buf);
    snprintf(buf, sizeof(buf), "%u MB", (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
    infoRow(page, "PSRAM", buf);
    
    
    section(page, "NOTES");
    lv_obj_t *note = lv_label_create(page);
    lv_label_set_text(note,
        "Brightness is PWM on the backlight rail (GPIO45) and is shared "
        "with the launcher quick control. Auto-brightness is not supported "
        "by the current hardware.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_style_text_color(note, ST_MUTED, 0);
    lv_obj_set_style_text_font(note, fontSmall(), 0);
    lv_obj_set_style_pad_left(note, 4, 0);

    refresh_display();
    return root;
}

/* ========================================================================= */
/* About page                                                                */
/* ========================================================================= */

static struct {
    lv_obj_t *uptime = nullptr;
    lv_obj_t *heap = nullptr;
} g_about;

static void refresh_about()
{
    if (!g_dev) return;
    uint32_t sec = millis() / 1000;
    char b[40];
    snprintf(b, sizeof(b), "%lud %02lu:%02lu", (unsigned long)(sec / 86400),
             (unsigned long)((sec % 86400) / 3600), (unsigned long)((sec % 3600) / 60));
    setValue(g_about.uptime, b);
    snprintf(b, sizeof(b), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
    setValue(g_about.heap, b);
}

static lv_obj_t *buildAbout(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, About);
    lv_obj_t *page = STheme::page(root);

    section(page, "DEVICE");
    infoRow(page, "Model", "Chappie 2");
    infoRow(page, "MCU", "ESP32-S3");
    infoRow(page, "Flash / PSRAM", "16 MB / 8 MB");
    infoRow(page, "Display", "1.69in 240x280");
    infoRow(page, "Touch", "CST816D");
    infoRow(page, "PMIC", "AXP173");
    infoRow(page, "RTC", "BM8563");

    section(page, "SOFTWARE");
    infoRow(page, "Firmware", "V1.1");
    infoRow(page, "LVGL", LVGL_VERSION_INFO);
    infoRow(page, "Build", __DATE__ " ");

    section(page, "RUNTIME");
    g_about.uptime = infoRow(page, "Uptime", "--").value;
    g_about.heap = infoRow(page, "Free heap", "--").value;

    refresh_about();
    return root;
}

/* ========================================================================= */
/* Dispatch                                                                  */
/* ========================================================================= */

lv_obj_t *buildPage(PageId page, lv_obj_t *parent, CHAPPIE *dev)
{
    g_dev = dev;
    switch (page) {
    case Wifi: return buildWifi(parent);
    case Bluetooth: return buildBluetooth(parent);
    case Time: return buildTime(parent);
    case Display: return buildDisplay(parent);
    case Battery: return buildBattery(parent);
    case Keys: return buildKeys(parent);
    // case Test: return buildTest(parent);
    case About: return buildAbout(parent);
    case Root:
    default: return buildRoot(parent, dev);
    }
}

void setNavHandler(NavHandler handler) { g_nav = handler; }
void setBackHandler(BackHandler handler) { g_back = handler; }

void refresh()
{
    if (!g_dev) return;
    switch (g_current) {
    case Root: refresh_root(); break;
    case Wifi: refresh_wifi(); break;
    case Bluetooth: refresh_ble(); break;
    case Time: refresh_time(); break;
    case Display: refresh_display(); break;
    case Battery: refresh_battery(); break;
    case Keys: break;
    // case Test: refresh_test(); break;
    case About: refresh_about(); break;
    default: break;
    }
}

void setCurrent(PageId page) { g_current = page; }

bool handleBack()
{
    /* Full-screen hardware tests own B first: they cover the whole display
     * and must return to the Device page, not to the carousel. */
    if (g_touchTest) {
        lv_obj_del(g_touchTest);
        g_touchTest = nullptr;
        return true;
    }
    if (g_dispTest) {
        lv_obj_del(g_dispTest);
        g_dispTest = nullptr;
        return true;
    }
    if (g_pwOverlay) {
        close_password_dialog();
        return true;
    }
    if (g_confirmOverlay) {
        confirm_close();
        return true;
    }
    if (g_current != Root) {
        if (g_back)
            g_back();
        return true;
    }
    return false;
}

void destroy()
{
    close_password_dialog();
    confirm_close();
    if (g_touchTest) {
        lv_obj_del(g_touchTest);
        g_touchTest = nullptr;
    }
    if (g_dispTest) {
        lv_obj_del(g_dispTest);
        g_dispTest = nullptr;
    }
    if (g_carView) {
        lv_anim_del(g_carView, car_anim_exec);
    }
    g_dev = nullptr;
    g_current = Root;

    /* Drop pointers into the page tree that is about to be deleted. */
    g_carView = nullptr;
    g_carRailFill = nullptr;
    g_carIndexLabel = nullptr;
    g_carAnimating = false;
    g_carSel = 0;
    g_carFrom = 0;
    g_carDir = 0;
    g_carPendingDir = 0;
    g_carHomeIndex = 0;
    g_carRailFrom = 0;
    g_carRailTo = 0;
    for (int slot = 0; slot < 3; slot++) {
        g_carRows[slot] = {};
        g_carSlotIndex[slot] = -1;
    }

    g_wifi = {};
    g_ble = {};
    g_time = {};
    g_disp = {};
    g_bat = {};
    g_test = {};
    g_about = {};
}

} // namespace SettingsPages
