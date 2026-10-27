/**
 * @file SyUi.cpp
 * @brief Synology monitor: pages, gesture navigation and refresh timer.
 *
 * Pages: Home, Settings, Storage, System, UPS.
 * Gestures: down = Settings, left = Storage, right = System, up = Home.
 *
 * The CPU and RAM meters on the home page are the protected part of the app.
 * ArcCreate() and Content_Update() below are the original implementations and
 * are used exactly as before: same size, position, ticks, colours, animation
 * and centre label. Only the network block underneath them was rebuilt.
 */
#include "SyCommon.h"

#include "../../../ChappieBsp/Chappie.h"
#include "../../Resource/ResourcePool.h"

#include <string.h>

namespace Sy {

/* ---------------------------------------------------------------- palette */
#define SY_BG        lv_color_hex(0x000000)   /* home keeps the original look */
#define SY_DARK      lv_color_hex(0x0A0C0F)   /* sub pages */
#define SY_PANEL     lv_color_hex(0x121519)
#define SY_PANEL_HI  lv_color_hex(0x181D23)
#define SY_LINE      lv_color_hex(0x1E2329)
#define SY_TEXT      lv_color_hex(0xF2F4F7)
#define SY_MUTED     lv_color_hex(0x8A919B)
#define SY_TEAL      lv_color_hex(0x18C7A8)
#define SY_AMBER     lv_color_hex(0xFFB02E)
#define SY_RED       lv_color_hex(0xE5484D)
#define SY_GREEN     lv_color_hex(0x53DE77)

#define SY_W 280
#define SY_H 240

#define SY_CHART_POINTS 48

/* ---------------------------------------------------------------- fonts */
static const lv_font_t *fSmall()
{
    return &lv_font_montserrat_12;
}
static const lv_font_t *fText()
{
    lv_font_t *f = ResourcePool::GetFont("bahnschrift_13");
    return f ? f : &lv_font_montserrat_12;
}
static const lv_font_t *fValue()
{
    lv_font_t *f = ResourcePool::GetFont("bahnschrift_17");
    return f ? f : &lv_font_montserrat_12;
}
static const lv_font_t *fBold18()
{
    lv_font_t *f = ResourcePool::GetFont("UbuntuBold18");
    return f ? f : &lv_font_montserrat_12;
}

/* ================================================================ meters */
/* ---- original code, unchanged: do not redesign these two meters ---- */

struct MeterInfo {
    lv_obj_t *METER;
    lv_meter_indicator_t *INDIC;
    int16_t Meter_val_old = 0;
};

static MeterInfo Meter, Meter1, Meter2;
static lv_anim_t anim;

static void set_meter_cpu_value(void *indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter1.METER, Meter1.INDIC, v);
}
static void set_meter_ram_value(void *indic, int32_t v)
{
    lv_meter_set_indicator_end_value(Meter2.METER, Meter2.INDIC, v);
}

static MeterInfo ArcCreate(lv_obj_t *obj, const char *text, int16_t value, int x)
{
    Meter.METER = lv_meter_create(obj);
    lv_obj_remove_style(Meter.METER, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(Meter.METER, 0, 0);
    lv_obj_set_style_border_opa(Meter.METER, 0, 0);
    lv_obj_set_style_radius(Meter.METER, 1, 0);
    lv_meter_scale_t *scale_min1 = lv_meter_add_scale(Meter.METER);
    lv_obj_set_size(Meter.METER, 150, 150);
    lv_obj_set_pos(Meter.METER, 0, 0);

    lv_meter_set_scale_ticks(Meter.METER, scale_min1, 50, 2, 10, lv_color_hex(0x4C4C4C));
    lv_meter_set_scale_range(Meter.METER, scale_min1, 0, 100, 270, 135);

    Meter.INDIC = lv_meter_add_scale_lines(Meter.METER, scale_min1, lv_color_hex(0x85FFBD),
                                           lv_color_hex(0xFF6E3B), false, 0);
    lv_meter_set_indicator_end_value(Meter.METER, (lv_meter_indicator_t *)Meter.INDIC, value);
    lv_obj_set_align(Meter.METER, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(Meter.METER, x);
    lv_obj_set_y(Meter.METER, -3);

    lv_obj_t *tvalue = lv_label_create(Meter.METER);
    lv_label_set_text_fmt(tvalue, "%d%%", value);
    lv_obj_set_style_text_font(tvalue, fBold18(), 0);
    lv_obj_set_style_text_color(tvalue, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_align(tvalue, LV_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(Meter.METER);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(t, text);
    lv_obj_set_y(t, -10);
    lv_obj_set_align(t, LV_ALIGN_BOTTOM_MID);

    return Meter;
}

static void Content_Update(MeterInfo *meterobj, int16_t value, bool check)
{
    lv_anim_init(&anim);

    if (check) {
        lv_anim_set_exec_cb(&anim, set_meter_cpu_value);
    } else {
        lv_anim_set_exec_cb(&anim, set_meter_ram_value);
    }

    lv_anim_set_values(&anim, meterobj->Meter_val_old, value);
    lv_anim_set_time(&anim, 700);
    lv_anim_set_var(&anim, meterobj->INDIC);
    lv_anim_start(&anim);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_label_set_text_fmt(lv_obj_get_child(meterobj->METER, 0), "%d %%", value);
    meterobj->Meter_val_old = value;
}

/* =============================================================== helpers */
static lv_obj_t *s_scr[PAGE_MAX];
static PageId    s_cur = PAGE_HOME;
static uint32_t  s_navBlockUntil = 0;
static CHAPPIE  *s_dev = nullptr;

static void gesture_cb(lv_event_t *e);
static void updateHome(const Snapshot &s, bool force);

static void setTextIfChanged(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *old = lv_label_get_text(label);
    if (!old || strcmp(old, text) != 0) lv_label_set_text(label, text);
}

static void mkLabel(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                    const lv_font_t *font, lv_color_t color, lv_obj_t **out)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    if (out) *out = l;
}

static lv_obj_t *mkButton(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_color_t fg,
                          lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, fText(), 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *mkPanel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_coord_t radius)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, radius, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *newScreen(lv_color_t bg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);
    return scr;
}

static void homeBtn_cb(lv_event_t *e)
{
    (void)e;
    UiNavigate(PAGE_HOME, true);
}

static lv_obj_t *pageHeader(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = mkPanel(scr, 0, 0, SY_W, 26, SY_DARK, 0);
    mkLabel(bar, title, 8, 3, fValue(), SY_TEXT, nullptr);

    lv_obj_t *home = mkButton(bar, "HOME", 224, 3, 50, 20, SY_PANEL_HI, SY_MUTED, homeBtn_cb, nullptr);
    (void)home;
    return bar;
}

/* --------------------------------------------------------- formatting */
static void fmtBytes(uint64_t bytes, char *out, size_t n)
{
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double tb = gb * 1024.0;

    if (bytes >= (uint64_t)tb)      snprintf(out, n, "%.2f TB", bytes / tb);
    else if (bytes >= (uint64_t)gb) snprintf(out, n, "%.1f GB", bytes / gb);
    else if (bytes >= (uint64_t)mb) snprintf(out, n, "%.0f MB", bytes / mb);
    else                            snprintf(out, n, "%.0f KB", bytes / kb);
}

static void fmtRate(float bps, char *num, size_t nNum, const char **unit)
{
    if (!(bps >= 0.0f)) bps = 0.0f;
    if (bps < 1024.0f) {
        snprintf(num, nNum, "%.0f", bps);
        *unit = "B/s";
    } else if (bps < 1024.0f * 1024.0f) {
        snprintf(num, nNum, "%.1f", bps / 1024.0f);
        *unit = "KB/s";
    } else if (bps < 1024.0f * 1024.0f * 1024.0f) {
        snprintf(num, nNum, "%.2f", bps / (1024.0f * 1024.0f));
        *unit = "MB/s";
    } else {
        snprintf(num, nNum, "%.2f", bps / (1024.0f * 1024.0f * 1024.0f));
        *unit = "GB/s";
    }
}

static void fmtUptime(const char *raw, char *out, size_t n)
{
    out[0] = 0;
    if (!raw || !raw[0]) return;

    long h = 0, m = 0, sec = 0;
    if (sscanf(raw, "%ld:%ld:%ld", &h, &m, &sec) < 1) return;
    if (h < 0) h = 0;

    if (h >= 24) snprintf(out, n, "%ldd %ldh %ldm", h / 24, h % 24, m);
    else         snprintf(out, n, "%ldh %ldm", h, m);
}

static lv_color_t usageColor(int pct)
{
    if (pct >= 90) return SY_RED;
    if (pct >= 75) return SY_AMBER;
    return SY_TEAL;
}

/* ============================================================ home page */
static lv_obj_t *s_timeLabel;
static lv_obj_t *s_dlVal, *s_dlUnit, *s_ulVal, *s_ulUnit;
static lv_obj_t *s_netState, *s_axisLbl, *s_dot;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_serDl, *s_serUl;

static void buildHome()
{
    lv_obj_t *scr = newScreen(SY_BG);
    s_scr[PAGE_HOME] = scr;

    /* --- protected: the two resource meters, exactly as before --- */
    Meter1 = ArcCreate(scr, "CPU", 1, -2);
    Meter2 = ArcCreate(scr, "RAM", 1, 135);

    s_timeLabel = lv_label_create(scr);
    lv_obj_set_align(s_timeLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_font(s_timeLabel, fBold18(), 0);
    lv_label_set_text(s_timeLabel, "Init...");
    lv_obj_set_style_text_color(s_timeLabel, lv_color_hex(0x8F8F8F), 0);

    /* --- redesigned network block (below the meters, y >= 148) --- */
    lv_obj_t *panel = mkPanel(scr, 6, 148, 268, 88, SY_PANEL, 12);

    /* download */
    mkLabel(panel, "DL", 14, 6, fSmall(), SY_MUTED, nullptr);
    s_dlUnit = nullptr;
    mkLabel(panel, "B/s", 44, 6, fSmall(), SY_MUTED, &s_dlUnit);
    mkLabel(panel, "-", 14, 17, fBold18(), SY_TEAL, &s_dlVal);

    /* upload */
    mkLabel(panel, "UL", 14, 40, fSmall(), SY_MUTED, nullptr);
    mkLabel(panel, "B/s", 44, 40, fSmall(), SY_MUTED, &s_ulUnit);
    mkLabel(panel, "-", 14, 51, fBold18(), SY_AMBER, &s_ulVal);

    /* chart */
    s_chart = lv_chart_create(panel);
    lv_obj_set_pos(s_chart, 102, 18);
    lv_obj_set_size(s_chart, 158, 44);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 0, 0);
    lv_chart_set_point_count(s_chart, SY_CHART_POINTS);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 64);
    s_serDl = lv_chart_add_series(s_chart, SY_TEAL, LV_CHART_AXIS_PRIMARY_Y);
    s_serUl = lv_chart_add_series(s_chart, SY_AMBER, LV_CHART_AXIS_PRIMARY_Y);

    mkLabel(panel, "MAX --", 102, 3, fSmall(), SY_MUTED, &s_axisLbl);
    lv_obj_set_width(s_axisLbl, 158);
    lv_obj_set_style_text_align(s_axisLbl, LV_TEXT_ALIGN_RIGHT, 0);

    /* connection status */
    s_dot = lv_obj_create(panel);
    lv_obj_set_size(s_dot, 7, 7);
    lv_obj_set_pos(s_dot, 12, 75);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, SY_MUTED, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    mkLabel(panel, "INIT", 22, 69, fSmall(), SY_MUTED, &s_netState);
    lv_obj_set_width(s_netState, 238);
    lv_obj_set_style_text_align(s_netState, LV_TEXT_ALIGN_LEFT, 0);
}

static const char *connText(ConnState c)
{
    switch (c) {
    case CONN_ONLINE:      return "ONLINE";
    case CONN_CONNECTING:  return "CONNECTING";
    case CONN_AUTH_FAIL:   return "LOGIN FAILED";
    case CONN_NO_WIFI:     return "WI-FI OFF";
    case CONN_ERROR:       return "NAS UNREACHABLE";
    case CONN_OFFLINE:
    default:               return "NOT CONFIGURED";
    }
}

static lv_color_t connColor(ConnState c)
{
    switch (c) {
    case CONN_ONLINE:     return SY_TEAL;
    case CONN_CONNECTING: return SY_AMBER;
    case CONN_AUTH_FAIL:
    case CONN_ERROR:      return SY_RED;
    default:              return SY_MUTED;
    }
}

/* Chart scale: values are stored in KB/s, or in half-MB steps once the peak
 * passes 4 MB/s, and the axis caption states which unit is in use. */
static uint8_t s_chartMB = 0;
static float   s_chartPeak = 0.0f;

static int niceCeil(int v)
{
    static const int steps[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
                                 8192, 12288, 16384, 24576, 32000 };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (v <= steps[i]) return steps[i];
    }
    return 32000;
}

static void chartPush(float downBps, float upBps)
{
    if (!s_chart) return;

    float peak = downBps > upBps ? downBps : upBps;

    /* Decay the remembered peak so the scale comes back down slowly. */
    s_chartPeak = s_chartPeak * 0.985f;
    if (peak > s_chartPeak) s_chartPeak = peak;

    uint8_t wantMB = (s_chartPeak > 4.0f * 1024.0f * 1024.0f) ? 1 : 0;
    if (wantMB != s_chartMB) {
        s_chartMB = wantMB;
        /* Units changed: old points would be meaningless, start clean. */
        lv_chart_set_all_value(s_chart, s_serDl, 0);
        lv_chart_set_all_value(s_chart, s_serUl, 0);
    }

    /* 1 chart unit = 1 KB/s, or 0.5 MB/s above the switch point. */
    float div = s_chartMB ? (512.0f * 1024.0f) : 1024.0f;
    int dv = (int)(downBps / div + 0.5f);
    int uv = (int)(upBps / div + 0.5f);
    if (dv < 0) dv = 0;
    if (uv < 0) uv = 0;

    lv_chart_set_next_value(s_chart, s_serDl, (lv_coord_t)dv);
    lv_chart_set_next_value(s_chart, s_serUl, (lv_coord_t)uv);

    /* Scale to the visible window. */
    uint32_t cnt = lv_chart_get_point_count(s_chart);
    int32_t maxv = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        if (s_serDl->y_points[i] > maxv) maxv = s_serDl->y_points[i];
        if (s_serUl->y_points[i] > maxv) maxv = s_serUl->y_points[i];
    }
    int top = niceCeil((int)maxv + (maxv / 10));
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);

    char buf[24];
    if (s_chartMB) snprintf(buf, sizeof(buf), "MAX %.1f MB/s", (float)top * 0.5f);
    else           snprintf(buf, sizeof(buf), "MAX %d KB/s", top);
    setTextIfChanged(s_axisLbl, buf);
}

static void updateNetLabels(const Snapshot &s)
{
    char num[16];
    const char *unit = "B/s";
    float down = s.netValid ? s.downBps : 0.0f;
    float up = s.netValid ? s.upBps : 0.0f;

    if (!s.netValid) {
        setTextIfChanged(s_dlVal, "--");
        setTextIfChanged(s_ulVal, "--");
        setTextIfChanged(s_dlUnit, "");
        setTextIfChanged(s_ulUnit, "");
        lv_obj_set_style_text_color(s_dlVal, SY_MUTED, 0);
        lv_obj_set_style_text_color(s_ulVal, SY_MUTED, 0);
        return;
    }

    fmtRate(down, num, sizeof(num), &unit);
    setTextIfChanged(s_dlVal, num);
    setTextIfChanged(s_dlUnit, unit);

    fmtRate(up, num, sizeof(num), &unit);
    setTextIfChanged(s_ulVal, num);
    setTextIfChanged(s_ulUnit, unit);

    /* A stale sample keeps its value but loses its colour, so "58 KB/s" from
     * two minutes ago cannot be mistaken for live traffic. */
    lv_color_t dlc = s.netStale ? SY_MUTED : SY_TEAL;
    lv_color_t ulc = s.netStale ? SY_MUTED : SY_AMBER;
    lv_obj_set_style_text_color(s_dlVal, dlc, 0);
    lv_obj_set_style_text_color(s_ulVal, ulc, 0);
}

static void updateStatusLine(const Snapshot &s)
{
    lv_color_t col = connColor(s.conn);
    lv_obj_set_style_bg_color(s_dot, col, 0);

    char buf[96];
    if (s.conn == CONN_ONLINE) {
        uint32_t age = 0;
        if (s.lastFastMs) age = (millis() - s.lastFastMs) / 1000;
        if (age > 999) age = 999;

        /* Interface name (or a real link-down flag) plus the age of the
         * sample, so a frozen number is never mistaken for live traffic. */
        if (s.linkValid && s.linkName[0] && !s.linkUp)
            snprintf(buf, sizeof(buf), "ONLINE  %s DOWN  %us", s.linkName, (unsigned)age);
        else if (s.nic[0])
            snprintf(buf, sizeof(buf), "ONLINE  %s  %us", s.nic, (unsigned)age);
        else if (s.model[0])
            snprintf(buf, sizeof(buf), "ONLINE  %s  %us", s.model, (unsigned)age);
        else
            snprintf(buf, sizeof(buf), "ONLINE  %us", (unsigned)age);
    } else {
        const char *base = connText(s.conn);
        if (s.msg[0])
            snprintf(buf, sizeof(buf), "%s  %s", base, s.msg);
        else
            snprintf(buf, sizeof(buf), "%s", base);
    }
    setTextIfChanged(s_netState, buf);
    lv_obj_set_style_text_color(s_netState, col, 0);
}

static void updateHome(const Snapshot &s, bool force)
{
    /* clock, once per second */
    static uint32_t lastClock = 0;
    uint32_t now = millis();
    if (s_dev && (force || now - lastClock >= 1000)) {
        lastClock = now;
        I2C_BM8563_TimeTypeDef t = { 0, 0, 0 };
        if (s_dev->Rtc.getTime(&t) && t.hours >= 0 && t.hours <= 23 &&
            t.minutes >= 0 && t.minutes <= 59) {
            char b[8];
            snprintf(b, sizeof(b), "%02d:%02d", t.hours, t.minutes);
            setTextIfChanged(s_timeLabel, b);
        }
    }

    /* meters: only drive the animation when the value actually moved */
    static int16_t lastCpu = -1, lastRam = -1;
    if (s.cpuValid && s.cpuUse != lastCpu) {
        lastCpu = s.cpuUse;
        Content_Update(&Meter1, s.cpuUse, true);
    }
    if (s.ramValid && s.ramUse != lastRam) {
        lastRam = s.ramUse;
        Content_Update(&Meter2, s.ramUse, false);
    }

    if (force) updateNetLabels(s);
    updateStatusLine(s);
}

/* ========================================================= storage page */
#define STOR_CARDS 3
static lv_obj_t *s_storCard[STOR_CARDS];
static lv_obj_t *s_storName[STOR_CARDS];
static lv_obj_t *s_storPct[STOR_CARDS];
static lv_obj_t *s_storBar[STOR_CARDS];
static lv_obj_t *s_storSize[STOR_CARDS];
static lv_obj_t *s_storMsg;
static lv_obj_t *s_storPageLbl;
static lv_obj_t *s_storDisk;
static lv_obj_t *s_storPrev, *s_storNext;
static uint8_t   s_storPage = 0;

static void storApply(const Snapshot &s);

static void storArrow_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);

    Snapshot s;
    if (!DataLock(20)) return;
    s = Data();
    DataUnlock();

    int pages = (s.volCount + STOR_CARDS - 1) / STOR_CARDS;
    if (pages < 1) pages = 1;

    int p = (int)s_storPage + delta;
    if (p < 0) p = 0;
    if (p >= pages) p = pages - 1;
    s_storPage = (uint8_t)p;

    storApply(s);
}

static void upsEntry_cb(lv_event_t *e)
{
    (void)e;
    UiNavigate(PAGE_UPS, true);
}

static void buildStorage()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_STORAGE] = scr;
    pageHeader(scr, "STORAGE");

    for (int i = 0; i < STOR_CARDS; i++) {
        lv_obj_t *card = mkPanel(scr, 6, 30 + i * 54, 268, 50, SY_PANEL, 8);

        s_storName[i] = nullptr;
        mkLabel(card, "-", 8, 3, fValue(), SY_TEXT, &s_storName[i]);
        lv_obj_set_width(s_storName[i], 190);
        lv_label_set_long_mode(s_storName[i], LV_LABEL_LONG_DOT);

        s_storPct[i] = nullptr;
        mkLabel(card, "-", 200, 3, fValue(), SY_TEAL, &s_storPct[i]);
        lv_obj_set_width(s_storPct[i], 60);
        lv_obj_set_style_text_align(s_storPct[i], LV_TEXT_ALIGN_RIGHT, 0);

        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_pos(bar, 8, 24);
        lv_obj_set_size(bar, 252, 6);
        lv_obj_set_style_bg_color(bar, SY_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, SY_TEAL, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        s_storBar[i] = bar;

        s_storSize[i] = nullptr;
        mkLabel(card, "-", 8, 33, fSmall(), SY_MUTED, &s_storSize[i]);
        lv_obj_set_width(s_storSize[i], 250);
        lv_label_set_long_mode(s_storSize[i], LV_LABEL_LONG_DOT);

        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        s_storCard[i] = card;
    }

    s_storMsg = nullptr;
    mkLabel(scr, "Waiting for data...", 8, 100, fText(), SY_MUTED, &s_storMsg);
    lv_obj_set_width(s_storMsg, 264);
    lv_obj_set_style_text_align(s_storMsg, LV_TEXT_ALIGN_CENTER, 0);

    s_storPrev = mkButton(scr, "<", 236, 194, 18, 18, SY_PANEL_HI, SY_MUTED,
                          storArrow_cb, (void *)(intptr_t)-1);
    s_storNext = mkButton(scr, ">", 256, 194, 18, 18, SY_PANEL_HI, SY_MUTED,
                          storArrow_cb, (void *)(intptr_t)1);

    s_storDisk = nullptr;
    mkLabel(scr, "", 8, 196, fSmall(), SY_MUTED, &s_storDisk);
    lv_obj_set_width(s_storDisk, 182);
    lv_label_set_long_mode(s_storDisk, LV_LABEL_LONG_DOT);

    s_storPageLbl = nullptr;
    mkLabel(scr, "1/1", 200, 196, fSmall(), SY_MUTED, &s_storPageLbl);
    lv_obj_set_width(s_storPageLbl, 32);
    lv_obj_set_style_text_align(s_storPageLbl, LV_TEXT_ALIGN_RIGHT, 0);

    mkButton(scr, "UPS STATUS   >", 6, 214, 268, 24, SY_PANEL, SY_TEXT, upsEntry_cb, nullptr);
}

static void storApply(const Snapshot &s)
{
    int pages = (s.volCount + STOR_CARDS - 1) / STOR_CARDS;
    if (pages < 1) pages = 1;
    if (s_storPage >= pages) s_storPage = (uint8_t)(pages - 1);

    if (!s.storageValid) {
        for (int i = 0; i < STOR_CARDS; i++) lv_obj_add_flag(s_storCard[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_storMsg, LV_OBJ_FLAG_HIDDEN);
        setTextIfChanged(s_storMsg, s.conn == CONN_ONLINE ? "Reading volumes..." : "No data");
        setTextIfChanged(s_storPageLbl, "");
        setTextIfChanged(s_storDisk, "");
        lv_obj_add_flag(s_storPrev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_storNext, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s.volCount == 0) {
        for (int i = 0; i < STOR_CARDS; i++) lv_obj_add_flag(s_storCard[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_storMsg, LV_OBJ_FLAG_HIDDEN);
        setTextIfChanged(s_storMsg, "No volume reported by the storage API");
        setTextIfChanged(s_storPageLbl, "");
        setTextIfChanged(s_storDisk, "");
        lv_obj_add_flag(s_storPrev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_storNext, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(s_storMsg, LV_OBJ_FLAG_HIDDEN);

    int first = (int)s_storPage * STOR_CARDS;
    for (int i = 0; i < STOR_CARDS; i++) {
        int idx = first + i;
        if (idx >= (int)s.volCount) {
            lv_obj_add_flag(s_storCard[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_storCard[i], LV_OBJ_FLAG_HIDDEN);

        const VolumeInfo &v = s.vols[idx];
        const char *name = v.name[0] ? v.name : "volume";

        int pct = 0;
        if (v.total > 0) {
            uint64_t used = v.used > v.total ? v.total : v.used;
            pct = (int)((used * 100ULL) / v.total);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
        }

        setTextIfChanged(s_storName[i], name);

        char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        setTextIfChanged(s_storPct[i], pb);
        lv_obj_set_style_text_color(s_storPct[i], usageColor(pct), 0);
        lv_obj_set_style_bg_color(s_storBar[i], usageColor(pct), LV_PART_INDICATOR);
        lv_bar_set_value(s_storBar[i], pct, LV_ANIM_OFF);

        char used[16], total[16], freeb[16];
        fmtBytes(v.used, used, sizeof(used));
        fmtBytes(v.total, total, sizeof(total));
        fmtBytes(v.total > v.used ? v.total - v.used : 0, freeb, sizeof(freeb));

        char line[72];
        if (v.status[0] && strcmp(v.status, "normal") != 0)
            snprintf(line, sizeof(line), "%s / %s  |  %s", used, total, v.status);
        else
            snprintf(line, sizeof(line), "%s / %s  |  %s free", used, total, freeb);
        setTextIfChanged(s_storSize[i], line);

        lv_obj_set_style_text_color(s_storSize[i],
                                    (v.status[0] && strcmp(v.status, "normal") != 0) ? SY_AMBER : SY_MUTED,
                                    0);
    }

    char pl[16];
    snprintf(pl, sizeof(pl), "%u/%d", (unsigned)s_storPage + 1, pages);
    setTextIfChanged(s_storPageLbl, pl);

    if (pages > 1) {
        lv_obj_clear_flag(s_storPrev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_storNext, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_storPrev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_storNext, LV_OBJ_FLAG_HIDDEN);
    }

    /* Disk detail is only shown when the storage response really carried it;
     * DSM 7 does not always include disk_info, so say so instead of guessing. */
    char dl[48];
    if (!s.diskApiValid) {
        snprintf(dl, sizeof(dl), "Disk detail: not in API");
    } else if (s.diskWarn > 0) {
        snprintf(dl, sizeof(dl), "%u disks, %u warning", (unsigned)s.diskCount,
                 (unsigned)s.diskWarn);
    } else if (s.diskTempMax >= 0) {
        snprintf(dl, sizeof(dl), "%u disks OK | %d C", (unsigned)s.diskCount,
                 (int)s.diskTempMax);
    } else {
        snprintf(dl, sizeof(dl), "%u disks OK", (unsigned)s.diskCount);
    }
    setTextIfChanged(s_storDisk, dl);
    lv_obj_set_style_text_color(s_storDisk, s.diskWarn > 0 ? SY_AMBER : SY_MUTED, 0);
}

/* =========================================================== system page */
#define SYS_ROWS 8
static lv_obj_t *s_sysVal[SYS_ROWS];
static lv_obj_t *s_sysNote;

static void buildSystem()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_SYSTEM] = scr;
    pageHeader(scr, "SYSTEM");

    static const char *labels[SYS_ROWS] = { "MODEL", "DSM", "CPU", "CORES",
                                            "MEMORY", "TEMP", "UPTIME", "NETWORK" };

    for (int i = 0; i < SYS_ROWS; i++) {
        lv_obj_t *row = mkPanel(scr, 6, 30 + i * 24, 268, 22, SY_PANEL, 6);
        mkLabel(row, labels[i], 8, 3, fSmall(), SY_MUTED, nullptr);
        s_sysVal[i] = nullptr;
        mkLabel(row, "--", 82, 2, fValue(), SY_TEXT, &s_sysVal[i]);
        lv_obj_set_width(s_sysVal[i], 176);
        lv_obj_set_style_text_align(s_sysVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_sysVal[i], LV_LABEL_LONG_DOT);
    }

    s_sysNote = nullptr;
    mkLabel(scr, "", 8, 224, fSmall(), SY_MUTED, &s_sysNote);
    lv_obj_set_width(s_sysNote, 264);
    lv_label_set_long_mode(s_sysNote, LV_LABEL_LONG_DOT);
}

static void sysApply(const Snapshot &s)
{
    if (!s.sysValid) {
        for (int i = 0; i < SYS_ROWS; i++) setTextIfChanged(s_sysVal[i], "--");
        setTextIfChanged(s_sysNote, s.conn == CONN_ONLINE ? "Reading system information..." : "No data");
        return;
    }

    char b[48];
    setTextIfChanged(s_sysVal[0], s.model[0] ? s.model : "n/a");
    setTextIfChanged(s_sysVal[1], s.dsm[0] ? s.dsm : "n/a");
    setTextIfChanged(s_sysVal[2], s.cpuSeries[0] ? s.cpuSeries : "n/a");

    if (s.cores > 0) snprintf(b, sizeof(b), "%d", (int)s.cores);
    else snprintf(b, sizeof(b), "n/a");
    setTextIfChanged(s_sysVal[3], b);

    if (s.ramMB > 0.0f) snprintf(b, sizeof(b), "%.1f GB", s.ramMB / 1024.0f);
    else snprintf(b, sizeof(b), "n/a");
    setTextIfChanged(s_sysVal[4], b);

    if (s.sysTemp > 0) snprintf(b, sizeof(b), "%d C%s", (int)s.sysTemp, s.tempWarn ? " !" : "");
    else snprintf(b, sizeof(b), "n/a");
    setTextIfChanged(s_sysVal[5], b);
    lv_obj_set_style_text_color(s_sysVal[5], s.tempWarn ? SY_RED : SY_TEXT, 0);

    char up[24];
    fmtUptime(s.uptime, up, sizeof(up));
    setTextIfChanged(s_sysVal[6], up[0] ? up : "n/a");

    if (s.linkValid) {
        /* "ovs_eth0 UP 1000M" - name, link state and negotiated speed. */
        if (s.linkSpeed > 0)
            snprintf(b, sizeof(b), "%s %s %dM", s.linkName[0] ? s.linkName : "nic",
                     s.linkUp ? "UP" : "DOWN", (int)s.linkSpeed);
        else
            snprintf(b, sizeof(b), "%s %s", s.linkName[0] ? s.linkName : "nic",
                     s.linkUp ? "UP" : "DOWN");
        setTextIfChanged(s_sysVal[7], b);
        lv_obj_set_style_text_color(s_sysVal[7], s.linkUp ? SY_TEAL : SY_RED, 0);
    } else {
        setTextIfChanged(s_sysVal[7], "--");
        lv_obj_set_style_text_color(s_sysVal[7], SY_MUTED, 0);
    }

    setTextIfChanged(s_sysNote, s.linkIp[0] ? s.linkIp : "");
}

/* ============================================================== UPS page */
static lv_obj_t *s_upsBig, *s_upsVal[4], *s_upsNote;

static void buildUps()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_UPS] = scr;
    pageHeader(scr, "UPS");

    s_upsBig = nullptr;
    mkLabel(scr, "WAITING", 8, 36, fBold18(), SY_MUTED, &s_upsBig);

    static const char *labels[4] = { "MODEL", "CHARGE", "RUNTIME", "REPORTED" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = mkPanel(scr, 6, 66 + i * 28, 268, 24, SY_PANEL, 6);
        mkLabel(row, labels[i], 8, 4, fSmall(), SY_MUTED, nullptr);
        s_upsVal[i] = nullptr;
        mkLabel(row, "--", 82, 3, fValue(), SY_TEXT, &s_upsVal[i]);
        lv_obj_set_width(s_upsVal[i], 176);
        lv_obj_set_style_text_align(s_upsVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_upsVal[i], LV_LABEL_LONG_DOT);
    }

    s_upsNote = nullptr;
    mkLabel(scr, "Reading UPS...", 8, 184, fSmall(), SY_MUTED, &s_upsNote);
    lv_obj_set_size(s_upsNote, 264, 44);
    lv_label_set_long_mode(s_upsNote, LV_LABEL_LONG_DOT);
}

static void upsApply(const Snapshot &s)
{
    const char *big = "UNKNOWN";
    lv_color_t col = SY_MUTED;
    const char *note = "";

    switch (s.upsState) {
    case UPS_NONE:
        big = "NOT DETECTED";
        col = SY_MUTED;
        note = "DSM reports no UPS on this NAS.";
        break;
    case UPS_LINE:
        big = "ON LINE POWER";
        col = SY_TEAL;
        break;
    case UPS_BATTERY:
        big = "ON BATTERY";
        col = SY_AMBER;
        break;
    case UPS_LOW_BATTERY:
        big = "LOW BATTERY";
        col = SY_RED;
        break;
    case UPS_UNSUPPORTED:
        big = "NOT SUPPORTED";
        col = SY_MUTED;
        note = "This DSM does not expose SYNO.Core.ExternalDevice.UPS.";
        break;
    case UPS_NO_PERMISSION:
        big = "NO PERMISSION";
        col = SY_RED;
        note = "The account may read system data but not the UPS API "
               "(error 105). An account with the UPS/system privilege is needed.";
        break;
    case UPS_ERROR:
        big = "READ ERROR";
        col = SY_RED;
        note = "The UPS API failed; the last values are kept below.";
        break;
    case UPS_UNKNOWN:
    default:
        big = "STATUS UNKNOWN";
        col = SY_AMBER;
        note = s.upsRaw[0] ? "Raw status reported by DSM is shown below." : "Waiting for UPS data...";
        break;
    }

    setTextIfChanged(s_upsBig, big);
    lv_obj_set_style_text_color(s_upsBig, col, 0);

    if (s.upsModel[0]) setTextIfChanged(s_upsVal[0], s.upsModel);
    else setTextIfChanged(s_upsVal[0], "n/a");

    if (s.upsChargeValid) {
        char b[12];
        snprintf(b, sizeof(b), "%d%%", (int)s.upsCharge);
        setTextIfChanged(s_upsVal[1], b);
        lv_obj_set_style_text_color(s_upsVal[1], s.upsCharge <= 20 ? SY_RED : SY_TEXT, 0);
    } else {
        setTextIfChanged(s_upsVal[1], "n/a");
        lv_obj_set_style_text_color(s_upsVal[1], SY_MUTED, 0);
    }

    if (s.upsRuntimeValid) {
        char b[24];
        int32_t m = s.upsRuntime / 60;
        if (s.upsRuntime >= 3600)
            snprintf(b, sizeof(b), "%dh %02dm", (int)(s.upsRuntime / 3600), (int)(m % 60));
        else
            snprintf(b, sizeof(b), "%dm", (int)m);
        setTextIfChanged(s_upsVal[2], b);
    } else {
        setTextIfChanged(s_upsVal[2], "n/a");
    }

    setTextIfChanged(s_upsVal[3], s.upsRaw[0] ? s.upsRaw : "n/a");
    setTextIfChanged(s_upsNote, note);
}

/* ========================================================= settings page */
static lv_obj_t *s_setVal[4];      /* host, account, password, proto */
static lv_obj_t *s_setPort;
static lv_obj_t *s_setStatus;
static Config    s_draft;

static lv_obj_t *s_modal = nullptr;
static lv_obj_t *s_editTa = nullptr;
static lv_obj_t *s_editErr = nullptr;
static int       s_editField = -1;
static bool      s_editVisible = false;

static void settingsApplyDraft();

static void editorClose()
{
    if (s_modal) {
        lv_obj_del(s_modal);
        s_modal = nullptr;
    }
    s_editTa = nullptr;
    s_editErr = nullptr;
    s_editField = -1;
    s_editVisible = false;
}

static void editorOk()
{
    if (!s_editTa || s_editField < 0) {
        editorClose();
        return;
    }
    const char *txt = lv_textarea_get_text(s_editTa);

    switch (s_editField) {
    case 0: s_draft.host = txt ? txt : ""; break;
    case 1: s_draft.user = txt ? txt : ""; break;
    case 2: s_draft.pass = txt ? txt : ""; break;
    case 3: {
        long p = String(txt ? txt : "").toInt();
        if (p <= 0 || p > 65535) {
            if (s_editErr) lv_label_set_text(s_editErr, "Port must be 1-65535");
            return;
        }
        s_draft.port = (uint16_t)p;
        break;
    }
    default: break;
    }
    editorClose();
    settingsApplyDraft();
}

static void editorOk_cb(lv_event_t *e) { (void)e; editorOk(); }
static void editorCancel_cb(lv_event_t *e) { (void)e; editorClose(); }

static void editorShow_cb(lv_event_t *e)
{
    (void)e;
    if (!s_editTa) return;
    s_editVisible = !s_editVisible;
    lv_textarea_set_password_mode(s_editTa, !s_editVisible);
}

static void editorKb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) editorOk();
    else if (code == LV_EVENT_CANCEL) editorClose();
}

static void fieldClick_cb(lv_event_t *e)
{
    int field = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_modal) return;

    s_editField = field;
    s_editVisible = false;

    lv_obj_t *panel = mkPanel(s_scr[PAGE_SETTINGS], 0, 0, SY_W, SY_H, SY_DARK, 0);
    s_modal = panel;

    static const char *titles[4] = { "Server address", "Account", "Password", "Port" };
    mkLabel(panel, titles[field], 10, 8, fValue(), SY_TEXT, nullptr);

    s_editTa = lv_textarea_create(panel);
    lv_obj_set_pos(s_editTa, 8, 34);
    lv_obj_set_size(s_editTa, 264, 34);
    lv_textarea_set_one_line(s_editTa, true);
    lv_obj_set_style_bg_color(s_editTa, SY_PANEL, 0);
    lv_obj_set_style_border_color(s_editTa, SY_LINE, 0);
    lv_obj_set_style_border_width(s_editTa, 1, 0);
    lv_obj_set_style_radius(s_editTa, 8, 0);
    lv_obj_set_style_text_color(s_editTa, SY_TEXT, 0);
    lv_obj_set_style_text_font(s_editTa, fValue(), 0);
    lv_obj_set_style_pad_all(s_editTa, 6, 0);

    char buf[40];
    switch (field) {
    case 0: lv_textarea_set_text(s_editTa, s_draft.host.c_str());
            lv_textarea_set_placeholder_text(s_editTa, "192.168.1.10"); break;
    case 1: lv_textarea_set_text(s_editTa, s_draft.user.c_str());
            lv_textarea_set_placeholder_text(s_editTa, "account"); break;
    case 2:
        lv_textarea_set_text(s_editTa, s_draft.pass.c_str());
        lv_textarea_set_password_mode(s_editTa, true);
        lv_textarea_set_placeholder_text(s_editTa, "password");
        break;
    case 3:
        snprintf(buf, sizeof(buf), "%u", (unsigned)s_draft.port);
        lv_textarea_set_text(s_editTa, buf);
        lv_textarea_set_accepted_chars(s_editTa, "0123456789");
        break;
    default: break;
    }

    s_editErr = nullptr;
    mkLabel(panel, "", 10, 70, fSmall(), SY_RED, &s_editErr);

    mkButton(panel, "Cancel", 8, 90, 80, 26, SY_PANEL_HI, SY_TEXT, editorCancel_cb, nullptr);
    if (field == 2) {
        lv_obj_t *show = mkButton(panel, "Show", 94, 90, 60, 26, SY_PANEL_HI, SY_TEXT,
                                  editorShow_cb, nullptr);
        (void)show;
    }
    mkButton(panel, "OK", 160, 90, 112, 26, SY_TEAL, SY_DARK, editorOk_cb, nullptr);

    lv_obj_t *kb = lv_keyboard_create(panel);
    lv_obj_set_size(kb, 264, 116);
    lv_obj_set_pos(kb, 8, 120);
    lv_obj_set_style_bg_color(kb, SY_PANEL, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    if (field == 3) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, s_editTa);
    lv_obj_add_event_cb(kb, editorKb_cb, LV_EVENT_ALL, nullptr);

    lv_obj_add_state(s_editTa, LV_STATE_FOCUSED);
}

static void settingsApplyDraft()
{
    setTextIfChanged(s_setVal[0], s_draft.host.length() ? s_draft.host.c_str() : "not set");
    setTextIfChanged(s_setVal[1], s_draft.user.length() ? s_draft.user.c_str() : "not set");

    /* The password is never echoed back, only its presence. */
    setTextIfChanged(s_setVal[2], s_draft.pass.length() ? "********" : "not set");

    char b[24];
    snprintf(b, sizeof(b), "%s >", s_draft.https ? "HTTPS" : "HTTP");
    setTextIfChanged(s_setVal[3], b);

    snprintf(b, sizeof(b), "%u", (unsigned)s_draft.port);
    setTextIfChanged(s_setPort, b);
}

static void protoToggle_cb(lv_event_t *e)
{
    (void)e;
    s_draft.https = !s_draft.https;
    /* Follow the DSM default port when switching protocol. */
    if (s_draft.https && s_draft.port == 5000) s_draft.port = 5001;
    else if (!s_draft.https && s_draft.port == 5001) s_draft.port = 5000;
    settingsApplyDraft();
}

static void testBtn_cb(lv_event_t *e)
{
    (void)e;
    char err[48];
    if (!CfgValidate(s_draft, err, sizeof(err))) {
        setTextIfChanged(s_setStatus, err);
        lv_obj_set_style_text_color(s_setStatus, SY_RED, 0);
        return;
    }
    NetTest(s_draft);
}

static void saveBtn_cb(lv_event_t *e)
{
    (void)e;
    char err[48];
    if (!CfgValidate(s_draft, err, sizeof(err))) {
        setTextIfChanged(s_setStatus, err);
        lv_obj_set_style_text_color(s_setStatus, SY_RED, 0);
        return;
    }
    if (!CfgSave(s_draft)) {
        setTextIfChanged(s_setStatus, "Could not write the configuration");
        lv_obj_set_style_text_color(s_setStatus, SY_RED, 0);
        return;
    }
    CfgGet(s_draft);        /* pick up the sanitised host/port */
    settingsApplyDraft();
    NetApplyConfig();
    setTextIfChanged(s_setStatus, "Saved. Reconnecting...");
    lv_obj_set_style_text_color(s_setStatus, SY_TEAL, 0);
}

static void clearBtn_cb(lv_event_t *e)
{
    (void)e;
    CfgClear();
    CfgGet(s_draft);
    settingsApplyDraft();
    NetApplyConfig();
    setTextIfChanged(s_setStatus, "Configuration cleared");
    lv_obj_set_style_text_color(s_setStatus, SY_MUTED, 0);
}

static lv_obj_t *settingRow(lv_obj_t *scr, lv_coord_t y, const char *label,
                            lv_event_cb_t cb, int field, lv_obj_t **valueOut)
{
    lv_obj_t *row = mkPanel(scr, 6, y, 268, 30, SY_PANEL, 8);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)field);

    mkLabel(row, label, 8, 8, fSmall(), SY_MUTED, nullptr);

    lv_obj_t *v = nullptr;
    mkLabel(row, "-", 86, 6, fValue(), SY_TEXT, &v);
    lv_obj_set_width(v, 174);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    if (valueOut) *valueOut = v;
    return row;
}

static void buildSettings()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_SETTINGS] = scr;
    pageHeader(scr, "SETTINGS");

    CfgGet(s_draft);

    settingRow(scr, 30,  "ADDRESS",  fieldClick_cb, 0, &s_setVal[0]);
    settingRow(scr, 62,  "ACCOUNT",  fieldClick_cb, 1, &s_setVal[1]);
    settingRow(scr, 94,  "PASSWORD", fieldClick_cb, 2, &s_setVal[2]);
    settingRow(scr, 126, "PROTOCOL", protoToggle_cb, 0, &s_setVal[3]);
    settingRow(scr, 158, "PORT",     fieldClick_cb, 3, &s_setPort);

    mkButton(scr, "TEST",  6,  194, 86, 26, SY_PANEL_HI, SY_TEXT, testBtn_cb, nullptr);
    mkButton(scr, "SAVE",  97, 194, 86, 26, SY_TEAL, SY_DARK, saveBtn_cb, nullptr);
    mkButton(scr, "CLEAR", 188, 194, 86, 26, SY_PANEL_HI, SY_RED, clearBtn_cb, nullptr);

    s_setStatus = nullptr;
    mkLabel(scr, "Not connected", 8, 224, fSmall(), SY_MUTED, &s_setStatus);
    lv_obj_set_width(s_setStatus, 264);
    lv_label_set_long_mode(s_setStatus, LV_LABEL_LONG_DOT);

    settingsApplyDraft();
}

/* ============================================================== refresh */
static void settingsRefresh(const Snapshot &s)
{
    char buf[160];

    if (s.testState == 1) {
        setTextIfChanged(s_setStatus, "Testing connection...");
        lv_obj_set_style_text_color(s_setStatus, SY_AMBER, 0);
        return;
    }
    if (s.testState == 2) {
        setTextIfChanged(s_setStatus, s.testMsg);
        lv_obj_set_style_text_color(s_setStatus, SY_TEAL, 0);
        return;
    }
    if (s.testState == 3) {
        setTextIfChanged(s_setStatus, s.testMsg);
        lv_obj_set_style_text_color(s_setStatus, SY_RED, 0);
        return;
    }

    const char *st = connText(s.conn);
    if (s.conn == CONN_ONLINE) {
        if (!s_draft.https) {
            /* HTTP is not encrypted: say it instead of implying safety. */
            snprintf(buf, sizeof(buf), "ONLINE (HTTP, unencrypted) %s", s.model);
            setTextIfChanged(s_setStatus, buf);
            lv_obj_set_style_text_color(s_setStatus, SY_AMBER, 0);
        } else {
            snprintf(buf, sizeof(buf), "ONLINE (HTTPS) %s", s.model);
            setTextIfChanged(s_setStatus, buf);
            lv_obj_set_style_text_color(s_setStatus, SY_TEAL, 0);
        }
        return;
    }

    if (s.msg[0]) snprintf(buf, sizeof(buf), "%s: %s", st, s.msg);
    else snprintf(buf, sizeof(buf), "%s", st);
    setTextIfChanged(s_setStatus, buf);
    lv_obj_set_style_text_color(s_setStatus, connColor(s.conn), 0);
}

/* ================================================= gesture navigation */
static bool underFingerBlocked()
{
    lv_obj_t *o = lv_indev_get_obj_act();
    int depth = 0;
    while (o && depth++ < 12) {
        if (lv_obj_check_type(o, &lv_textarea_class)) return true;
        if (lv_obj_check_type(o, &lv_keyboard_class)) return true;
        if (lv_obj_check_type(o, &lv_btn_class)) return true;
        o = lv_obj_get_parent(o);
    }
    return false;
}

static lv_scr_load_anim_t animFor(PageId from, PageId to)
{
    switch (to) {
    case PAGE_SETTINGS: return LV_SCR_LOAD_ANIM_MOVE_TOP;
    case PAGE_STORAGE:  return LV_SCR_LOAD_ANIM_MOVE_LEFT;
    case PAGE_SYSTEM:   return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    case PAGE_UPS:      return LV_SCR_LOAD_ANIM_MOVE_LEFT;
    case PAGE_HOME:
    default:
        if (from == PAGE_STORAGE) return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        if (from == PAGE_SYSTEM)  return LV_SCR_LOAD_ANIM_MOVE_LEFT;
        if (from == PAGE_UPS)     return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        return LV_SCR_LOAD_ANIM_MOVE_BOTTOM;
    }
}

void UiNavigate(PageId page, bool animate)
{
    if (page >= PAGE_MAX || !s_scr[page]) return;
    if (page == s_cur) return;

    if (s_modal) editorClose();

    PageId from = s_cur;
    s_cur = page;
    NetSetPage(page);
    s_navBlockUntil = millis() + 320;

    lv_scr_load_anim(s_scr[page], animFor(from, page), animate ? 200 : 0, 0, false);

    if (page == PAGE_SETTINGS) {
        CfgGet(s_draft);
        settingsApplyDraft();
    } else if (page == PAGE_STORAGE || page == PAGE_SYSTEM || page == PAGE_UPS) {
        NetRequestFast();
    }

    /* Fill the new page immediately from the current snapshot. */
    if (DataLock(20)) {
        Snapshot s = Data();
        DataUnlock();
        if (page == PAGE_HOME) updateHome(s, true);
        else if (page == PAGE_STORAGE) storApply(s);
        else if (page == PAGE_SYSTEM) sysApply(s);
        else if (page == PAGE_UPS) upsApply(s);
        else if (page == PAGE_SETTINGS) settingsRefresh(s);
    }
}

PageId UiCurrentPage()
{
    return s_cur;
}

static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (s_modal) return;
    if ((int32_t)(millis() - s_navBlockUntil) < 0) return;
    if (underFingerBlocked()) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_NONE) return;

    if (s_cur == PAGE_HOME) {
        if (dir == LV_DIR_BOTTOM)      UiNavigate(PAGE_SETTINGS, true);
        else if (dir == LV_DIR_LEFT)   UiNavigate(PAGE_STORAGE, true);
        else if (dir == LV_DIR_RIGHT)  UiNavigate(PAGE_SYSTEM, true);
        /* swipe up on home: already home */
    } else if (dir == LV_DIR_TOP) {
        UiNavigate(PAGE_HOME, true);
    }
}

bool UiHandleBack()
{
    if (s_modal) {
        editorClose();
        return true;
    }
    if (s_cur != PAGE_HOME) {
        UiNavigate(PAGE_HOME, true);
        return true;
    }
    return false;
}

/* ================================================================ public */
void UiBuild(void *bsp)
{
    s_dev = (CHAPPIE *)bsp;
    s_cur = PAGE_HOME;
    s_storPage = 0;
    s_chartMB = 0;
    s_chartPeak = 0.0f;
    s_navBlockUntil = 0;
    s_modal = nullptr;

    buildHome();
    buildSettings();
    buildStorage();
    buildSystem();
    buildUps();

    NetSetPage(PAGE_HOME);
    /* The launcher left an empty screen active; replace it. */
    lv_scr_load_anim(s_scr[PAGE_HOME], LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
}

void UiDestroy()
{
    if (s_modal) editorClose();

    /* A running lv_scr_load_anim keeps raw pointers to both screens in
     * lv_disp_t (act/prev/scr_to_load) and animates them. Cancel those
     * animations and clear the pointers before anything is deleted, so an
     * exit during a page transition cannot touch freed memory. */
    lv_disp_t *d = lv_disp_get_default();
    if (d) {
        for (int i = 0; i < PAGE_MAX; i++) {
            if (s_scr[i]) lv_anim_del(s_scr[i], nullptr);
        }
        for (int i = 0; i < PAGE_MAX; i++) {
            if (s_scr[i]) lv_obj_set_pos(s_scr[i], 0, 0);
        }
        d->scr_to_load = nullptr;
        d->prev_scr = nullptr;
        d->del_prev = false;
        d->draw_prev_over_act = false;
    }

    /* Never leave the display pointing at a screen we are about to delete. */
    lv_disp_load_scr(lv_obj_create(NULL));

    for (int i = 0; i < PAGE_MAX; i++) {
        if (s_scr[i]) {
            lv_obj_del(s_scr[i]);
            s_scr[i] = nullptr;
        }
    }

    s_cur = PAGE_HOME;
    s_timeLabel = nullptr;
    s_chart = nullptr;
    s_serDl = s_serUl = nullptr;
    s_dot = s_netState = nullptr;
    s_storMsg = s_storPageLbl = nullptr;
    s_storDisk = s_storPrev = s_storNext = nullptr;
    for (int i = 0; i < STOR_CARDS; i++) {
        s_storCard[i] = s_storName[i] = s_storPct[i] = s_storBar[i] = s_storSize[i] = nullptr;
    }
    for (int i = 0; i < SYS_ROWS; i++) s_sysVal[i] = nullptr;
    s_sysNote = nullptr;
    s_upsBig = nullptr;
    s_upsNote = nullptr;
    for (int i = 0; i < 4; i++) {
        s_upsVal[i] = nullptr;
        s_setVal[i] = nullptr;
    }
    s_setPort = s_setStatus = nullptr;
}

void UiRefresh()
{
    if (!s_scr[PAGE_HOME]) return;

    Snapshot s;
    if (!DataLock(15)) return;
    s = Data();
    DataUnlock();

    static uint32_t seenFast = 0, seenSys = 0, seenStor = 0, seenUps = 0;

    /* Labels are refreshed every tick so an online->stale transition shows up
     * immediately; only a new sample adds a chart point. */
    updateNetLabels(s);
    if (s.fastSeq != seenFast) {
        seenFast = s.fastSeq;
        chartPush(s.downBps, s.upBps);
    }

    if (s_cur == PAGE_HOME) {
        updateHome(s, false);
    } else if (s_cur == PAGE_STORAGE) {
        if (s.storageSeq != seenStor) {
            seenStor = s.storageSeq;
            storApply(s);
        }
    } else if (s_cur == PAGE_SYSTEM) {
        if (s.sysSeq != seenSys) {
            seenSys = s.sysSeq;
            sysApply(s);
        }
    } else if (s_cur == PAGE_UPS) {
        if (s.upsSeq != seenUps) {
            seenUps = s.upsSeq;
            upsApply(s);
        }
    } else if (s_cur == PAGE_SETTINGS) {
        settingsRefresh(s);
    }
}

} // namespace Sy
