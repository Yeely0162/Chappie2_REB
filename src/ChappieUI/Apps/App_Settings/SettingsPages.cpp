#include "SettingsPages.h"
#include "SettingsTheme.h"
#include "../../Resource/ResourcePool.h"

#include <stdio.h>
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

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (g_back)
        g_back();
}

static const char *module_icon_for_title(const char *title)
{
    if (strcmp(title, "Wi-Fi") == 0) return "wifi_dark";
    if (strcmp(title, "Bluetooth") == 0) return "bluetooth_dark";
    if (strcmp(title, "Time") == 0) return "clock_dark";
    if (strcmp(title, "Display") == 0) return "monitor_dark";
    if (strcmp(title, "Battery") == 0) return "battery_dark";
    if (strcmp(title, "Buttons") == 0) return "system_info";
    if (strcmp(title, "Device test") == 0) return "ok_dark";
    if (strcmp(title, "About") == 0) return "about_dark";
    return nullptr;
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
 * @brief Sub-page shell. The header carries the module icon and the same thin
 *        orange/teal bar as the home carousel, so every module keeps the same
 *        visual language instead of falling back to a generic list heading.
 */
static lv_obj_t *shell(lv_obj_t *parent, const char *title)
{
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
    lv_obj_set_size(hdr, SCREEN_W, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, ST_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_column(hdr, 5, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_set_size(back, 28, 28);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *backIcon = lv_label_create(back);
    lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backIcon, ST_TEXT, 0);
    lv_obj_set_style_text_font(backIcon, &lv_font_montserrat_18, 0);
    lv_obj_center(backIcon);

    const char *iconName = module_icon_for_title(title);
    if (iconName) {
        lv_obj_t *icon = lv_img_create(hdr);
        lv_img_set_src(icon, module_icon(iconName));
        lv_img_set_zoom(icon, 146);       /* 35 px -> 20 px */
        lv_obj_set_style_img_recolor(icon, ST_TEXT, 0);
        lv_obj_set_style_img_recolor_opa(icon, 255, 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *bar = lv_obj_create(hdr);
        lv_obj_set_size(bar, 3, 18);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *top = lv_obj_create(bar);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 3, 9);
        lv_obj_set_style_bg_color(top, ST_ORANGE, 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_set_style_radius(top, 1, 0);
        lv_obj_t *bottom = lv_obj_create(bar);
        lv_obj_set_pos(bottom, 0, 9);
        lv_obj_set_size(bottom, 3, 9);
        lv_obj_set_style_bg_color(bottom, ST_TEAL, 0);
        lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bottom, 0, 0);
        lv_obj_set_style_radius(bottom, 1, 0);
    }

    lv_obj_t *label = lv_label_create(hdr);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);

    return root;
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

constexpr int MODULE_COUNT = 8;

struct ModuleDef {
    const char *title;
    const char *icon;
    const char *labels[4];
};

static const ModuleDef MODULES[MODULE_COUNT] = {
    {"Wi-Fi",       "wifi_dark",      {"Status", "Network", "IP",     "Mode"}},
    {"Bluetooth",   "bluetooth_dark", {"Status", "Name",    "Paired", "Connected"}},
    {"Time",        "clock_dark",     {"Time",   "Date",    "RTC",    "NTP"}},
    {"Display",     "monitor_dark",   {"Bright", "Panel",   "Driver", "Touch"}},
    {"Battery",     "battery_dark",   {"Charge", "Voltage", "Current","State"}},
    {"Buttons",     "system_info",    {"A key",  "B key",   "Long",   "Mapping"}},
    {"Device test", "ok_dark",        {"Touch",  "Multi",   "Display","Colours"}},
    {"About",       "about_dark",     {"Model",  "MCU",     "Firmware","LVGL"}},
};

lv_obj_t *g_modLayer = nullptr;
lv_obj_t *g_modIcon = nullptr;
lv_obj_t *g_modTitle = nullptr;
lv_obj_t *g_modLabels[4] = {nullptr};
lv_obj_t *g_modValues[4] = {nullptr};
lv_obj_t *g_modCounter = nullptr;
lv_obj_t *g_modDot[8] = {nullptr};
int g_modIndex = 0;
uint32_t g_lastGestureMs = 0;

const lv_color_t COL_DOT_IDLE = lv_color_hex(0x2A3038);

void anim_x_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void refresh_root();

static void update_dots()
{
    for (int i = 0; i < MODULE_COUNT; i++) {
        if (!g_modDot[i]) continue;
        lv_obj_set_style_bg_color(g_modDot[i], (i == g_modIndex) ? ST_TEXT : COL_DOT_IDLE, 0);
        lv_obj_set_width(g_modDot[i], (i == g_modIndex) ? 12 : 6);
    }
}

static void select_module(int index, int slideDir)
{
    if (!g_modLayer) return;
    if (index < 0) index = MODULE_COUNT - 1;
    if (index >= MODULE_COUNT) index = 0;
    g_modIndex = index;

    const ModuleDef &m = MODULES[index];
    if (g_modIcon) {
        lv_img_set_src(g_modIcon, module_icon(m.icon));
    }
    if (g_modTitle) {
        lv_label_set_text(g_modTitle, m.title);
    }
    for (int i = 0; i < 4; i++) {
        if (g_modLabels[i]) lv_label_set_text(g_modLabels[i], m.labels[i]);
        if (g_modValues[i]) lv_label_set_text(g_modValues[i], "--");
    }
    if (g_modCounter) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d / %02d", g_modIndex + 1, MODULE_COUNT);
        lv_label_set_text(g_modCounter, buf);
    }
    update_dots();
    refresh_root();

    if (slideDir != 0) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g_modLayer);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_values(&a, (lv_coord_t)(8 + slideDir * 20), 8);
        lv_anim_set_time(&a, 170);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

static void module_click_cb(lv_event_t *e)
{
    (void)e;
    /* Do not open a page when the click is the tail of a swipe. */
    if (millis() - g_lastGestureMs < 350)
        return;
    if (g_nav)
        g_nav((uint8_t)(g_modIndex + 1));
}

static void module_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        g_lastGestureMs = millis();
        select_module(g_modIndex + 1, +1);
    } else if (dir == LV_DIR_RIGHT) {
        g_lastGestureMs = millis();
        select_module(g_modIndex - 1, -1);
    }
}

static void mod_prev_cb(lv_event_t *e)
{
    (void)e;
    g_lastGestureMs = millis();
    select_module(g_modIndex - 1, -1);
}

static void mod_next_cb(lv_event_t *e)
{
    (void)e;
    g_lastGestureMs = millis();
    select_module(g_modIndex + 1, +1);
}

static void refresh_root()
{
    if (!g_dev || !g_modValues[0]) return;

    switch ((PageId)(g_modIndex + 1)) {
    case Wifi: {
        setText(g_modValues[0], g_dev->Wf.stateText());
        String configured = g_dev->Wf.configuredSSID();
        String network;
        if (g_dev->Wf.isConnected())
            network = g_dev->Wf.currentSSID();
        else if (configured.length())
            network = configured + " (saved)";
        else
            network = "--";
        setText(g_modValues[1], network.c_str());
        setText(g_modValues[2], g_dev->Wf.LocalIP().c_str());
        const char *mode = "Off";
        if (g_dev->Wf.isAPActive() && g_dev->Wf.isConnected()) mode = "AP + STA";
        else if (g_dev->Wf.isAPActive()) mode = "AP";
        else if (g_dev->Wf.isWiFiEnabled()) mode = "STA";
        setText(g_modValues[3], mode);
        break;
    }
    case Bluetooth: {
        setText(g_modValues[0], g_dev->Ble.stateText());
        setText(g_modValues[1], g_dev->Ble.getDeviceName().c_str());
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", g_dev->Ble.getBondCount());
        setText(g_modValues[2], buf);
        String addr = g_dev->Ble.getConnectedAddress();
        setText(g_modValues[3], addr.length() ? addr.c_str() : "--");
        break;
    }
    case Time: {
        I2C_BM8563_TimeTypeDef t;
        I2C_BM8563_DateTypeDef d;
        const bool timeOk = rtc_read_time(t);
        const bool dateOk = rtc_read_date(d);
        if (timeOk) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
            setText(g_modValues[0], buf);
        } else {
            setText(g_modValues[0], "--:--:--");
        }
        if (dateOk) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.date);
            setText(g_modValues[1], buf);
        } else {
            setText(g_modValues[1], timeOk ? "Not set" : "--");
        }
        setText(g_modValues[2], timeOk ? "OK" : "Read error");
        setText(g_modValues[3], g_dev->Wf.ntpStateText());
        break;
    }
    case Display: {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)((uint16_t)g_dev->Lcd.getBrightness() * 100 / 255));
        setText(g_modValues[0], buf);
        setText(g_modValues[1], "280 x 240");
        setText(g_modValues[2], "ST7789V");
        setText(g_modValues[3], g_dev->Tp.isReady() ? "CST816D OK" : "CST816D !");
        break;
    }
    case Battery: {
        if (!g_dev->Pow.dataValid()) {
            setText(g_modValues[0], "--");
            setText(g_modValues[1], "--");
            setText(g_modValues[2], "--");
            setText(g_modValues[3], "No data");
        } else {
            char buf[24];
            snprintf(buf, sizeof(buf), "%u%%", (unsigned)g_dev->Pow.percent());
            setText(g_modValues[0], buf);
            snprintf(buf, sizeof(buf), "%.2f V", g_dev->Pow.voltageV());
            setText(g_modValues[1], buf);
            snprintf(buf, sizeof(buf), "%+.0f mA", g_dev->Pow.currentMA());
            setText(g_modValues[2], buf);
            setText(g_modValues[3], g_dev->Pow.chargeText());
        }
        break;
    }
    case Keys:
        setText(g_modValues[0], "Power dialog");
        setText(g_modValues[1], "Back / home");
        setText(g_modValues[2], "Not mapped");
        setText(g_modValues[3], "A:9  B:0");
        break;
    case Test:
        setText(g_modValues[0], g_dev->Tp.isReady() ? "CST816D OK" : "Not detected");
        setText(g_modValues[1], "Not supported");
        setText(g_modValues[2], "ST7789V");
        setText(g_modValues[3], "5 patterns");
        break;
    case About:
        setText(g_modValues[0], "Chappie 2");
        setText(g_modValues[1], "ESP32-S3 N16R8");
        setText(g_modValues[2], "V1.1");
        setText(g_modValues[3], LVGL_VERSION_INFO);
        break;
    default:
        break;
    }
}

static lv_obj_t *make_module_field(lv_obj_t *parent, lv_obj_t **labelOut)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 24);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, 66);
    lv_obj_set_style_text_color(label, ST_MUTED, 0);
    lv_obj_set_style_text_font(label, fontSmall(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, "--");
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    *labelOut = label;

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_flex_grow(value, 1);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(value, ST_TEXT, 0);
    lv_obj_set_style_text_font(value, fontValue(), 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_label_set_text(value, "--");
    lv_obj_clear_flag(value, LV_OBJ_FLAG_CLICKABLE);
    return value;
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

    header(root, "SETTINGS", nullptr, nullptr);

    /* Module counter, top right. */
    g_modCounter = lv_label_create(root);
    lv_label_set_text(g_modCounter, "01 / 08");
    lv_obj_set_style_text_color(g_modCounter, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_modCounter, fontSmall(), 0);
    lv_obj_set_style_text_letter_space(g_modCounter, 1, 0);
    lv_obj_align(g_modCounter, LV_ALIGN_TOP_RIGHT, -10, 8);

    /* Three-column module area. */
    g_modLayer = lv_obj_create(root);
    lv_obj_set_pos(g_modLayer, 8, 34);
    lv_obj_set_size(g_modLayer, 264, 150);
    lv_obj_set_style_bg_opa(g_modLayer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_modLayer, 0, 0);
    lv_obj_set_style_pad_all(g_modLayer, 0, 0);
    lv_obj_clear_flag(g_modLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_modLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_modLayer, module_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_modLayer, module_gesture_cb, LV_EVENT_GESTURE, nullptr);

    /* 1) Left: large white line icon. */
    g_modIcon = lv_img_create(g_modLayer);
    lv_obj_set_pos(g_modIcon, 0, 46);
    lv_img_set_zoom(g_modIcon, 409);              /* 35 px -> ~56 px */
    lv_obj_set_style_img_recolor(g_modIcon, ST_TEXT, 0);
    lv_obj_set_style_img_recolor_opa(g_modIcon, 255, 0);
    lv_obj_clear_flag(g_modIcon, LV_OBJ_FLAG_CLICKABLE);

    /* 2) Middle: thin dual-colour vertical bar. */
    lv_obj_t *bar = lv_obj_create(g_modLayer);
    lv_obj_set_pos(bar, 76, 8);
    lv_obj_set_size(bar, 4, 134);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *barTop = lv_obj_create(bar);
    lv_obj_set_pos(barTop, 0, 0);
    lv_obj_set_size(barTop, 4, 66);
    lv_obj_set_style_bg_color(barTop, ST_ORANGE, 0);
    lv_obj_set_style_bg_opa(barTop, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(barTop, 2, 0);
    lv_obj_set_style_border_width(barTop, 0, 0);
    lv_obj_clear_flag(barTop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *barBottom = lv_obj_create(bar);
    lv_obj_set_pos(barBottom, 0, 68);
    lv_obj_set_size(barBottom, 4, 66);
    lv_obj_set_style_bg_color(barBottom, ST_TEAL, 0);
    lv_obj_set_style_bg_opa(barBottom, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(barBottom, 2, 0);
    lv_obj_set_style_border_width(barBottom, 0, 0);
    lv_obj_clear_flag(barBottom, LV_OBJ_FLAG_CLICKABLE);

    /* 3) Right: title + field/value lines. */
    lv_obj_t *info = lv_obj_create(g_modLayer);
    lv_obj_set_pos(info, 88, 0);
    lv_obj_set_size(info, 176, 150);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_pad_row(info, 0, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_CLICKABLE);

    g_modTitle = lv_label_create(info);
    lv_obj_set_width(g_modTitle, LV_PCT(100));
    lv_obj_set_style_text_color(g_modTitle, ST_TEXT, 0);
    lv_obj_set_style_text_font(g_modTitle, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(g_modTitle, 1, 0);
    lv_label_set_text(g_modTitle, "Wi-Fi");
    lv_obj_set_style_pad_bottom(g_modTitle, 3, 0);
    lv_obj_clear_flag(g_modTitle, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 4; i++) {
        g_modValues[i] = make_module_field(info, &g_modLabels[i]);
    }

    lv_obj_t *hint = lv_label_create(info);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(hint, ST_MUTED, 0);
    lv_obj_set_style_text_font(hint, fontSmall(), 0);
    lv_label_set_text(hint, "TAP TO OPEN  " LV_SYMBOL_RIGHT);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE);

    /* Bottom navigation: chevrons + module dots. */
    lv_obj_t *prev = lv_btn_create(root);
    lv_obj_set_pos(prev, 8, 192);
    lv_obj_set_size(prev, 34, 34);
    lv_obj_set_style_bg_color(prev, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(prev, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(prev, 17, 0);
    lv_obj_set_style_border_width(prev, 0, 0);
    lv_obj_set_style_shadow_width(prev, 0, 0);
    lv_obj_add_event_cb(prev, mod_prev_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *prevLabel = lv_label_create(prev);
    lv_label_set_text(prevLabel, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(prevLabel, ST_TEXT, 0);
    lv_obj_set_style_text_font(prevLabel, &lv_font_montserrat_18, 0);
    lv_obj_center(prevLabel);

    lv_obj_t *next = lv_btn_create(root);
    lv_obj_set_pos(next, 238, 192);
    lv_obj_set_size(next, 34, 34);
    lv_obj_set_style_bg_color(next, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(next, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(next, 17, 0);
    lv_obj_set_style_border_width(next, 0, 0);
    lv_obj_set_style_shadow_width(next, 0, 0);
    lv_obj_add_event_cb(next, mod_next_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *nextLabel = lv_label_create(next);
    lv_label_set_text(nextLabel, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(nextLabel, ST_TEXT, 0);
    lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_18, 0);
    lv_obj_center(nextLabel);

    lv_obj_t *dots = lv_obj_create(root);
    lv_obj_set_pos(dots, 84, 203);
    lv_obj_set_size(dots, 112, 12);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_style_pad_column(dots, 5, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < MODULE_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_color(dot, COL_DOT_IDLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        g_modDot[i] = dot;
    }

    select_module(g_modIndex, 0);
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
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, ssid.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, ST_TEXT, 0);
        lv_obj_set_style_text_font(name, fontSmall(), 0);

        lv_obj_t *lock = lv_label_create(row);
        lv_label_set_text(lock, secure ? "WPA" : "OPEN");
        lv_obj_set_style_text_color(lock, secure ? ST_MUTED : ST_TEAL, 0);
        lv_obj_set_style_text_font(lock, fontSmall(), 0);

        char rb[16];
        snprintf(rb, sizeof(rb), "%d", (int)rssi);
        lv_obj_t *sig = lv_label_create(row);
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
    setValue(g_wifi.network, net.c_str());
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
    lv_obj_t *root = shell(parent, "Wi-Fi");
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
    g_wifi.apName = infoRow(page, "AP name", "Chappie-AP").value;
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
    lv_label_set_text(hint,
        "1. Turn on Config AP\n2. Join the Chappie-AP hotspot\n"
        "3. Open http://192.168.4.1\n4. Pick a network and connect\n\n"
        "AP password: chappie123");
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
        lv_label_set_text(l, addr.c_str());
        lv_obj_set_flex_grow(l, 1);
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
    setValue(g_ble.name, g_dev->Ble.getDeviceName().c_str());
    String addr = g_dev->Ble.getConnectedAddress();
    setValue(g_ble.addr, addr.length() ? addr.c_str() : "--");

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
    lv_obj_t *root = shell(parent, "Bluetooth");
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
    lv_obj_t *root = shell(parent, "Time");
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
    lv_obj_t *root = shell(parent, "Display");
    lv_obj_t *page = STheme::page(root);

    uint16_t pct = (uint16_t)g_dev->Lcd.getBrightness() * 100 / 255;
    sliderRow(page, "Brightness", 0, 100, pct, brightness_cb, nullptr,
               &g_disp.slider, &g_disp.pct);

    section(page, "PANEL");
    g_disp.res = infoRow(page, "Resolution", "280 x 240").value;
    g_disp.driver = infoRow(page, "Driver IC", "ST7789V").value;
    g_disp.touchIc = infoRow(page, "Touch IC", "CST816D").value;
    g_disp.touch = infoRow(page, "Touch status", "OK").value;
    g_disp.lvgl = infoRow(page, "LVGL", LVGL_VERSION_INFO).value;

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
    lv_obj_t *root = shell(parent, "Battery");
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
    lv_obj_t *root = shell(parent, "Buttons");
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
/* Device test page                                                          */
/* ========================================================================= */

static struct {
    lv_obj_t *pad = nullptr;
    lv_obj_t *dot = nullptr;
    lv_obj_t *coord = nullptr;
    lv_obj_t *touch = nullptr;
} g_test;

static lv_obj_t *g_colorOverlay = nullptr;
static lv_timer_t *g_colorTimer = nullptr;

static void color_overlay_delete(lv_event_t *e)
{
    (void)e;
    if (g_colorTimer) {
        lv_timer_del(g_colorTimer);
        g_colorTimer = nullptr;
    }
    g_colorOverlay = nullptr;
}

static void color_overlay_click(lv_event_t *e)
{
    lv_obj_del(lv_event_get_target(e));
}

static void color_overlay_timeout(lv_timer_t *t)
{
    g_colorTimer = nullptr;         /* auto-deleted by repeat_count = 1 */
    lv_obj_t *o = (lv_obj_t *)t->user_data;
    if (o)
        lv_obj_del(o);
}

static void open_color_test(lv_color_t color, lv_color_t hint_color)
{
    if (g_colorOverlay)
        lv_obj_del(g_colorOverlay);

    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    g_colorOverlay = o;

    lv_obj_add_event_cb(o, color_overlay_delete, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(o, color_overlay_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hint = lv_label_create(o);
    lv_label_set_text(hint, "Tap to exit");
    lv_obj_set_style_text_color(hint, hint_color, 0);
    lv_obj_set_style_text_font(hint, fontValue(), 0);
    lv_obj_center(hint);

    g_colorTimer = lv_timer_create(color_overlay_timeout, 5000, o);
    lv_timer_set_repeat_count(g_colorTimer, 1);
}

static void color_red_cb(lv_event_t *e)   { (void)e; open_color_test(lv_color_hex(0xE5484D), ST_TEXT); }
static void color_green_cb(lv_event_t *e) { (void)e; open_color_test(lv_color_hex(0x18C7A8), ST_BG); }
static void color_blue_cb(lv_event_t *e)  { (void)e; open_color_test(lv_color_hex(0x3B82F6), ST_TEXT); }
static void color_white_cb(lv_event_t *e) { (void)e; open_color_test(lv_color_hex(0xFFFFFF), ST_BG); }
static void color_black_cb(lv_event_t *e) { (void)e; open_color_test(lv_color_hex(0x000000), ST_TEXT); }

static void touch_pad_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING)
        return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
        return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_obj_t *pad = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(pad, &a);
    int x = p.x - a.x1;
    int y = p.y - a.y1;
    int w = a.x2 - a.x1;
    int h = a.y2 - a.y1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > w) x = w;
    if (y > h) y = h;

    set_text_buf(g_test.coord, "X:%d  Y:%d", x, y);
    if (g_test.dot) {
        lv_obj_clear_flag(g_test.dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_test.dot, x - 6, y - 6);
    }
}

static void refresh_test()
{
    if (!g_dev) return;
    setValue(g_test.touch, g_dev->Tp.isReady() ? "OK" : "Not detected");
    setDanger(g_test.touch, !g_dev->Tp.isReady());
}

static void color_button(lv_obj_t *parent, const char *text, lv_color_t bg,
                         lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 34);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_set_style_text_font(l, fontValue(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
}

static lv_obj_t *buildTest(lv_obj_t *parent)
{
    lv_obj_t *root = shell(parent, "Device test");
    lv_obj_t *page = STheme::page(root);

    section(page, "TOUCH");
    g_test.touch = infoRow(page, "Touch controller", "OK").value;
    infoRow(page, "Multi-touch", "Not supported");
    infoRow(page, "Touch IC", "CST816D");

    g_test.pad = lv_obj_create(page);
    lv_obj_set_size(g_test.pad, LV_PCT(100), 62);
    lv_obj_set_style_bg_color(g_test.pad, ST_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(g_test.pad, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_test.pad, 9, 0);
    lv_obj_set_style_border_width(g_test.pad, 1, 0);
    lv_obj_set_style_border_color(g_test.pad, ST_LINE, 0);
    lv_obj_set_style_pad_all(g_test.pad, 0, 0);
    lv_obj_clear_flag(g_test.pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_test.pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_test.pad, touch_pad_cb, LV_EVENT_ALL, nullptr);

    g_test.coord = lv_label_create(g_test.pad);
    lv_label_set_text(g_test.coord, "Touch here");
    lv_obj_set_style_text_color(g_test.coord, ST_MUTED, 0);
    lv_obj_set_style_text_font(g_test.coord, fontSmall(), 0);
    lv_obj_align(g_test.coord, LV_ALIGN_CENTER, 0, 0);

    g_test.dot = lv_obj_create(g_test.pad);
    lv_obj_set_size(g_test.dot, 12, 12);
    lv_obj_set_style_bg_color(g_test.dot, ST_TEAL, 0);
    lv_obj_set_style_bg_opa(g_test.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_test.dot, 6, 0);
    lv_obj_set_style_border_width(g_test.dot, 0, 0);
    lv_obj_clear_flag(g_test.dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_test.dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_test.dot, LV_OBJ_FLAG_HIDDEN);

    section(page, "DISPLAY");
    color_button(page, "Red", lv_color_hex(0xE5484D), ST_TEXT, color_red_cb);
    color_button(page, "Green", lv_color_hex(0x18C7A8), ST_BG, color_green_cb);
    color_button(page, "Blue", lv_color_hex(0x3B82F6), ST_TEXT, color_blue_cb);
    color_button(page, "White", lv_color_hex(0xFFFFFF), ST_BG, color_white_cb);
    color_button(page, "Black", lv_color_hex(0x000000), ST_TEXT, color_black_cb);

    section(page, "HARDWARE INFORMATION");
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
    infoRow(page, "Display", "ST7789V 280x240");
    infoRow(page, "Touch", "CST816D");
    infoRow(page, "PMIC", "AXP173");

    refresh_test();
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
    lv_obj_t *root = shell(parent, "About");
    lv_obj_t *page = STheme::page(root);

    section(page, "DEVICE");
    infoRow(page, "Model", "Chappie 2");
    infoRow(page, "MCU", "ESP32-S3 N16R8");
    infoRow(page, "Flash / PSRAM", "16 MB / 8 MB");
    infoRow(page, "Display", "1.69in 240x280");
    infoRow(page, "Touch", "CST816D");
    infoRow(page, "PMIC", "AXP173");
    infoRow(page, "RTC", "BM8563");

    section(page, "SOFTWARE");
    infoRow(page, "Firmware", "V1.1");
    infoRow(page, "LVGL", LVGL_VERSION_INFO);
    infoRow(page, "Build", __DATE__ " " __TIME__);

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
    case Test: return buildTest(parent);
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
    case Test: refresh_test(); break;
    case About: refresh_about(); break;
    default: break;
    }
}

void setCurrent(PageId page) { g_current = page; }

bool handleBack()
{
    if (g_pwOverlay) {
        close_password_dialog();
        return true;
    }
    if (g_confirmOverlay) {
        confirm_close();
        return true;
    }
    if (g_colorOverlay) {
        lv_obj_del(g_colorOverlay);      /* delete handler releases the timer */
        g_colorOverlay = nullptr;
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
    if (g_colorOverlay) {
        lv_obj_del(g_colorOverlay);   /* delete handler releases the timer */
        g_colorOverlay = nullptr;
    }
    if (g_colorTimer) {
        lv_timer_del(g_colorTimer);
        g_colorTimer = nullptr;
    }
    g_dev = nullptr;
    g_current = Root;

    /* Drop pointers into the page tree that is about to be deleted. */
    g_modLayer = nullptr;
    g_modIcon = nullptr;
    g_modTitle = nullptr;
    g_modCounter = nullptr;
    for (int i = 0; i < 4; i++) {
        g_modLabels[i] = nullptr;
        g_modValues[i] = nullptr;
    }
    for (int i = 0; i < 8; i++)
        g_modDot[i] = nullptr;

    g_wifi = {};
    g_ble = {};
    g_time = {};
    g_disp = {};
    g_bat = {};
    g_test = {};
    g_about = {};
}

} // namespace SettingsPages
