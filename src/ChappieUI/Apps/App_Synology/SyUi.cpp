/**
 * @file SyUi.cpp
 * @brief Synology monitor: pages, gestures, session page and on-screen keyboard.
 *
 * Pages are created on first use and destroyed when leaving them (except the
 * home page, which owns the protected CPU/RAM meters). This board runs LVGL
 * with its default 48 KB pool, so keeping six screens alive at once - and
 * building a ~70 object keyboard on every field tap - exhausted lv_mem and
 * aborted in LV_ASSERT_MALLOC. The keyboard is now a single canvas object
 * that draws its own keys and hit-tests touches.
 */
#include "SyCommon.h"

#include "../../../ChappieBsp/Chappie.h"
#include "../../Resource/ResourcePool.h"

#include <string.h>
#include <ctype.h>
#include <time.h>

namespace Sy {

/* ---------------------------------------------------------------- palette */
#define SY_BG        lv_color_hex(0x000000)
#define SY_DARK      lv_color_hex(0x0A0C0F)
#define SY_PANEL     lv_color_hex(0x121519)
#define SY_PANEL_HI  lv_color_hex(0x181D23)
#define SY_LINE      lv_color_hex(0x1E2329)
#define SY_TEXT      lv_color_hex(0xF2F4F7)
#define SY_MUTED     lv_color_hex(0x8A919B)
#define SY_TEAL      lv_color_hex(0x18C7A8)
#define SY_AMBER     lv_color_hex(0xFFB02E)
#define SY_RED       lv_color_hex(0xE5484D)
#define SY_GREEN     lv_color_hex(0x53DE77)   /* project green: healthy / normal */
#define SY_ONLINE    lv_color_hex(0x34C759)   /* ONLINE status dot */
#define SY_WARN      lv_color_hex(0xFF8F5A)   /* soft orange-red: warning */
#define SY_CRIT      lv_color_hex(0xD9534F)   /* soft red: critical */

#define SY_W 280
#define SY_H 240

/* Every page keeps its content 10 px away from the screen edges. */
#define SY_PAD 10
#define SY_CONTENT_W (SY_W - 2 * SY_PAD)     /* 260 */
#define SY_BODY_Y 40                         /* first row below the 34 px bar */
#define SY_BODY_BOTTOM (SY_H - SY_PAD)       /* 230 */

/* ---------------------------------------------------------------- fonts */
static const lv_font_t *fSmall()  { return &lv_font_montserrat_12; }
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
static const lv_font_t *fHuge()
{
    lv_font_t *f = ResourcePool::GetFont("MontserratBold48");
    return f ? f : &lv_font_montserrat_14;
}
/* LV_SYMBOL_* only exists in the built-in font; the project fonts are ASCII. */
static const lv_font_t *fSymbol() { return &lv_font_montserrat_14; }

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

/* =============================================================== styles */
static lv_style_t st_card;      /* rounded panel */
static lv_style_t st_card_flat; /* square panel */
static lv_style_t st_btn;
static lv_style_t st_btn_press;
static bool st_ready = false;

static void stylesInit()
{
    if (st_ready) return;

    lv_style_init(&st_card);
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card, SY_PANEL);
    lv_style_set_border_width(&st_card, 0);
    lv_style_set_radius(&st_card, 8);
    lv_style_set_pad_all(&st_card, 0);

    lv_style_init(&st_card_flat);
    lv_style_set_bg_opa(&st_card_flat, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card_flat, SY_DARK);
    lv_style_set_border_width(&st_card_flat, 0);
    lv_style_set_radius(&st_card_flat, 0);
    lv_style_set_pad_all(&st_card_flat, 0);

    lv_style_init(&st_btn);
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&st_btn, SY_PANEL_HI);
    lv_style_set_border_width(&st_btn, 0);
    lv_style_set_shadow_width(&st_btn, 0);
    lv_style_set_radius(&st_btn, 6);
    lv_style_set_pad_all(&st_btn, 0);

    lv_style_init(&st_btn_press);
    lv_style_set_bg_color(&st_btn_press, SY_PANEL);

    st_ready = true;
}

/* =============================================================== helpers */
static lv_obj_t *s_scr[PAGE_MAX];
static PageId    s_cur = PAGE_HOME;
static uint32_t  s_navBlockUntil = 0;
static CHAPPIE  *s_dev = nullptr;

static double s_peakDown = 0.0;
static double s_peakUp = 0.0;

static void gesture_cb(lv_event_t *e);
static void volCard_cb(lv_event_t *e);
static void updateHome(const Snapshot &s, bool force);
static void buildPage(PageId page);
static bool ensurePage(PageId page);

/* LVGL keeps its own pool (LV_MEM_SIZE, 48 KB by default on this build).
 * Logging the watermark makes a future exhaustion visible before it aborts. */
static void logLvMem(const char *tag)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("[Syno] lv_mem %s: %u/%u used, max %u, frag %u%%\n", tag,
           (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.total_size,
           (unsigned)mon.max_used, (unsigned)mon.frag_pct);
}

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
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_press, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, bg, 0);
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
    lv_obj_add_style(p, radius > 0 ? &st_card : &st_card_flat, 0);
    lv_obj_set_style_bg_color(p, bg, 0);
    if (radius > 0) lv_obj_set_style_radius(p, radius, 0);
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

/* Title bar: title and the right-hand button 10 px from the screen edges. */
static void pageHeader(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = mkPanel(scr, 0, 0, SY_W, 34, SY_DARK, 0);
    mkLabel(bar, title, SY_PAD, 10, fValue(), SY_TEXT, nullptr);
    mkButton(bar, "HOME", 220, 10, 50, 20, SY_PANEL, SY_MUTED, homeBtn_cb, nullptr);
}

static void backToStorage_cb(lv_event_t *e)
{
    (void)e;
    UiNavigate(PAGE_STORAGE, true);
}

static void pageHeaderBack(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = mkPanel(scr, 0, 0, SY_W, 34, SY_DARK, 0);
    mkLabel(bar, title, SY_PAD, 10, fValue(), SY_TEXT, nullptr);
    mkButton(bar, "BACK", 214, 10, 56, 20, SY_PANEL, SY_MUTED, backToStorage_cb, nullptr);
}

/* --------------------------------------------------------- formatting */
static void fmtBytes(uint64_t bytes, char *out, size_t n)
{
    const double kb = 1024.0, mb = kb * 1024.0, gb = mb * 1024.0, tb = gb * 1024.0;
    if (bytes >= (uint64_t)tb)      snprintf(out, n, "%.2f TB", bytes / tb);
    else if (bytes >= (uint64_t)gb) snprintf(out, n, "%.1f GB", bytes / gb);
    else if (bytes >= (uint64_t)mb) snprintf(out, n, "%.0f MB", bytes / mb);
    else                            snprintf(out, n, "%.0f KB", bytes / kb);
}

static void fmtSpeed(double bps, char *out, size_t n)
{
    if (!(bps >= 0.0)) bps = 0.0;
    if (bps < 1024.0)                        snprintf(out, n, "%.0f B/s", bps);
    else if (bps < 1024.0 * 1024.0)          snprintf(out, n, "%.1f KB/s", bps / 1024.0);
    else if (bps < 1024.0 * 1024.0 * 1024.0) snprintf(out, n, "%.1f MB/s", bps / (1024.0 * 1024.0));
    else                                     snprintf(out, n, "%.2f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
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

/* Runtime in seconds -> "MM:SS" or "H:MM:SS" (never "hours" shown as minutes). */
static void fmtRuntime(int32_t sec, char *out, size_t n)
{
    if (sec < 0) {
        snprintf(out, n, "--");
        return;
    }
    if (sec >= 3600)
        snprintf(out, n, "%d:%02d:%02d", (int)(sec / 3600), (int)((sec / 60) % 60), (int)(sec % 60));
    else
        snprintf(out, n, "%02d:%02d", (int)(sec / 60), (int)(sec % 60));
}

/* One capacity colour rule for the storage bars AND the detail ring:
 * <=75 % green, >75..90 % soft orange-red, >90 % soft red. */
static lv_color_t capColor(int pct)
{
    if (pct > 90) return SY_CRIT;
    if (pct > 75) return SY_WARN;
    return SY_GREEN;
}

/* ============================================================ home page */
static lv_obj_t *s_timeLabel;
static lv_obj_t *s_dlVal, *s_ulVal, *s_dlPeak, *s_ulPeak, *s_netState, *s_dot;
static lv_obj_t *s_dlBar, *s_ulBar;
static int16_t s_lastCpu = -1, s_lastRam = -1;

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

    /* network block: 10 px from the left, right and bottom edges.
     *   status row: dot + ONLINE + interface name
     *   two columns: UPLOAD (amber) and DOWNLOAD (teal), current rate large,
     *   session peak below, and a thin activity bar under each column. */
    lv_obj_t *panel = mkPanel(scr, SY_PAD, 148, SY_CONTENT_W, 82, SY_PANEL, 12);

    s_dot = lv_obj_create(panel);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_pos(s_dot, 10, 8);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, SY_MUTED, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    mkLabel(panel, "INIT", 22, 5, fSmall(), SY_MUTED, &s_netState);
    lv_obj_set_width(s_netState, 228);

    /* upload column */
    mkLabel(panel, LV_SYMBOL_UP, 10, 25, fSymbol(), SY_AMBER, nullptr);
    mkLabel(panel, "UPLOAD", 26, 24, fSmall(), SY_MUTED, nullptr);
    mkLabel(panel, "--", 10, 36, fBold18(), SY_AMBER, &s_ulVal);
    lv_obj_set_width(s_ulVal, 112);
    mkLabel(panel, "MAX --", 10, 58, fSmall(), SY_MUTED, &s_ulPeak);
    lv_obj_set_width(s_ulPeak, 112);

    /* download column */
    mkLabel(panel, LV_SYMBOL_DOWN, 134, 25, fSymbol(), SY_TEAL, nullptr);
    mkLabel(panel, "DOWNLOAD", 150, 24, fSmall(), SY_MUTED, nullptr);
    mkLabel(panel, "--", 134, 36, fBold18(), SY_TEAL, &s_dlVal);
    lv_obj_set_width(s_dlVal, 112);
    mkLabel(panel, "MAX --", 134, 58, fSmall(), SY_MUTED, &s_dlPeak);
    lv_obj_set_width(s_dlPeak, 112);

    /* activity bars (same scale for both columns) */
    s_ulBar = lv_bar_create(panel);
    lv_obj_set_pos(s_ulBar, 10, 74);
    lv_obj_set_size(s_ulBar, 112, 4);
    lv_obj_set_style_bg_color(s_ulBar, SY_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ulBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ulBar, SY_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ulBar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ulBar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_ulBar, 0, 1000);
    lv_bar_set_value(s_ulBar, 0, LV_ANIM_OFF);

    s_dlBar = lv_bar_create(panel);
    lv_obj_set_pos(s_dlBar, 134, 74);
    lv_obj_set_size(s_dlBar, 112, 4);
    lv_obj_set_style_bg_color(s_dlBar, SY_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dlBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dlBar, SY_TEAL, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dlBar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dlBar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_dlBar, 0, 1000);
    lv_bar_set_value(s_dlBar, 0, LV_ANIM_OFF);
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

static void updateNetLabels(const Snapshot &s)
{
    char b[32];
    bool live = s.netValid && !s.netStale;

    if (!live) {
        setTextIfChanged(s_dlVal, "--");
        setTextIfChanged(s_ulVal, "--");
    } else {
        fmtSpeed(s.downBps, b, sizeof(b));
        setTextIfChanged(s_dlVal, b);
        fmtSpeed(s.upBps, b, sizeof(b));
        setTextIfChanged(s_ulVal, b);
    }

    char d[32], u[32];
    fmtSpeed(s_peakDown, d, sizeof(d));
    fmtSpeed(s_peakUp, u, sizeof(u));
    snprintf(b, sizeof(b), "MAX %s", s_peakDown > 0.5 ? d : "--");
    setTextIfChanged(s_dlPeak, b);
    snprintf(b, sizeof(b), "MAX %s", s_peakUp > 0.5 ? u : "--");
    setTextIfChanged(s_ulPeak, b);

    lv_obj_set_style_text_color(s_dlVal, live ? SY_TEAL : SY_MUTED, 0);
    lv_obj_set_style_text_color(s_ulVal, live ? SY_AMBER : SY_MUTED, 0);

    /* Both bars share one scale (the larger peak) so the two columns can be
     * compared; the bar is an activity indicator, the numbers are the truth. */
    double top = s_peakDown > s_peakUp ? s_peakDown : s_peakUp;
    if (top < 1024.0) top = 1024.0;
    int dv = live ? (int)((double)s.downBps * 1000.0 / top) : 0;
    int uv = live ? (int)((double)s.upBps * 1000.0 / top) : 0;
    if (dv < 0) dv = 0;
    if (dv > 1000) dv = 1000;
    if (uv < 0) uv = 0;
    if (uv > 1000) uv = 1000;
    lv_bar_set_value(s_dlBar, dv, LV_ANIM_OFF);
    lv_bar_set_value(s_ulBar, uv, LV_ANIM_OFF);
}

/* Status row inside the network panel: green #34C759 only when the NAS is
 * reachable AND the interface reports a link; every other state says why. */
static void updateStatusLine(const Snapshot &s)
{
    bool online = (s.conn == CONN_ONLINE);
    bool linkOk = (!s.linkValid) || s.linkUp;
    bool live = online && s.netValid && !s.netStale && linkOk;

    lv_color_t col;
    if (live)                                       col = SY_ONLINE;
    else if (s.conn == CONN_CONNECTING)             col = SY_AMBER;
    else if (s.conn == CONN_AUTH_FAIL ||
             s.conn == CONN_ERROR)                  col = SY_RED;
    else                                            col = SY_MUTED;
    lv_obj_set_style_bg_color(s_dot, col, 0);

    char buf[72];
    if (online) {
        const char *ifname = s.nic[0] ? s.nic : (s.linkName[0] ? s.linkName : "?");
        if (s.netStale)                          snprintf(buf, sizeof(buf), "STALE  %s", ifname);
        else if (s.linkValid && !s.linkUp)       snprintf(buf, sizeof(buf), "LINK DOWN  %s", s.linkName);
        else                                     snprintf(buf, sizeof(buf), "ONLINE  %s", ifname);
    } else if (s.msg[0]) {
        snprintf(buf, sizeof(buf), "%s  %s", connText(s.conn), s.msg);
    } else {
        snprintf(buf, sizeof(buf), "%s", connText(s.conn));
    }
    setTextIfChanged(s_netState, buf);
    lv_obj_set_style_text_color(s_netState, live ? SY_TEXT : col, 0);
}

static void updateHome(const Snapshot &s, bool force)
{
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

    if (s.cpuValid && s.cpuUse != s_lastCpu) {
        s_lastCpu = s.cpuUse;
        Content_Update(&Meter1, s.cpuUse, true);
    }
    if (s.ramValid && s.ramUse != s_lastRam) {
        s_lastRam = s.ramUse;
        Content_Update(&Meter2, s.ramUse, false);
    }

    if (force) updateNetLabels(s);
    updateStatusLine(s);
}

/* =========================================== overview: connected users */
static lv_obj_t *s_usrList;
static lv_obj_t *s_usrMsg;
static lv_obj_t *s_usrNote;
static lv_obj_t *s_usrDot[SY_MAX_SESSIONS];
static lv_obj_t *s_usrName[SY_MAX_SESSIONS];
static lv_obj_t *s_usrProto[SY_MAX_SESSIONS];
static lv_obj_t *s_usrTime[SY_MAX_SESSIONS];

#define USR_ROW_H 30

static void buildOverview()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_OVERVIEW] = scr;
    pageHeader(scr, "CONNECTED USERS");

    mkLabel(scr, "USER", 34, 40, fSmall(), SY_MUTED, nullptr);
    mkLabel(scr, "PROTOCOL", 120, 40, fSmall(), SY_MUTED, nullptr);
    lv_obj_t *cap = nullptr;
    mkLabel(scr, "ONLINE", 182, 40, fSmall(), SY_MUTED, &cap);
    lv_obj_set_width(cap, 72);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_RIGHT, 0);

    /* Scrollable list. Gestures that start inside it scroll instead of
     * navigating; HOME button and the B key still leave the page. */
    s_usrList = lv_obj_create(scr);
    lv_obj_set_pos(s_usrList, SY_PAD, 54);
    lv_obj_set_size(s_usrList, SY_CONTENT_W, 148);
    lv_obj_set_style_bg_opa(s_usrList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_usrList, 0, 0);
    lv_obj_set_style_radius(s_usrList, 0, 0);
    lv_obj_set_style_pad_all(s_usrList, 0, 0);
    lv_obj_set_scrollbar_mode(s_usrList, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < SY_MAX_SESSIONS; i++) {
        lv_coord_t y = i * USR_ROW_H;

        lv_obj_t *dot = lv_obj_create(s_usrList);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_pos(dot, 4, y + 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, SY_TEAL, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s_usrDot[i] = dot;

        s_usrName[i] = nullptr;
        mkLabel(s_usrList, "", 24, y + 5, fValue(), SY_TEXT, &s_usrName[i]);
        lv_obj_set_width(s_usrName[i], 84);
        lv_label_set_long_mode(s_usrName[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(s_usrName[i], LV_OBJ_FLAG_HIDDEN);

        s_usrProto[i] = nullptr;
        mkLabel(s_usrList, "", 112, y + 6, fSmall(), SY_MUTED, &s_usrProto[i]);
        lv_obj_set_width(s_usrProto[i], 66);
        lv_obj_add_flag(s_usrProto[i], LV_OBJ_FLAG_HIDDEN);

        s_usrTime[i] = nullptr;
        mkLabel(s_usrList, "", 182, y + 6, fSmall(), SY_TEXT, &s_usrTime[i]);
        lv_obj_set_width(s_usrTime[i], 72);
        lv_obj_set_style_text_align(s_usrTime[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_add_flag(s_usrTime[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_usrMsg = nullptr;
    mkLabel(scr, "Reading sessions...", SY_PAD, 116, fText(), SY_MUTED, &s_usrMsg);
    lv_obj_set_width(s_usrMsg, SY_CONTENT_W);
    lv_obj_set_style_text_align(s_usrMsg, LV_TEXT_ALIGN_CENTER, 0);

    s_usrNote = nullptr;
    mkLabel(scr, "", SY_PAD, 210, fSmall(), SY_MUTED, &s_usrNote);
    lv_obj_set_width(s_usrNote, SY_CONTENT_W);
    lv_label_set_long_mode(s_usrNote, LV_LABEL_LONG_DOT);
}

/* H:MM:SS with unbounded hours (e.g. 120:05:00 stays 120:05:00). */
static void fmtDuration(uint32_t sec, char *out, size_t n)
{
    if (sec == 0xFFFFFFFFu) {
        snprintf(out, n, "--:--:--");
        return;
    }
    uint32_t h = sec / 3600;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    snprintf(out, n, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
}

static void setRowVisible(int i, bool on)
{
    lv_obj_t *objs[4] = { s_usrDot[i], s_usrName[i], s_usrProto[i], s_usrTime[i] };
    for (int k = 0; k < 4; k++) {
        if (!objs[k]) continue;
        if (on) lv_obj_clear_flag(objs[k], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(objs[k], LV_OBJ_FLAG_HIDDEN);
    }
}

static void overviewApply(const Snapshot &s)
{
    if (!s.usersValid) {
        for (int i = 0; i < SY_MAX_SESSIONS; i++) setRowVisible(i, false);
        lv_obj_clear_flag(s_usrMsg, LV_OBJ_FLAG_HIDDEN);

        char m[48];
        if (s.usersErr) snprintf(m, sizeof(m), "Sessions unavailable (%d)", (int)s.usersErr);
        else if (s.conn != CONN_ONLINE) snprintf(m, sizeof(m), "%s", connText(s.conn));
        else snprintf(m, sizeof(m), "Reading sessions...");
        setTextIfChanged(s_usrMsg, m);
        lv_obj_set_style_text_color(s_usrMsg, s.usersErr ? SY_RED : SY_MUTED, 0);
        setTextIfChanged(s_usrNote, "");
        return;
    }

    if (s.userCount == 0) {
        for (int i = 0; i < SY_MAX_SESSIONS; i++) setRowVisible(i, false);
        lv_obj_clear_flag(s_usrMsg, LV_OBJ_FLAG_HIDDEN);
        setTextIfChanged(s_usrMsg, "No users connected");
        setTextIfChanged(s_usrNote, "");
        return;
    }

    lv_obj_add_flag(s_usrMsg, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < SY_MAX_SESSIONS; i++) {
        if (i >= (int)s.userCount) {
            setRowVisible(i, false);
            continue;
        }
        setRowVisible(i, true);

        const SessionInfo &u = s.users[i];
        setTextIfChanged(s_usrName[i], u.who[0] ? u.who : "?");
        setTextIfChanged(s_usrProto[i], u.proto[0] ? u.proto : "n/a");

        char d[16];
        fmtDuration(u.seconds, d, sizeof(d));
        setTextIfChanged(s_usrTime[i], d);
        lv_obj_set_style_text_color(s_usrTime[i], u.seconds == 0xFFFFFFFFu ? SY_MUTED : SY_TEXT, 0);
    }

    /* The NAS exposes no session-termination method (see report), so this page
     * is read-only and says so instead of offering a button that cannot work. */
    char note[48];
    snprintf(note, sizeof(note), "%u sessions - view only", (unsigned)s.userCount);
    setTextIfChanged(s_usrNote, note);
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
        lv_obj_t *card = mkPanel(scr, SY_PAD, 40 + i * 50, SY_CONTENT_W, 46, SY_PANEL, 8);

        s_storName[i] = nullptr;
        mkLabel(card, "-", 10, 2, fValue(), SY_TEXT, &s_storName[i]);
        lv_obj_set_width(s_storName[i], 150);
        lv_label_set_long_mode(s_storName[i], LV_LABEL_LONG_DOT);

        s_storPct[i] = nullptr;
        mkLabel(card, "-", 170, 2, fValue(), SY_TEAL, &s_storPct[i]);
        lv_obj_set_width(s_storPct[i], 62);
        lv_obj_set_style_text_align(s_storPct[i], LV_TEXT_ALIGN_RIGHT, 0);

        /* chevron: this card opens the disk detail page */
        mkLabel(card, ">", 240, 3, fSmall(), SY_MUTED, nullptr);

        lv_obj_add_event_cb(card, volCard_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_pos(bar, 10, 22);
        lv_obj_set_size(bar, 240, 6);
        lv_obj_set_style_bg_color(bar, SY_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, SY_TEAL, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        s_storBar[i] = bar;

        s_storSize[i] = nullptr;
        mkLabel(card, "-", 10, 30, fSmall(), SY_MUTED, &s_storSize[i]);
        lv_obj_set_width(s_storSize[i], 240);
        lv_label_set_long_mode(s_storSize[i], LV_LABEL_LONG_DOT);

        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        s_storCard[i] = card;
    }

    s_storMsg = nullptr;
    mkLabel(scr, "Waiting for data...", SY_PAD, 100, fText(), SY_MUTED, &s_storMsg);
    lv_obj_set_width(s_storMsg, SY_CONTENT_W);
    lv_obj_set_style_text_align(s_storMsg, LV_TEXT_ALIGN_CENTER, 0);

    s_storDisk = nullptr;
    mkLabel(scr, "", SY_PAD, 192, fSmall(), SY_MUTED, &s_storDisk);
    lv_obj_set_width(s_storDisk, 176);
    lv_label_set_long_mode(s_storDisk, LV_LABEL_LONG_DOT);

    s_storPageLbl = nullptr;
    mkLabel(scr, "1/1", 190, 192, fSmall(), SY_MUTED, &s_storPageLbl);
    lv_obj_set_width(s_storPageLbl, 40);
    lv_obj_set_style_text_align(s_storPageLbl, LV_TEXT_ALIGN_RIGHT, 0);

    s_storPrev = mkButton(scr, "<", 234, 190, 16, 16, SY_PANEL_HI, SY_MUTED,
                          storArrow_cb, (void *)(intptr_t)-1);
    s_storNext = mkButton(scr, ">", 254, 190, 16, 16, SY_PANEL_HI, SY_MUTED,
                          storArrow_cb, (void *)(intptr_t)1);

    mkButton(scr, "UPS STATUS   >", SY_PAD, 208, SY_CONTENT_W, 22, SY_PANEL, SY_TEXT,
             upsEntry_cb, nullptr);
}

static void storApply(const Snapshot &s)
{
    int pages = (s.volCount + STOR_CARDS - 1) / STOR_CARDS;
    if (pages < 1) pages = 1;
    if (s_storPage >= pages) s_storPage = (uint8_t)(pages - 1);

    if (!s.storageValid || s.volCount == 0) {
        for (int i = 0; i < STOR_CARDS; i++) lv_obj_add_flag(s_storCard[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_storMsg, LV_OBJ_FLAG_HIDDEN);
        setTextIfChanged(s_storMsg, !s.storageValid
                         ? (s.conn == CONN_ONLINE ? "Reading volumes..." : "No data")
                         : "No volume reported by the storage API");
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
        int pct = 0;
        if (v.total > 0) {
            uint64_t used = v.used > v.total ? v.total : v.used;
            pct = (int)((used * 100ULL) / v.total);
            if (pct > 100) pct = 100;
        }

        setTextIfChanged(s_storName[i], v.name[0] ? v.name : "volume");

        char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        setTextIfChanged(s_storPct[i], pb);
        lv_obj_set_style_text_color(s_storPct[i], capColor(pct), 0);
        lv_obj_set_style_bg_color(s_storBar[i], capColor(pct), LV_PART_INDICATOR);
        lv_bar_set_value(s_storBar[i], pct, LV_ANIM_OFF);

        char used[16], total[16], freeb[16];
        fmtBytes(v.used, used, sizeof(used));
        fmtBytes(v.total, total, sizeof(total));
        fmtBytes(v.total > v.used ? v.total - v.used : 0, freeb, sizeof(freeb));

        char line[72];
        bool warn = v.status[0] && strcmp(v.status, "normal") != 0;
        if (warn) snprintf(line, sizeof(line), "%s / %s  |  %s", used, total, v.status);
        else      snprintf(line, sizeof(line), "%s / %s  |  %s free", used, total, freeb);
        setTextIfChanged(s_storSize[i], line);
        lv_obj_set_style_text_color(s_storSize[i], warn ? SY_AMBER : SY_MUTED, 0);
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

    char dl[48];
    if (!s.diskApiValid)            snprintf(dl, sizeof(dl), "Disk detail: not in API");
    else if (s.diskWarn > 0)        snprintf(dl, sizeof(dl), "%u disks, %u warning",
                                             (unsigned)s.diskCount, (unsigned)s.diskWarn);
    else if (s.diskTempMax >= 0)    snprintf(dl, sizeof(dl), "%u disks OK | %d C",
                                             (unsigned)s.diskCount, (int)s.diskTempMax);
    else                            snprintf(dl, sizeof(dl), "%u disks OK", (unsigned)s.diskCount);
    setTextIfChanged(s_storDisk, dl);
    lv_obj_set_style_text_color(s_storDisk, s.diskWarn > 0 ? SY_AMBER : SY_MUTED, 0);
}

/* ==================================================== volume detail page
 *
 * The storage list shows VOLUMES (vol_info), so the detail page is keyed by a
 * volume index. The previous version reused that index against hdd_info[],
 * which put volume_2 on the Drive 1 page: the two arrays have unrelated order
 * and the pool ids ("reuse_1") are what link them.
 */
static uint8_t   s_volIndex = 0;
static uint32_t  s_gestureAt = 0;
static lv_obj_t *s_volDiskLine;      /* "DISK 1" (or the list of mapped disks) */
static lv_obj_t *s_volArc, *s_volArcPct;
static lv_obj_t *s_volName;
static lv_obj_t *s_volVal[4];        /* USED, TOTAL, TEMP, HEALTH */
static lv_obj_t *s_volModel;

static void volCard_cb(lv_event_t *e)
{
    /* A swipe also fires CLICKED on a non-scrollable card; ignore taps that
     * follow a page gesture so swiping never opens a detail page. */
    if ((int32_t)(millis() - s_gestureAt) < 400) return;

    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = (int)s_storPage * STOR_CARDS + slot;

    int have = 0;
    if (DataLock(20)) {
        have = Data().volCount;
        DataUnlock();
    }
    if (idx < 0 || idx >= have) return;

    s_volIndex = (uint8_t)idx;
    UiNavigate(PAGE_VOLUME, true);
}

/* "Drive 1" -> 1. The device's own number wins over the array position. */
static int diskNumber(const DiskInfo &d, int fallbackIdx)
{
    int n = 0;
    bool any = false;
    for (const char *p = d.name; *p; p++) {
        if (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); any = true; }
        else if (any) break;
    }
    return any ? n : fallbackIdx + 1;
}

/* DSM health wording -> {text, colour}. Unknown is never shown as healthy. */
static const char *healthText(const char *raw, lv_color_t *col)
{
    if (!raw || !raw[0]) { *col = SY_MUTED; return "Unknown"; }
    if (strcmp(raw, "normal") == 0) { *col = SY_GREEN; return "OK"; }
    if (strstr(raw, "crit") || strstr(raw, "fail") || strstr(raw, "crash")) { *col = SY_CRIT; return raw; }
    if (strstr(raw, "warn") || strstr(raw, "degrad")) { *col = SY_WARN; return raw; }
    *col = SY_WARN;
    return raw;
}

/* 0 unknown, 1 normal, 2 warning, 3 critical - used to pick the worst disk */
static int healthRankOf(const char *raw)
{
    if (!raw || !raw[0]) return 0;
    if (strcmp(raw, "normal") == 0) return 1;
    if (strstr(raw, "crit") || strstr(raw, "fail") || strstr(raw, "crash")) return 3;
    if (strstr(raw, "warn") || strstr(raw, "degrad")) return 2;
    return 2;
}

/* One vertical stack, centred: disk id, usage ring, volume name, four info
 * rows, then the disk model on a single ellipsised line. */
static void buildVolume()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_VOLUME] = scr;
    pageHeaderBack(scr, "VOLUME");

    s_volDiskLine = nullptr;
    mkLabel(scr, "--", SY_PAD, 36, fValue(), SY_TEXT, &s_volDiskLine);
    lv_obj_set_width(s_volDiskLine, SY_CONTENT_W);
    lv_obj_set_style_text_align(s_volDiskLine, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_volDiskLine, LV_LABEL_LONG_DOT);

    s_volArc = lv_arc_create(scr);
    lv_obj_set_pos(s_volArc, 105, 56);          /* 70 px ring, centred on x=140 */
    lv_obj_set_size(s_volArc, 70, 70);
    lv_arc_set_rotation(s_volArc, 270);
    lv_arc_set_bg_angles(s_volArc, 0, 360);
    lv_arc_set_range(s_volArc, 0, 100);
    lv_arc_set_value(s_volArc, 0);
    lv_obj_remove_style(s_volArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_volArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_volArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_volArc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_volArc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_volArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_volArc, SY_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_volArc, SY_GREEN, LV_PART_INDICATOR);

    s_volArcPct = nullptr;
    mkLabel(s_volArc, "--", 0, 0, fBold18(), SY_TEXT, &s_volArcPct);
    lv_obj_set_width(s_volArcPct, 60);
    lv_obj_set_style_text_align(s_volArcPct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_volArcPct, LV_ALIGN_CENTER, 0, 0);

    s_volName = nullptr;
    mkLabel(scr, "--", SY_PAD, 129, fValue(), SY_TEXT, &s_volName);
    lv_obj_set_width(s_volName, SY_CONTENT_W);
    lv_obj_set_style_text_align(s_volName, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_volName, LV_LABEL_LONG_DOT);

    /* USED / TOTAL describe the volume, TEMP / HEALTH the disk listed above. */
    static const char *cap[4] = { "USED", "TOTAL", "TEMP", "HEALTH" };
    for (int i = 0; i < 4; i++) {
        lv_coord_t y = 150 + i * 16;
        mkLabel(scr, cap[i], SY_PAD, y, fSmall(), SY_MUTED, nullptr);

        s_volVal[i] = nullptr;
        mkLabel(scr, "--", 140, y - 1, fText(), SY_TEXT, &s_volVal[i]);
        lv_obj_set_width(s_volVal[i], 130);
        lv_obj_set_style_text_align(s_volVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_volVal[i], LV_LABEL_LONG_DOT);
    }

    mkLabel(scr, "MODEL", SY_PAD, 216, fSmall(), SY_MUTED, nullptr);
    s_volModel = nullptr;
    mkLabel(scr, "--", 62, 215, fText(), SY_TEXT, &s_volModel);
    lv_obj_set_width(s_volModel, 208);
    lv_label_set_long_mode(s_volModel, LV_LABEL_LONG_DOT);
}

/* A pool id such as "reuse_1" maps to the volume whose name ends with "_1".
 * Only used when exactly one volume matches; otherwise the relation is
 * ambiguous and nothing is claimed. */
static int volForPool(const Snapshot &s, const char *pool)
{
    if (!pool || !pool[0]) return -1;
    const char *suffix = strrchr(pool, '_');
    if (!suffix) return -1;

    int found = -1;
    for (int i = 0; i < s.volCount && i < SY_MAX_VOLUMES; i++) {
        size_t len = strlen(s.vols[i].name);
        size_t sl = strlen(suffix);
        if (len <= sl) continue;
        if (strcmp(s.vols[i].name + len - sl, suffix) != 0) continue;
        if (found >= 0) return -1;      /* more than one candidate */
        found = i;
    }
    return found;
}

/* Which disks sit behind a volume: disk.pool -> volume, using the same
 * pool-id -> volume-name rule as the ring. */
static bool diskInVolume(const Snapshot &s, const DiskInfo &d, int volIdx)
{
    return volForPool(s, d.pool) == volIdx;
}

static void volApply(const Snapshot &s)
{
    if (!s.storageValid || s_volIndex >= s.volCount) {
        setTextIfChanged(s_volName, "No volume data");
        setTextIfChanged(s_volDiskLine, "");
        for (int i = 0; i < 4; i++) setTextIfChanged(s_volVal[i], "--");
        setTextIfChanged(s_volModel, "--");
        return;
    }

    const VolumeInfo &v = s.vols[s_volIndex];
    char b[80];

    /* volume identity */
    setTextIfChanged(s_volName, v.name[0] ? v.name : "volume");

    uint64_t used = v.used > v.total ? v.total : v.used;
    int pct = (v.total > 0) ? (int)((used * 100ULL) / v.total) : -1;
    if (pct > 100) pct = 100;

    if (pct >= 0) {
        lv_arc_set_value(s_volArc, pct);
        lv_obj_set_style_arc_color(s_volArc, capColor(pct), LV_PART_INDICATOR);
        char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        setTextIfChanged(s_volArcPct, pb);
        lv_obj_set_style_text_color(s_volArcPct, capColor(pct), 0);
    } else {
        /* No usable size from the API: no proportion is drawn. */
        lv_arc_set_value(s_volArc, 0);
        lv_obj_set_style_arc_color(s_volArc, SY_LINE, LV_PART_INDICATOR);
        setTextIfChanged(s_volArcPct, "--");
        lv_obj_set_style_text_color(s_volArcPct, SY_MUTED, 0);
    }

    if (v.total > 0) {
        fmtBytes(used, b, sizeof(b));
        setTextIfChanged(s_volVal[0], b);
        fmtBytes(v.total, b, sizeof(b));
        setTextIfChanged(s_volVal[1], b);
    } else {
        setTextIfChanged(s_volVal[0], "--");
        setTextIfChanged(s_volVal[1], "--");
    }

    /* disks behind this volume (pool -> volume); list them all, never assume
     * the volume is one physical drive. */
    const DiskInfo *disks[3] = { nullptr, nullptr, nullptr };
    int count = 0;
    for (int i = 0; i < s.diskCount && i < SY_MAX_DISKS && count < 3; i++) {
        if (diskInVolume(s, s.disks[i], s_volIndex)) disks[count++] = &s.disks[i];
    }

    if (count == 0) {
        setTextIfChanged(s_volDiskLine, "NO DISK MAPPED");
        lv_obj_set_style_text_color(s_volDiskLine, SY_MUTED, 0);

        setTextIfChanged(s_volVal[2], "--");
        lv_obj_set_style_text_color(s_volVal[2], SY_MUTED, 0);
        setTextIfChanged(s_volVal[3], "Unknown");
        lv_obj_set_style_text_color(s_volVal[3], SY_MUTED, 0);
        setTextIfChanged(s_volModel, "--");
        return;
    }

    /* disk id line: "DISK 1" or "DISK 1, DISK 2" */
    b[0] = 0;
    for (int i = 0; i < count; i++) {
        char one[16];
        snprintf(one, sizeof(one), "%sDISK %d", i ? ", " : "",
                 diskNumber(*disks[i], (int)(disks[i] - s.disks)));
        strncat(b, one, sizeof(b) - strlen(b) - 1);
    }
    setTextIfChanged(s_volDiskLine, b);
    lv_obj_set_style_text_color(s_volDiskLine, SY_TEXT, 0);

    /* temperature: one disk -> "38 C"; several -> "38/37 C" */
    int temps[3];
    int tCount = 0;
    for (int i = 0; i < count; i++) {
        if (disks[i]->temp >= 0) temps[tCount++] = disks[i]->temp;
    }
    if (tCount == 0) {
        setTextIfChanged(s_volVal[2], "--");
        lv_obj_set_style_text_color(s_volVal[2], SY_MUTED, 0);
    } else {
        b[0] = 0;
        for (int i = 0; i < tCount; i++) {
            char one[8];
            snprintf(one, sizeof(one), "%s%d", i ? "/" : "", temps[i]);
            strncat(b, one, sizeof(b) - strlen(b) - 1);
        }
        strncat(b, " C", sizeof(b) - strlen(b) - 1);
        setTextIfChanged(s_volVal[2], b);
        lv_obj_set_style_text_color(s_volVal[2],
                                    temps[0] >= 55 ? SY_WARN : SY_TEXT, 0);
    }

    /* health: the worst state of the mapped disks, never "OK" by default */
    int worstIdx = 0;
    int worstRank = -1;
    for (int i = 0; i < count; i++) {
        int r = healthRankOf(disks[i]->status);
        if (r > worstRank) { worstRank = r; worstIdx = i; }
    }
    lv_color_t worstCol = SY_MUTED;
    const char *worst = healthText(disks[worstIdx]->status, &worstCol);
    setTextIfChanged(s_volVal[3], worst);
    lv_obj_set_style_text_color(s_volVal[3], worstCol, 0);

    /* model of the first disk; long models are ellipsised by LV_LABEL_LONG_DOT */
    setTextIfChanged(s_volModel, disks[0]->model[0] ? disks[0]->model : "--");
    lv_obj_set_style_text_color(s_volModel, SY_TEXT, 0);
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
        lv_obj_t *row = mkPanel(scr, SY_PAD, 40 + i * 21, SY_CONTENT_W, 20, SY_PANEL, 6);
        mkLabel(row, labels[i], 8, 3, fSmall(), SY_MUTED, nullptr);
        s_sysVal[i] = nullptr;
        mkLabel(row, "--", 76, 1, fValue(), SY_TEXT, &s_sysVal[i]);
        lv_obj_set_width(s_sysVal[i], 176);
        lv_obj_set_style_text_align(s_sysVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_sysVal[i], LV_LABEL_LONG_DOT);
    }

    s_sysNote = nullptr;
    mkLabel(scr, "", SY_PAD, 214, fSmall(), SY_MUTED, &s_sysNote);
    lv_obj_set_width(s_sysNote, SY_CONTENT_W);
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

    /* firmware_ver is the DSM release string, e.g. "DSM 7.3-81180". */
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
static lv_obj_t *s_upsBig, *s_upsSub, *s_upsPct, *s_upsBar;
static lv_obj_t *s_upsRuntime, *s_upsModel, *s_upsNote;

static void buildUps()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_UPS] = scr;
    pageHeader(scr, "UPS STATUS");

    lv_obj_t *state = mkPanel(scr, SY_PAD, 40, SY_CONTENT_W, 58, SY_PANEL, 10);
    s_upsBig = nullptr;
    mkLabel(state, "WAITING", 12, 9, fBold18(), SY_MUTED, &s_upsBig);
    s_upsSub = nullptr;
    mkLabel(state, "reading UPS...", 12, 34, fSmall(), SY_MUTED, &s_upsSub);
    lv_obj_set_width(s_upsSub, 236);
    lv_label_set_long_mode(s_upsSub, LV_LABEL_LONG_DOT);

    lv_obj_t *bat = mkPanel(scr, SY_PAD, 102, SY_CONTENT_W, 48, SY_PANEL, 10);
    mkLabel(bat, "BATTERY", 12, 6, fSmall(), SY_MUTED, nullptr);
    s_upsPct = nullptr;
    mkLabel(bat, "--", 180, 4, fValue(), SY_TEXT, &s_upsPct);
    lv_obj_set_width(s_upsPct, 66);
    lv_obj_set_style_text_align(s_upsPct, LV_TEXT_ALIGN_RIGHT, 0);

    s_upsBar = lv_bar_create(bat);
    lv_obj_set_pos(s_upsBar, 12, 28);
    lv_obj_set_size(s_upsBar, 236, 8);
    lv_obj_set_style_bg_color(s_upsBar, SY_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_upsBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_upsBar, SY_TEAL, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_upsBar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_upsBar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(s_upsBar, 0, 100);
    lv_bar_set_value(s_upsBar, 0, LV_ANIM_OFF);

    lv_obj_t *rt = mkPanel(scr, SY_PAD, 154, SY_CONTENT_W, 24, SY_PANEL, 6);
    mkLabel(rt, "RUNTIME", 10, 4, fSmall(), SY_MUTED, nullptr);
    s_upsRuntime = nullptr;
    mkLabel(rt, "--:--", 118, 3, fValue(), SY_TEXT, &s_upsRuntime);
    lv_obj_set_width(s_upsRuntime, 132);
    lv_obj_set_style_text_align(s_upsRuntime, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *md = mkPanel(scr, SY_PAD, 182, SY_CONTENT_W, 24, SY_PANEL, 6);
    mkLabel(md, "MODEL", 10, 4, fSmall(), SY_MUTED, nullptr);
    s_upsModel = nullptr;
    mkLabel(md, "--", 118, 3, fValue(), SY_TEXT, &s_upsModel);
    lv_obj_set_width(s_upsModel, 132);
    lv_obj_set_style_text_align(s_upsModel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_upsModel, LV_LABEL_LONG_DOT);

    s_upsNote = nullptr;
    mkLabel(scr, "", SY_PAD, 212, fSmall(), SY_MUTED, &s_upsNote);
    lv_obj_set_size(s_upsNote, SY_CONTENT_W, 18);
    lv_label_set_long_mode(s_upsNote, LV_LABEL_LONG_DOT);
}

static void upsApply(const Snapshot &s)
{
    const char *big = "UNKNOWN";
    lv_color_t col = SY_MUTED;
    const char *sub = "";
    const char *note = "";
    bool known = true;

    switch (s.upsState) {
    case UPS_NONE:
        big = "NOT DETECTED"; col = SY_MUTED;
        sub = "DSM reports no UPS on this NAS";
        break;
    case UPS_LINE:
        big = "ON LINE POWER"; col = SY_TEAL;
        sub = "Running from mains, battery charging";
        break;
    case UPS_BATTERY:
        big = "ON BATTERY"; col = SY_AMBER;
        sub = "Mains lost - running on battery";
        break;
    case UPS_LOW_BATTERY:
        big = "LOW BATTERY"; col = SY_RED;
        sub = "Battery nearly empty";
        break;
    case UPS_UNSUPPORTED:
        big = "NOT SUPPORTED"; col = SY_MUTED;
        sub = "SYNO.Core.ExternalDevice.UPS is not available";
        note = "This DSM does not expose the UPS API.";
        break;
    case UPS_NO_PERMISSION:
        big = "NO PERMISSION"; col = SY_RED;
        sub = "Account lacks the UPS privilege (error 105)";
        note = "A user with the UPS/system privilege is required.";
        break;
    case UPS_ERROR:
        big = "READ ERROR"; col = SY_RED;
        sub = "The UPS API failed; values below may be old";
        known = false;
        break;
    default:
        big = "STATUS UNKNOWN"; col = SY_AMBER;
        sub = s.upsRaw[0] ? "DSM reported an unrecognised state" : "Waiting for UPS data...";
        known = false;
        break;
    }

    setTextIfChanged(s_upsBig, big);
    lv_obj_set_style_text_color(s_upsBig, col, 0);
    setTextIfChanged(s_upsSub, sub);

    if (s.upsChargeValid) {
        char b[12];
        snprintf(b, sizeof(b), "%d%%", (int)s.upsCharge);
        setTextIfChanged(s_upsPct, b);
        lv_obj_set_style_text_color(s_upsPct, s.upsCharge <= 20 ? SY_RED : SY_TEXT, 0);
        lv_bar_set_value(s_upsBar, s.upsCharge, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_upsBar,
            s.upsCharge <= 20 ? SY_RED : (s.upsCharge <= 50 ? SY_AMBER : SY_TEAL),
            LV_PART_INDICATOR);
    } else {
        setTextIfChanged(s_upsPct, "n/a");
        lv_obj_set_style_text_color(s_upsPct, SY_MUTED, 0);
        lv_bar_set_value(s_upsBar, 0, LV_ANIM_OFF);
    }

    /* data.runtime is the estimated remaining runtime in seconds on this DSM;
     * shown as MM:SS (or H:MM:SS past one hour), never rounded to minutes. */
    if (s.upsRuntimeValid && s.upsRuntime >= 0) {
        char b[16];
        fmtRuntime(s.upsRuntime, b, sizeof(b));
        setTextIfChanged(s_upsRuntime, b);
        lv_obj_set_style_text_color(s_upsRuntime, s.upsRuntime <= 300 ? SY_RED : SY_TEXT, 0);
    } else {
        setTextIfChanged(s_upsRuntime, "--:--");
        lv_obj_set_style_text_color(s_upsRuntime, SY_MUTED, 0);
    }

    setTextIfChanged(s_upsModel, s.upsModel[0] ? s.upsModel : "n/a");

    if (!known && s.upsRaw[0]) {
        char b[48];
        snprintf(b, sizeof(b), "reported: %s", s.upsRaw);
        setTextIfChanged(s_upsNote, b);
        lv_obj_set_style_text_color(s_upsNote, SY_AMBER, 0);
    } else {
        setTextIfChanged(s_upsNote, note);
        lv_obj_set_style_text_color(s_upsNote, SY_MUTED, 0);
    }
}

/* ================================================= settings + keyboard */
static lv_obj_t *s_setVal[4];
static lv_obj_t *s_setPort;
static lv_obj_t *s_setStatus;
static Config    s_draft;

static lv_obj_t *s_modal = nullptr;
static lv_obj_t *s_kbCanvas = nullptr;
static lv_obj_t *s_kbTa = nullptr;
static lv_obj_t *s_kbErr = nullptr;
static uint8_t   s_kbMode = 0;      /* 0 abc, 1 numbers, 2 symbols */
static bool      s_kbShift = false;
static int       s_editField = -1;
static int       s_kbPressed = 0;

#define KB_DEL       1
#define KB_CLEAR     2
#define KB_SPACE     3
#define KB_OK        4
#define KB_CLOSE     5
#define KB_MODE_ABC  6
#define KB_MODE_NUM  7
#define KB_MODE_SYM  8
#define KB_SHIFT     9

/* canvas geometry (object-local) */
#define KB_CW 260
#define KB_CH 144
#define KB_MODES_H 22
#define KB_KEY_Y0 26
#define KB_KEY_H 26
#define KB_KEY_STEP 30
#define KB_ACT_Y 118
#define KB_ACT_H 26
#define KB_COL_STEP 29
#define KB_KEY_W 27

static const char *KB_LAY_LOWER[3] = { "abcdefghi", "jklmnopqr", "stuvwxyz" };
static const char *KB_LAY_UPPER[3] = { "ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ" };
static const char *KB_LAY_NUM[3]   = { "123456789", "0-_.:/@&", "()[]{}<>" };
static const char *KB_LAY_SYM[3]   = { "\"'`~^|\\/", "%*+=_-#$", "!:;,.?@" };

static const char *KB_MODES[4] = { "ABC", "123", "SYM", "Aa" };

static void settingsApplyDraft();
static void kbFill();
static void kbExec(int code);

static void kbClose()
{
    if (s_modal) {
        lv_obj_del(s_modal);
        s_modal = nullptr;
    }
    s_kbCanvas = nullptr;
    s_kbTa = nullptr;
    s_kbErr = nullptr;
    s_editField = -1;
    s_kbPressed = 0;
}

static void kbApply()
{
    if (!s_kbTa || s_editField < 0) { kbClose(); return; }
    const char *txt = lv_textarea_get_text(s_kbTa);

    switch (s_editField) {
    case 0: s_draft.host = txt ? txt : ""; break;
    case 1: s_draft.user = txt ? txt : ""; break;
    case 2: s_draft.pass = txt ? txt : ""; break;
    case 3: {
        long p = String(txt ? txt : "").toInt();
        if (p <= 0 || p > 65535) {
            if (s_kbErr) lv_label_set_text(s_kbErr, "Port must be 1-65535");
            return;
        }
        s_draft.port = (uint16_t)p;
        break;
    }
    default: break;
    }
    kbClose();
    settingsApplyDraft();
}

/* ---- cell hit test ---- */
static int kbCellAt(lv_coord_t x, lv_coord_t y)
{
    if (y >= 0 && y < KB_MODES_H && x >= 0 && x < KB_CW) {
        int i = x / 66;
        if (i > 3) i = 3;
        return 100 + i;
    }
    if (y >= KB_KEY_Y0 && y < KB_KEY_Y0 + 3 * KB_KEY_STEP) {
        int r = (y - KB_KEY_Y0) / KB_KEY_STEP;
        int c = x / KB_COL_STEP;
        if (c > 8) c = 8;
        if (c < 0) return 0;
        return 200 + r * 9 + c;
    }
    if (y >= KB_ACT_Y && y < KB_ACT_Y + KB_ACT_H) {
        if (x < 62)  return 300;
        if (x < 170) return 301;
        if (x < 216) return 302;
        return 303;
    }
    return 0;
}

static char kbCharAt(int row, int col)
{
    const char **lay = (s_kbMode == 1) ? KB_LAY_NUM
                     : (s_kbMode == 2) ? KB_LAY_SYM
                     : (s_kbShift ? KB_LAY_UPPER : KB_LAY_LOWER);
    if (row < 0 || row > 2 || col < 0 || col > 8) return 0;
    return lay[row][col];
}

static void kbExec(int code)
{
    if (!s_kbTa) return;

    if (code >= 100 && code <= 103) {
        if (code == 100) s_kbMode = 0;
        else if (code == 101) s_kbMode = 1;
        else if (code == 102) s_kbMode = 2;
        else s_kbShift = !s_kbShift;
        if (s_kbCanvas) lv_obj_invalidate(s_kbCanvas);
        return;
    }
    if (code >= 200 && code <= 226) {
        int idx = code - 200;
        char c = kbCharAt(idx / 9, idx % 9);
        if (!c) return;
        if (s_kbMode == 0 && s_kbShift) {
            c = (char)toupper((unsigned char)c);
            s_kbShift = false;
            if (s_kbCanvas) lv_obj_invalidate(s_kbCanvas);
        }
        lv_textarea_add_char(s_kbTa, c);
        return;
    }
    switch (code) {
    case 300: lv_textarea_set_text(s_kbTa, ""); break;
    case 301: lv_textarea_add_char(s_kbTa, ' '); break;
    case 302: lv_textarea_del_char(s_kbTa); break;
    case 303: kbApply(); break;
    default: break;
    }
}

/* ---- drawing ---- */
static void kbDrawCell(lv_draw_ctx_t *ctx, lv_coord_t x, lv_coord_t y,
                       lv_coord_t w, lv_coord_t h, const char *txt,
                       bool pressed, bool selected, bool primary)
{
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = 6;
    rd.bg_opa = LV_OPA_COVER;

    lv_color_t fg = SY_TEXT;
    if (primary) {
        rd.bg_color = SY_TEAL;
        fg = SY_DARK;
    } else if (pressed) {
        rd.bg_color = SY_PANEL_HI;
        fg = SY_TEAL;
    } else if (selected) {
        rd.bg_color = SY_PANEL_HI;
        fg = SY_TEAL;
    } else {
        rd.bg_color = SY_PANEL;
    }

    lv_area_t a;
    a.x1 = x; a.y1 = y; a.x2 = x + w - 1; a.y2 = y + h - 1;
    lv_draw_rect(ctx, &rd, &a);

    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = fText();
    ld.color = fg;
    ld.align = LV_TEXT_ALIGN_CENTER;

    lv_coord_t lh = lv_font_get_line_height(fText());
    lv_area_t ta;
    ta.x1 = x; ta.y1 = y + (h - lh) / 2; ta.x2 = x + w - 1; ta.y2 = y + h - 1;
    lv_draw_label(ctx, &ld, &ta, txt, NULL);
}

static void kbDraw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    if (!ctx) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t base;
    lv_obj_get_coords(obj, &base);
    lv_coord_t ox = base.x1, oy = base.y1;

    /* mode row */
    for (int i = 0; i < 4; i++) {
        bool sel = (i < 3) ? (s_kbMode == i) : s_kbShift;
        kbDrawCell(ctx, ox + i * 66, oy, 62, KB_MODES_H, KB_MODES[i],
                   s_kbPressed == 100 + i, sel, false);
    }

    /* letter / digit / symbol keys */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 9; c++) {
            char ch = kbCharAt(r, c);
            if (!ch) continue;
            char txt[2] = { ch, 0 };
            int code = 200 + r * 9 + c;
            kbDrawCell(ctx, ox + c * KB_COL_STEP, oy + KB_KEY_Y0 + r * KB_KEY_STEP,
                       KB_KEY_W, KB_KEY_H, txt, s_kbPressed == code, false, false);
        }
    }

    /* actions */
    static const char *lbl[4] = { "CLR", "SPACE", "DEL", "OK" };
    static const lv_coord_t ax[4] = { 0, 66, 174, 220 };
    static const lv_coord_t aw[4] = { 62, 104, 42, 40 };
    for (int i = 0; i < 4; i++) {
        int code = 300 + i;
        kbDrawCell(ctx, ox + ax[i], oy + KB_ACT_Y, aw[i], KB_ACT_H, lbl[i],
                   s_kbPressed == code, false, i == 3);
    }
}

static void kbTouch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        int c = kbCellAt(p.x - a.x1, p.y - a.y1);
        if (c != s_kbPressed) {
            s_kbPressed = c;
            lv_obj_invalidate(obj);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int c = s_kbPressed;
        s_kbPressed = 0;
        lv_obj_invalidate(obj);
        if (code == LV_EVENT_RELEASED && c) kbExec(c);
    }
}

static void kbClose_cb(lv_event_t *e)
{
    (void)e;
    kbClose();
}

static void kbOpen(int field)
{
    if (s_modal) return;

    s_editField = field;
    s_kbShift = false;
    s_kbMode = (field == 3) ? 1 : 0;
    s_kbPressed = 0;

    /* One panel, one textarea, one canvas: the whole keyboard is drawn by the
     * canvas, so a tap on a field costs ~8 objects instead of ~70. */
    lv_obj_t *panel = mkPanel(s_scr[PAGE_SETTINGS], 0, 0, SY_W, SY_H, SY_DARK, 0);
    s_modal = panel;

    static const char *titles[4] = { "Server address", "Account", "Password", "Port" };
    mkLabel(panel, titles[field], SY_PAD, 10, fValue(), SY_TEXT, nullptr);
    mkButton(panel, "X", 240, 10, 30, 20, SY_PANEL, SY_MUTED, kbClose_cb, nullptr);

    s_kbTa = lv_textarea_create(panel);
    lv_obj_set_pos(s_kbTa, SY_PAD, 34);
    lv_obj_set_size(s_kbTa, SY_CONTENT_W, 30);
    lv_textarea_set_one_line(s_kbTa, true);
    lv_obj_set_style_bg_color(s_kbTa, SY_PANEL, 0);
    lv_obj_set_style_border_color(s_kbTa, SY_LINE, 0);
    lv_obj_set_style_border_width(s_kbTa, 1, 0);
    lv_obj_set_style_radius(s_kbTa, 6, 0);
    lv_obj_set_style_text_color(s_kbTa, SY_TEXT, 0);
    lv_obj_set_style_text_font(s_kbTa, fValue(), 0);
    lv_obj_set_style_pad_all(s_kbTa, 5, 0);

    switch (field) {
    case 0:
        lv_textarea_set_text(s_kbTa, s_draft.host.c_str());
        lv_textarea_set_placeholder_text(s_kbTa, "192.168.1.10");
        break;
    case 1:
        lv_textarea_set_text(s_kbTa, s_draft.user.c_str());
        lv_textarea_set_placeholder_text(s_kbTa, "account");
        break;
    case 2:
        lv_textarea_set_text(s_kbTa, s_draft.pass.c_str());
        lv_textarea_set_password_mode(s_kbTa, true);
        lv_textarea_set_placeholder_text(s_kbTa, "password");
        break;
    case 3: {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)s_draft.port);
        lv_textarea_set_text(s_kbTa, buf);
        lv_textarea_set_accepted_chars(s_kbTa, "0123456789");
        break;
    }
    default: break;
    }
    lv_obj_add_state(s_kbTa, LV_STATE_FOCUSED);

    s_kbErr = nullptr;
    mkLabel(panel, "", SY_PAD, 66, fSmall(), SY_RED, &s_kbErr);

    s_kbCanvas = lv_obj_create(panel);
    lv_obj_set_pos(s_kbCanvas, SY_PAD, 84);
    lv_obj_set_size(s_kbCanvas, KB_CW, KB_CH);
    lv_obj_clear_flag(s_kbCanvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_kbCanvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_kbCanvas, 0, 0);
    lv_obj_set_style_pad_all(s_kbCanvas, 0, 0);
    lv_obj_add_event_cb(s_kbCanvas, kbDraw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_kbCanvas, kbTouch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_kbCanvas, kbTouch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_kbCanvas, kbTouch_cb, LV_EVENT_PRESS_LOST, NULL);

    kbFill();
    logLvMem("keyboard");
}

/* kbFill() just repaints; the canvas owns all key state. */
static void kbFill()
{
    if (s_kbCanvas) lv_obj_invalidate(s_kbCanvas);
}

/* ---- settings rows ---- */
static void fieldClick_cb(lv_event_t *e)
{
    int field = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_modal) return;
    kbOpen(field);
}

static void settingsApplyDraft()
{
    setTextIfChanged(s_setVal[0], s_draft.host.length() ? s_draft.host.c_str() : "not set");
    setTextIfChanged(s_setVal[1], s_draft.user.length() ? s_draft.user.c_str() : "not set");
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
    CfgGet(s_draft);
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
    lv_obj_t *row = mkPanel(scr, SY_PAD, y, SY_CONTENT_W, 26, SY_PANEL, 8);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)field);

    mkLabel(row, label, 8, 6, fSmall(), SY_MUTED, nullptr);

    lv_obj_t *v = nullptr;
    mkLabel(row, "-", 76, 4, fValue(), SY_TEXT, &v);
    lv_obj_set_width(v, 174);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v, cb, LV_EVENT_CLICKED, (void *)(intptr_t)field);
    if (valueOut) *valueOut = v;
    return row;
}

static void buildSettings()
{
    lv_obj_t *scr = newScreen(SY_DARK);
    s_scr[PAGE_SETTINGS] = scr;
    pageHeader(scr, "SETTINGS");

    CfgGet(s_draft);

    settingRow(scr, 40,  "ADDRESS",  fieldClick_cb, 0, &s_setVal[0]);
    settingRow(scr, 68,  "ACCOUNT",  fieldClick_cb, 1, &s_setVal[1]);
    settingRow(scr, 96,  "PASSWORD", fieldClick_cb, 2, &s_setVal[2]);
    settingRow(scr, 124, "PROTOCOL", protoToggle_cb, 0, &s_setVal[3]);
    settingRow(scr, 152, "PORT",     fieldClick_cb, 3, &s_setPort);

    mkButton(scr, "TEST",  SY_PAD, 182, 84, 26, SY_PANEL_HI, SY_TEXT, testBtn_cb, nullptr);
    mkButton(scr, "SAVE",  98,     182, 84, 26, SY_TEAL, SY_DARK, saveBtn_cb, nullptr);
    mkButton(scr, "CLEAR", 186,    182, 84, 26, SY_PANEL_HI, SY_RED, clearBtn_cb, nullptr);

    s_setStatus = nullptr;
    mkLabel(scr, "Not connected", SY_PAD, 214, fSmall(), SY_MUTED, &s_setStatus);
    lv_obj_set_width(s_setStatus, SY_CONTENT_W);
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
        if (lv_obj_check_type(o, &lv_btn_class)) return true;
        o = lv_obj_get_parent(o);
    }
    return false;
}

static lv_scr_load_anim_t animFor(PageId from, PageId to)
{
    switch (to) {
    case PAGE_OVERVIEW: return LV_SCR_LOAD_ANIM_MOVE_TOP;
    case PAGE_SETTINGS: return LV_SCR_LOAD_ANIM_MOVE_TOP;
    case PAGE_STORAGE:  return LV_SCR_LOAD_ANIM_MOVE_LEFT;
    case PAGE_SYSTEM:   return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    case PAGE_VOLUME:     return LV_SCR_LOAD_ANIM_MOVE_LEFT;
    case PAGE_UPS:      return LV_SCR_LOAD_ANIM_MOVE_LEFT;
    case PAGE_HOME:
    default:
        if (from == PAGE_STORAGE) return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        if (from == PAGE_SYSTEM)  return LV_SCR_LOAD_ANIM_MOVE_LEFT;
        if (from == PAGE_UPS)     return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        if (from == PAGE_VOLUME)    return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        return LV_SCR_LOAD_ANIM_MOVE_BOTTOM;
    }
}

void UiNavigate(PageId page, bool animate)
{
    if (page >= PAGE_MAX) return;
    if (!ensurePage(page)) return;
    if (page == s_cur) return;

    if (s_modal) kbClose();

    PageId from = s_cur;
    bool dropPrev = (from != PAGE_HOME);

    s_cur = page;
    NetSetPage(page);
    s_navBlockUntil = millis() + 320;

    lv_scr_load_anim(s_scr[page], animFor(from, page), animate ? 180 : 0, 0, dropPrev);
    /* LVGL deletes the outgoing page when the animation ends. */
    if (dropPrev) s_scr[from] = nullptr;

    if (page == PAGE_SETTINGS) {
        CfgGet(s_draft);
        settingsApplyDraft();
    } else if (page == PAGE_OVERVIEW || page == PAGE_STORAGE || page == PAGE_VOLUME ||
               page == PAGE_SYSTEM || page == PAGE_UPS) {
        NetRequestFast();
    }

    if (DataLock(20)) {
        Snapshot s = Data();
        DataUnlock();
        if (page == PAGE_HOME) updateHome(s, true);
        else if (page == PAGE_OVERVIEW) overviewApply(s);
        else if (page == PAGE_STORAGE) storApply(s);
        else if (page == PAGE_VOLUME) volApply(s);
        else if (page == PAGE_SYSTEM) sysApply(s);
        else if (page == PAGE_UPS) upsApply(s);
        else if (page == PAGE_SETTINGS) settingsRefresh(s);
    }
}

PageId UiCurrentPage()
{
    return s_cur;
}

/* True when the touch started inside the scrollable user list: those swipes
 * belong to the list, not to page navigation. */
static bool fingerOnUserList()
{
    lv_obj_t *o = lv_indev_get_obj_act();
    int depth = 0;
    while (o && depth++ < 12) {
        if (o == s_usrList) return true;
        o = lv_obj_get_parent(o);
    }
    return false;
}

static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (s_modal) return;
    if ((int32_t)(millis() - s_navBlockUntil) < 0) return;
    if (underFingerBlocked()) return;
    if (s_cur == PAGE_OVERVIEW && fingerOnUserList()) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_NONE) return;

    s_gestureAt = millis();

    if (s_cur == PAGE_HOME) {
        if (dir == LV_DIR_LEFT)        UiNavigate(PAGE_STORAGE, true);
        else if (dir == LV_DIR_RIGHT)  UiNavigate(PAGE_SYSTEM, true);
        else if (dir == LV_DIR_BOTTOM) UiNavigate(PAGE_SETTINGS, true);
        else if (dir == LV_DIR_TOP)    UiNavigate(PAGE_OVERVIEW, true);
        return;
    }

    /* The disk page is one level below storage: go back to the list. */
    if (s_cur == PAGE_VOLUME) {
        UiNavigate(PAGE_STORAGE, true);
        return;
    }

    /* Every other sub page is one level deep: any swipe returns home. */
    UiNavigate(PAGE_HOME, true);
}

bool UiHandleBack()
{
    if (s_modal) {
        kbClose();
        return true;
    }
    if (s_cur != PAGE_HOME) {
        UiNavigate(PAGE_HOME, true);
        return true;
    }
    return false;
}

/* ================================================== page construction */
static bool ensurePage(PageId page)
{
    if (page >= PAGE_MAX) return false;
    if (s_scr[page]) return true;
    buildPage(page);
    return s_scr[page] != nullptr;
}

static void buildPage(PageId page)
{
    switch (page) {
    case PAGE_HOME:     if (!s_scr[PAGE_HOME]) buildHome(); break;
    case PAGE_OVERVIEW: buildOverview(); break;
    case PAGE_SETTINGS: buildSettings(); break;
    case PAGE_STORAGE:  buildStorage(); break;
    case PAGE_VOLUME:     buildVolume(); break;
    case PAGE_SYSTEM:   buildSystem(); break;
    case PAGE_UPS:      buildUps(); break;
    default: break;
    }
}

/* ================================================================ public */
void UiBuild(void *bsp)
{
    s_dev = (CHAPPIE *)bsp;
    s_cur = PAGE_HOME;
    s_storPage = 0;
    s_peakDown = 0.0;
    s_peakUp = 0.0;
    s_navBlockUntil = 0;
    s_modal = nullptr;
    s_lastCpu = -1;
    s_lastRam = -1;
    s_volIndex = 0;
    s_gestureAt = 0;

    stylesInit();

    /* Only the home page is created up front: the other pages are built when
     * they are opened and freed when they are left, which keeps the 48 KB
     * LVGL pool free for layers and the keyboard. */
    for (int i = 0; i < PAGE_MAX; i++) s_scr[i] = nullptr;
    buildHome();

    NetSetPage(PAGE_HOME);
    /* No fade: a whole-screen opacity animation asks for a large layer that
     * this pool cannot spare. */
    lv_disp_load_scr(s_scr[PAGE_HOME]);
    logLvMem("home built");
}

void UiDestroy()
{
    if (s_modal) kbClose();

    /* A running lv_scr_load_anim keeps raw pointers to both screens in
     * lv_disp_t and animates them; cancel those first. */
    lv_disp_t *d = lv_disp_get_default();
    if (d) {
        for (int i = 0; i < PAGE_MAX; i++) {
            if (s_scr[i]) lv_anim_del(s_scr[i], nullptr);
        }
        if (d->prev_scr && d->prev_scr != d->act_scr) {
            bool tracked = false;
            for (int i = 0; i < PAGE_MAX; i++) {
                if (s_scr[i] == d->prev_scr) { tracked = true; break; }
            }
            if (!tracked) lv_obj_del(d->prev_scr);
        }
        d->scr_to_load = nullptr;
        d->prev_scr = nullptr;
        d->del_prev = false;
        d->draw_prev_over_act = false;
    }

    lv_disp_load_scr(lv_obj_create(NULL));

    for (int i = 0; i < PAGE_MAX; i++) {
        if (s_scr[i]) {
            lv_obj_del(s_scr[i]);
            s_scr[i] = nullptr;
        }
    }

    s_cur = PAGE_HOME;
    s_timeLabel = nullptr;
    s_dlVal = s_ulVal = s_dlPeak = s_ulPeak = nullptr;
    s_dot = s_netState = nullptr;
    s_usrList = s_usrMsg = s_usrNote = nullptr;
    for (int i = 0; i < SY_MAX_SESSIONS; i++) {
        s_usrDot[i] = s_usrName[i] = s_usrProto[i] = s_usrTime[i] = nullptr;
    }
    s_volDiskLine = s_volArc = s_volArcPct = s_volName = s_volModel = nullptr;
    for (int i = 0; i < 4; i++) s_volVal[i] = nullptr;
    s_storMsg = s_storPageLbl = nullptr;
    s_storDisk = s_storPrev = s_storNext = nullptr;
    for (int i = 0; i < STOR_CARDS; i++) {
        s_storCard[i] = s_storName[i] = s_storPct[i] = s_storBar[i] = s_storSize[i] = nullptr;
    }
    for (int i = 0; i < SYS_ROWS; i++) s_sysVal[i] = nullptr;
    s_sysNote = nullptr;
    s_upsBig = s_upsSub = s_upsPct = s_upsBar = s_upsRuntime = s_upsModel = s_upsNote = nullptr;
    for (int i = 0; i < 4; i++) s_setVal[i] = nullptr;
    s_setPort = s_setStatus = nullptr;

    Meter1.METER = Meter2.METER = nullptr;
    Meter1.INDIC = Meter2.INDIC = nullptr;
    Meter1.Meter_val_old = Meter2.Meter_val_old = 0;
}

void UiRefresh()
{
    if (!s_scr[PAGE_HOME]) return;

    Snapshot s;
    if (!DataLock(15)) return;
    s = Data();
    DataUnlock();

    static uint32_t seenFast = 0, seenSys = 0, seenStor = 0, seenUps = 0;

    if (s.fastSeq != seenFast) {
        seenFast = s.fastSeq;
        if (s.netValid && !s.netStale) {
            if (s.downBps > s_peakDown) s_peakDown = s.downBps;
            if (s.upBps > s_peakUp) s_peakUp = s.upBps;
        }
    }

    if (s_scr[PAGE_HOME]) updateNetLabels(s);

    if (s_cur == PAGE_HOME) {
        updateHome(s, false);
    } else if (s_cur == PAGE_OVERVIEW) {
        if (!s_scr[PAGE_OVERVIEW]) return;
        /* cheap enough to refresh every tick: 8 rows of setTextIfChanged */
        overviewApply(s);
    } else if (s_cur == PAGE_STORAGE) {
        if (!s_scr[PAGE_STORAGE]) return;
        if (s.storageSeq != seenStor) {
            seenStor = s.storageSeq;
            storApply(s);
        }
    } else if (s_cur == PAGE_VOLUME) {
        if (!s_scr[PAGE_VOLUME]) return;
        if (s.storageSeq != seenStor) {
            seenStor = s.storageSeq;
            volApply(s);
        }
    } else if (s_cur == PAGE_SYSTEM) {
        if (!s_scr[PAGE_SYSTEM]) return;
        if (s.sysSeq != seenSys) {
            seenSys = s.sysSeq;
            sysApply(s);
        }
    } else if (s_cur == PAGE_UPS) {
        if (!s_scr[PAGE_UPS]) return;
        if (s.upsSeq != seenUps) {
            seenUps = s.upsSeq;
            upsApply(s);
        }
    } else if (s_cur == PAGE_SETTINGS) {
        if (s_scr[PAGE_SETTINGS]) settingsRefresh(s);
    }
}

} // namespace Sy
