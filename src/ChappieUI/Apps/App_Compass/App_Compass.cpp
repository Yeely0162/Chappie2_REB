#if 1
/**
 * @file App_Compass.cpp
 * @brief Tilt-compensated compass and attitude indicator for Chappie2.
 *
 * Sensor policy:
 *  - all I2C is done in onLoop(), never in an LVGL callback
 *  - the LVGL timer only renders cached values
 *  - the BMM150 is initialised lazily when this app is opened
 *  - the MPU6050 DMP is initialised once at boot; getYawPitchRoll() returns
 *    0 for a new packet, 1 when the IMU is not ready and 2 when no new
 *    packet is available
 *
 * Calibration:
 *  - hard iron: per-axis min/max midpoint
 *  - soft iron: per-axis range normalisation
 *  - full cross-axis ellipsoid fitting is not claimed
 *  - parameters are stored in the magnetometer's native frame in NVS
 *    namespace "compass2", with a format version.  Version 2 is required
 *    because the BMM150 raw-data decode was corrected (v1 data is invalid).
 *
 * Coordinate system (same as the BMM150 datasheet's Android frame):
 *  - +X: screen right
 *  - +Y: screen top
 *  - +Z: out of the screen, towards the user
 *  - heading 0 deg = +Y points to magnetic north, 90 deg = east
 */
#include "App_Compass.h"
#include "../../../ChappieBsp/Chappie.h"
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(ui_img_icon_compass_png);

/* ---------- palette ---------- */
#define C_BG            0x07111C
#define C_PANEL         0x0D1A27
#define C_PANEL_ALT     0x15293D
#define C_LINE          0x243B50
#define C_TEXT          0xEAF3FA
#define C_DIM           0x7E93A8
#define C_ACCENT        0x2FD4E6
#define C_POINTER       0xFF6B3D
#define C_OK            0x43E08B
#define C_WARN          0xFFC857
#define C_DANGER        0xFF5A66
#define C_SKY           0x0E3A5C
#define C_GROUND        0x63381B
#define C_HORIZON       0xF2F7FB
#define C_LADDER        0xD6E3EF

/* ---------- sensor -> device axis mapping ----------
 * Device frame: +X screen right, +Y screen top, +Z out of the screen.
 *
 * MPU6050 (U8, pin 1 at the board top-left, P&P rotation 270 deg):
 *   The observed tilt response and a level attitude give a right-handed
 *   sensor frame rotated 180 deg about Z relative to the screen:
 *       X_dev = -X_imu,  Y_dev = -Y_imu,  Z_dev = +Z_imu
 *
 * BMM150 (U4, A1 at the board bottom-left, P&P rotation 180 deg):
 *   Datasheet Table 33 with A1 in the bottom-left corner gives:
 *       X_dev = -X_mag,  Y_dev = +Y_mag,  Z_dev = -Z_mag
 *   (the datasheet's +Z points into the PCB, the device +Z points out).
 *
 * The MAG calibration stays in the BMM150 native frame; the mapping is
 * applied only after the hard/soft-iron correction, so an existing
 * calibration is never compensated twice.
 */
#define COMPASS_IMU_INVERT_X        1
#define COMPASS_IMU_INVERT_Y        1
#define COMPASS_IMU_INVERT_Z        0
#define COMPASS_MAG_INVERT_X        1
#define COMPASS_MAG_INVERT_Y        0
#define COMPASS_MAG_INVERT_Z        1
/* Set this only to magnetic declination for your location.  Zero means the
 * compass reports magnetic north, which is what an external compass shows. */
#define COMPASS_HEADING_OFFSET      0.0f
/* Right bank (left edge up) must tilt the horizon's right side up. */
#define COMPASS_HORIZON_ROLL_SIGN   (-1.0f)
#define COMPASS_HORIZON_PITCH_SIGN  1.0f

/* Print raw/calibrated/device MAG vectors and the field-range decision.
 * Throttled to state transitions and 2 s while a fault persists. */
#define COMPASS_MAG_DIAG_LOG 1

/* ---------- timing / limits ---------- */
static const uint32_t SAMPLE_PERIOD_MS = 100;
static const uint32_t UI_PERIOD_MS = 50;
static const uint32_t RTC_PERIOD_MS = 1000;
/* Earth's field is normally ~25-65 uT.  This window is deliberately wide:
 * it rejects open-circuit / saturated readings and strong local magnets
 * without rejecting normal regional variation or hand-held rotation. */
static const float FIELD_MIN_VALID_UT = 10.0f;
static const float FIELD_MAX_VALID_UT = 120.0f;
/* Do not invalidate the needle on a single glitch or a momentary bad sample.
 * ~3 samples at 10 Hz is 300 ms. */
static const uint8_t MAG_FAIL_LIMIT = 3;
static const uint8_t FIELD_BAD_LIMIT = 3;
/* Bump this whenever the raw data format or axis mapping changes.  Saved
 * calibration offsets/scales from an older format must not be reused. */
static const uint32_t CAL_VERSION = 2;
static const uint16_t CAL_MIN_SAMPLES = 100;
static const float CAL_MIN_RANGE_UT = 12.0f;
static const float CAL_MIN_SCALE = 0.5f;
static const float CAL_MAX_SCALE = 2.0f;
static const float PI_F = 3.14159265358979323846f;
static const float DEG_TO_RAD_F = PI_F / 180.0f;
static const float RAD_TO_DEG_F = 180.0f / PI_F;

static std::string app_name = "Compass";
static CHAPPIE* device = nullptr;

enum Page : uint8_t {
    PAGE_COMPASS = 0,
    PAGE_ATTITUDE,
    PAGE_SETTINGS,
    PAGE_CALIBRATION,
};

enum CalMsg : uint8_t {
    CAL_MSG_NONE = 0,
    CAL_MSG_COLLECTING,
    CAL_MSG_INSUFFICIENT,
    CAL_MSG_OK,
    CAL_MSG_CANCELLED,
    CAL_MSG_CLEARED,
    CAL_MSG_FAILED,
};

/* ---------- cached sensor state ---------- */
static volatile float _mx = 0.0f, _my = 0.0f, _mz = 0.0f;   /* device frame, uT */
static volatile float _field = 0.0f;                          /* calibrated, uT */
static volatile float _pitch = 0.0f, _roll = 0.0f;            /* device frame, deg */
static volatile float _targetHeading = 0.0f;
static volatile bool _magOk = false;
static volatile bool _imuOk = false;
static volatile bool _headingOk = false;
static volatile bool _haveImuSample = false;
static volatile bool _fieldInRange = false;
static volatile uint8_t _magStatus = ChappieMAG::STATUS_NOT_INIT;
static volatile uint8_t _magReadReason = (uint8_t)ChappieMAG::READ_NOT_READY;
static volatile bool _magReadFailed = false;
static volatile uint8_t _magFailCount = 0;
static volatile uint8_t _fieldBadCount = 0;
/* DMP gravity, raw sensor frame; mapped to the device frame before use. */
static volatile float _gravityX = 0.0f, _gravityY = 0.0f, _gravityZ = 1.0f;
static volatile uint8_t _hour = 0, _minute = 0;
static volatile bool _rtcOk = false;
static volatile uint32_t _sampleVersion = 0;

/* ---------- smoothing / display ---------- */
static bool _filterInit = false;
static float _sinAcc = 0.0f, _cosAcc = 0.0f;
static float _displayHeading = 0.0f;
static bool _displayInit = false;

/* ---------- calibration state ---------- */
static volatile bool _calActive = false;
static volatile bool _calFinishRequested = false;
static volatile bool _calCancelRequested = false;
static volatile bool _calClearRequested = false;
static volatile bool _retryMagRequested = false;
static volatile bool _returnToSettingsRequested = false;
static volatile uint16_t _calSamples = 0;
static volatile float _calMinX = 0.0f, _calMaxX = 0.0f;
static volatile float _calMinY = 0.0f, _calMaxY = 0.0f;
static volatile float _calMinZ = 0.0f, _calMaxZ = 0.0f;
static volatile uint8_t _calCoverage = 0;
static volatile uint8_t _calProgress = 0;
static volatile uint8_t _calMsg = CAL_MSG_NONE;
static volatile bool _calibrated = false;
static float _offX = 0.0f, _offY = 0.0f, _offZ = 0.0f;
static float _scaleX = 1.0f, _scaleY = 1.0f, _scaleZ = 1.0f;

/* ---------- widget pointers ---------- */
static lv_timer_t* _uiTimer = nullptr;
static Page _page = PAGE_COMPASS;
static uint32_t _lastSampleMs = 0;
static uint32_t _lastRtcMs = 0;

static lv_obj_t* _pageCompass = nullptr;
static lv_obj_t* _pageAttitude = nullptr;
static lv_obj_t* _pageSettings = nullptr;
static lv_obj_t* _pageCal = nullptr;

static lv_obj_t* _timeLabel = nullptr;
static lv_obj_t* _statusLabel = nullptr;
static lv_obj_t* _dial = nullptr;
static lv_obj_t* _needleLine = nullptr;
static lv_obj_t* _headingLabel = nullptr;
static lv_obj_t* _cardinalLabel = nullptr;
static lv_obj_t* _degreeRing = nullptr;
static lv_obj_t* _xLabel = nullptr;
static lv_obj_t* _yLabel = nullptr;
static lv_obj_t* _zLabel = nullptr;
static lv_obj_t* _fLabel = nullptr;
static lv_obj_t* _pLabel = nullptr;
static lv_obj_t* _rLabel = nullptr;
static lv_obj_t* _levelMain = nullptr;
static lv_obj_t* _fieldValueLabel = nullptr;
static lv_obj_t* _fieldSeg[10] = {nullptr};

static lv_obj_t* _attView = nullptr;
static lv_obj_t* _attPitchLabel = nullptr;
static lv_obj_t* _attRollLabel = nullptr;
static lv_obj_t* _levelLabel = nullptr;

/* Attitude scene state (degrees, already sign-corrected for the display). */
static float _attPitchDisp = 0.0f;
static float _attRollDisp = 0.0f;
static float _attDrawnPitch = 0.0f;
static float _attDrawnRoll = 0.0f;
static bool _attDispInit = false;
static bool _attDrawnValid = false;
static bool _attShownValid = false;
static bool _attShownImuOk = false;
static int _attShownPitch = 1000;
static int _attShownRoll = 1000;

static lv_obj_t* _setMagLabel = nullptr;
static lv_obj_t* _setCalLabel = nullptr;
static lv_obj_t* _setImuLabel = nullptr;

static lv_obj_t* _calBar = nullptr;
static lv_obj_t* _calSampleLabel = nullptr;
static lv_obj_t* _calRangeLabel = nullptr;
static lv_obj_t* _calStatusLabel = nullptr;

static lv_point_t _needlePts[2] = {{78, 78}, {78, 22}};

/* ---------- helpers ---------- */
static inline lv_color_t col(uint32_t hex)
{
    return lv_color_hex(hex);
}

static inline float norm360(float angle)
{
    angle = fmodf(angle, 360.0f);
    if (angle < 0.0f)
        angle += 360.0f;
    return angle;
}

static inline float wrap180(float angle)
{
    angle = fmodf(angle + 180.0f, 360.0f);
    if (angle < 0.0f)
        angle += 360.0f;
    return angle - 180.0f;
}

static const char* cardinal(float angle)
{
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((norm360(angle) + 22.5f) / 45.0f) % 8;
    return names[idx];
}

static const lv_font_t* dataFont()
{
    const lv_font_t* f = ResourcePool::GetFont("bahnschrift_13");
    return f ? f : &lv_font_montserrat_14;
}

static const lv_font_t* dataFontLarge()
{
    const lv_font_t* f = ResourcePool::GetFont("bahnschrift_17");
    return f ? f : &lv_font_montserrat_18;
}

static const lv_font_t* buttonFont()
{
    const lv_font_t* f = ResourcePool::GetFont("UbuntuBold18");
    return f ? f : &lv_font_montserrat_18;
}

static void setText(lv_obj_t* label, const char* text)
{
    if (label != nullptr)
        lv_label_set_text(label, text);
}

static void setLabel(lv_obj_t* label, const char* text, uint32_t color)
{
    if (label == nullptr)
        return;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, col(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                           uint32_t color, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_text_align_t align, bool wrap = false)
{
    lv_obj_t* label = lv_label_create(parent);
    /* Drop theme-provided border/shadow/outline so no stray chrome is drawn. */
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, col(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(label, x, y);
    if (w > 0) {
        lv_obj_set_width(label, w);
        lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    }
    return label;
}

static lv_obj_t* makeRect(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, uint32_t color)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, col(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

/* lv_line keeps the point array by reference, so give every line a persistent slot. */
static const uint8_t LINE_POOL_SIZE = 12;
static lv_point_t _linePtPool[LINE_POOL_SIZE][2];
static uint8_t _linePtNext = 0;

static lv_obj_t* makeLine(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, uint32_t color, bool rounded)
{
    if (_linePtNext >= LINE_POOL_SIZE)
        _linePtNext = 0;

    lv_point_t* pts = _linePtPool[_linePtNext++];
    pts[0].x = 0;
    pts[0].y = 0;
    pts[1].x = w;
    pts[1].y = h;

    lv_obj_t* obj = lv_line_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w > 0 ? w + 1 : 1, h > 0 ? h + 1 : 1);
    lv_line_set_points(obj, pts, 2);
    lv_obj_set_style_line_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(obj, col(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(obj, rounded, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h, bool filled,
                            lv_event_cb_t cb, void* user_data = nullptr)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, col(filled ? C_ACCENT : C_PANEL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, filled ? 0 : 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, col(C_LINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (cb != nullptr)
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, buttonFont(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, col(filled ? C_BG : C_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t* makePage()
{
    lv_obj_t* page = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, 280, 240);
    lv_obj_set_style_bg_color(page, col(C_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void setPage(Page page)
{
    _page = page;
    if (_pageCompass != nullptr) lv_obj_add_flag(_pageCompass, LV_OBJ_FLAG_HIDDEN);
    if (_pageAttitude != nullptr) lv_obj_add_flag(_pageAttitude, LV_OBJ_FLAG_HIDDEN);
    if (_pageSettings != nullptr) lv_obj_add_flag(_pageSettings, LV_OBJ_FLAG_HIDDEN);
    if (_pageCal != nullptr) lv_obj_add_flag(_pageCal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* target = nullptr;
    if (page == PAGE_COMPASS) target = _pageCompass;
    else if (page == PAGE_ATTITUDE) target = _pageAttitude;
    else if (page == PAGE_SETTINGS) target = _pageSettings;
    else if (page == PAGE_CALIBRATION) target = _pageCal;
    if (target != nullptr) {
        lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
        if (target == _pageAttitude && _attView != nullptr) {
            _attDispInit = false;
            _attDrawnValid = false;
            lv_obj_invalidate(_attView);
        }
    }
}

/* ---------- calibration storage ---------- */
static bool finiteFloat(float v)
{
    return !isnan(v) && !isinf(v);
}

static void loadCalibration()
{
    Preferences prefs;
    bool valid = false;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    float sx = 1.0f, sy = 1.0f, sz = 1.0f;

    uint32_t version = 0;
    bool hadSavedValid = false;
    if (prefs.begin("compass2", true)) {
        version = prefs.getUInt("ver", 0);
        valid = prefs.getBool("valid", false);
        hadSavedValid = valid;
        ox = prefs.getFloat("ox", 0.0f);
        oy = prefs.getFloat("oy", 0.0f);
        oz = prefs.getFloat("oz", 0.0f);
        sx = prefs.getFloat("sx", 1.0f);
        sy = prefs.getFloat("sy", 1.0f);
        sz = prefs.getFloat("sz", 1.0f);
        prefs.end();
    }

    /* v1 stored offsets derived from the broken BMM150 LSB decode.  Reusing
     * them would corrupt both the field magnitude and the heading. */
    if (version != CAL_VERSION)
        valid = false;

    if (valid && finiteFloat(ox) && finiteFloat(oy) && finiteFloat(oz) &&
        finiteFloat(sx) && finiteFloat(sy) && finiteFloat(sz) &&
        sx > 0.1f && sx < 10.0f && sy > 0.1f && sy < 10.0f &&
        sz > 0.1f && sz < 10.0f) {
        _offX = ox; _offY = oy; _offZ = oz;
        _scaleX = sx; _scaleY = sy; _scaleZ = sz;
        _calibrated = true;
        _calMsg = CAL_MSG_OK;
    } else {
        _offX = _offY = _offZ = 0.0f;
        _scaleX = _scaleY = _scaleZ = 1.0f;
        _calibrated = false;
        _calMsg = CAL_MSG_NONE;
        if (hadSavedValid && version != CAL_VERSION)
            printf("[Compass] old calibration ignored (saved v%u, need v%u); recalibrate\n",
                   (unsigned)version, (unsigned)CAL_VERSION);
    }
    printf("[Compass] calibration %s\n", _calibrated ? "loaded" : "not set");
}

static void saveCalibration()
{
    Preferences prefs;
    if (prefs.begin("compass2", false)) {
        prefs.putUInt("ver", CAL_VERSION);
        prefs.putBool("valid", true);
        prefs.putFloat("ox", _offX);
        prefs.putFloat("oy", _offY);
        prefs.putFloat("oz", _offZ);
        prefs.putFloat("sx", _scaleX);
        prefs.putFloat("sy", _scaleY);
        prefs.putFloat("sz", _scaleZ);
        prefs.end();
    }
}

static void clearCalibrationStorage()
{
    Preferences prefs;
    if (prefs.begin("compass2", false)) {
        prefs.clear();
        prefs.end();
    }
    _offX = _offY = _offZ = 0.0f;
    _scaleX = _scaleY = _scaleZ = 1.0f;
    _filterInit = false;
    _displayInit = false;
}

/* ---------- calibration logic ---------- */
static void updateCalibrationProgress()
{
    float sx = _calMaxX - _calMinX;
    float sy = _calMaxY - _calMinY;
    float sz = _calMaxZ - _calMinZ;
    if (sx < 0.0f) sx = 0.0f;
    if (sy < 0.0f) sy = 0.0f;
    if (sz < 0.0f) sz = 0.0f;

    uint8_t coverage = 0;
    if (sx >= CAL_MIN_RANGE_UT) coverage++;
    if (sy >= CAL_MIN_RANGE_UT) coverage++;
    if (sz >= CAL_MIN_RANGE_UT) coverage++;
    _calCoverage = coverage;

    uint32_t samplePct = (uint32_t)_calSamples * 100U / CAL_MIN_SAMPLES;
    if (samplePct > 100U) samplePct = 100U;
    uint32_t coveragePct = (uint32_t)coverage * 100U / 3U;
    _calProgress = (uint8_t)(samplePct < coveragePct ? samplePct : coveragePct);
}

static void startCalibration()
{
    _calSamples = 0;
    _calMinX = _calMinY = _calMinZ = 1e9f;
    _calMaxX = _calMaxY = _calMaxZ = -1e9f;
    _calCoverage = 0;
    _calProgress = 0;
    _calMsg = CAL_MSG_COLLECTING;
    _calActive = true;
    _calFinishRequested = false;
    _calCancelRequested = false;
    _returnToSettingsRequested = false;
    setPage(PAGE_CALIBRATION);
}

static void finishCalibration()
{
    updateCalibrationProgress();
    if (_calSamples < CAL_MIN_SAMPLES || _calCoverage < 3U) {
        _calMsg = CAL_MSG_INSUFFICIENT;
        return;
    }

    float sx = _calMaxX - _calMinX;
    float sy = _calMaxY - _calMinY;
    float sz = _calMaxZ - _calMinZ;
    float avg = (sx + sy + sz) / 3.0f;
    if (avg <= 0.0f) {
        _calMsg = CAL_MSG_FAILED;
        return;
    }

    _offX = (_calMaxX + _calMinX) * 0.5f;
    _offY = (_calMaxY + _calMinY) * 0.5f;
    _offZ = (_calMaxZ + _calMinZ) * 0.5f;

    _scaleX = avg / sx;
    _scaleY = avg / sy;
    _scaleZ = avg / sz;
    if (_scaleX < CAL_MIN_SCALE) _scaleX = CAL_MIN_SCALE;
    if (_scaleX > CAL_MAX_SCALE) _scaleX = CAL_MAX_SCALE;
    if (_scaleY < CAL_MIN_SCALE) _scaleY = CAL_MIN_SCALE;
    if (_scaleY > CAL_MAX_SCALE) _scaleY = CAL_MAX_SCALE;
    if (_scaleZ < CAL_MIN_SCALE) _scaleZ = CAL_MIN_SCALE;
    if (_scaleZ > CAL_MAX_SCALE) _scaleZ = CAL_MAX_SCALE;

    _calibrated = true;
    _calActive = false;
    _calMsg = CAL_MSG_OK;
    _filterInit = false;
    _displayInit = false;
    saveCalibration();
    _returnToSettingsRequested = true;
    printf("[Compass] calibrated: off %.1f %.1f %.1f, scale %.2f %.2f %.2f\n",
           _offX, _offY, _offZ, _scaleX, _scaleY, _scaleZ);
}

static void clearCalibration()
{
    clearCalibrationStorage();
    _calibrated = false;
    _calActive = false;
    _calMsg = CAL_MSG_CLEARED;
    _returnToSettingsRequested = true;
}

/* ---------- heading ---------- */
static void mapMagAxes(float& x, float& y, float& z)
{
#if COMPASS_MAG_INVERT_X
    x = -x;
#endif
#if COMPASS_MAG_INVERT_Y
    y = -y;
#endif
#if COMPASS_MAG_INVERT_Z
    z = -z;
#endif
}

#if COMPASS_MAG_DIAG_LOG
static uint32_t _magDiagLastMs = 0;
static bool _magDiagLastRange = true;
static bool _magDiagFirst = true;

static void logMagSample(float rawX, float rawY, float rawZ,
                         float calX, float calY, float calZ,
                         float devX, float devY, float devZ,
                         float rawField, float calField, bool inRange)
{
    uint32_t now = millis();
    bool changed = _magDiagFirst || (inRange != _magDiagLastRange);
    if (!changed && inRange)
        return;
    if (!changed && (now - _magDiagLastMs) < 2000)
        return;
    _magDiagLastMs = now;
    _magDiagLastRange = inRange;
    _magDiagFirst = false;

    printf("[Compass] MAG rawADC=(%d,%d,%d) rhall=%u | "
           "raw=(%.2f,%.2f,%.2f)uT Fraw=%.2f | "
           "cal=(%.2f,%.2f,%.2f)uT F=%.2f | dev=(%.2f,%.2f,%.2f) | "
           "%s (valid %.0f..%.0f uT)\n",
           (device != nullptr) ? (int)device->Mag.rawX() : 0,
           (device != nullptr) ? (int)device->Mag.rawY() : 0,
           (device != nullptr) ? (int)device->Mag.rawZ() : 0,
           (device != nullptr) ? (unsigned)device->Mag.rawHall() : 0u,
           (double)rawX, (double)rawY, (double)rawZ, (double)rawField,
           (double)calX, (double)calY, (double)calZ, (double)calField,
           (double)devX, (double)devY, (double)devZ,
           inRange ? "FIELD OK" : "FIELD OUT OF RANGE",
           (double)FIELD_MIN_VALID_UT, (double)FIELD_MAX_VALID_UT);
}
#else
static void logMagSample(float, float, float, float, float, float,
                         float, float, float, float, float, bool) {}
#endif

static void updateHeading(float mx, float my, float mz)
{
    /* Hard/soft-iron calibration is defined in the BMM150 native frame.
     * Apply it first, then rotate the corrected vector into the device frame.
     * This keeps the calibration independent of the native->device rotation;
     * the CAL_VERSION check separately invalidates offsets captured with the
     * old, broken raw-data decode. */
    float cx = (mx - _offX) * _scaleX;
    float cy = (my - _offY) * _scaleY;
    float cz = (mz - _offZ) * _scaleZ;

    float rawField = sqrtf(mx * mx + my * my + mz * mz);
    float field = sqrtf(cx * cx + cy * cy + cz * cz);
    if (!finiteFloat(field))
        field = 0.0f;

    _field = field;
    _fieldInRange = (field >= FIELD_MIN_VALID_UT && field <= FIELD_MAX_VALID_UT);

    /* Native -> device frame, after calibration. */
    float dx = cx, dy = cy, dz = cz;
    mapMagAxes(dx, dy, dz);
    _mx = dx;
    _my = dy;
    _mz = dz;

    logMagSample(mx, my, mz, cx, cy, cz, dx, dy, dz, rawField, field, _fieldInRange);

    if (!_fieldInRange) {
        /* Brief debounce so one sample at a magnetic discontinuity does not
         * blank the compass; after FIELD_BAD_LIMIT the heading is invalid. */
        if (_fieldBadCount < 255)
            _fieldBadCount++;
        if (_fieldBadCount >= FIELD_BAD_LIMIT)
            _headingOk = false;
        return;
    }
    _fieldBadCount = 0;

    if (!_imuOk) {
        _headingOk = false;
        return;
    }

    /* The DMP gravity vector is the world-up direction in the MPU sensor
     * frame.  Rotate it into the device frame with the same mapping used by
     * the attitude display, then remove the vertical field component. */
    float gx = COMPASS_IMU_INVERT_X ? -_gravityX : _gravityX;
    float gy = COMPASS_IMU_INVERT_Y ? -_gravityY : _gravityY;
    float gz = COMPASS_IMU_INVERT_Z ? -_gravityZ : _gravityZ;
    float gnorm = sqrtf(gx * gx + gy * gy + gz * gz);
    if (!finiteFloat(gnorm) || gnorm < 0.5f) {
        _headingOk = false;
        return;
    }
    gx /= gnorm;
    gy /= gnorm;
    gz /= gnorm;

    /* Project the magnetic field onto the plane perpendicular to gravity.
     * The result is the magnetic-north direction in that horizontal plane. */
    float dot = dx * gx + dy * gy + dz * gz;
    float mxh = dx - dot * gx;
    float myh = dy - dot * gy;
    float mzh = dz - dot * gz;
    float mn = sqrtf(mxh * mxh + myh * myh + mzh * mzh);
    if (!finiteFloat(mn) || mn < 0.1f) {
        /* The field is almost parallel to gravity; no reliable heading. */
        _headingOk = false;
        return;
    }
    float nx = mxh / mn;
    float ny = myh / mn;
    float nz = mzh / mn;

    /* East = north x up.  With +Z up this is a right-handed horizontal basis. */
    float ex = ny * gz - nz * gy;
    float ey = nz * gx - nx * gz;
    float ez = nx * gy - ny * gx;
    float elen = sqrtf(ex * ex + ey * ey + ez * ez);
    if (!finiteFloat(elen) || elen < 0.5f) {
        _headingOk = false;
        return;
    }
    ex /= elen;
    ey /= elen;
    ez /= elen;

    /* Device +Y (screen top) projected onto the same horizontal plane.
     * e_y = (0,1,0), so y_h = e_y - (e_y . g) g. */
    float yhx = -gy * gx;
    float yhy = 1.0f - gy * gy;
    float yhz = -gy * gz;
    float yhLen = sqrtf(yhx * yhx + yhy * yhy + yhz * yhz);
    if (!finiteFloat(yhLen) || yhLen < 0.1f) {
        /* Screen top points almost straight up/down; yaw is undefined. */
        _headingOk = false;
        return;
    }

    float ye = yhx * ex + yhy * ey + yhz * ez;
    float yn = yhx * nx + yhy * ny + yhz * nz;

    /* 0 deg = screen top towards magnetic north, 90 deg = east. */
    float heading = atan2f(ye, yn) * RAD_TO_DEG_F + COMPASS_HEADING_OFFSET;
    heading = norm360(heading);

    float rad = heading * DEG_TO_RAD_F;
    if (!_filterInit) {
        _sinAcc = sinf(rad);
        _cosAcc = cosf(rad);
        _filterInit = true;
    } else {
        _sinAcc = _sinAcc * 0.80f + sinf(rad) * 0.20f;
        _cosAcc = _cosAcc * 0.80f + cosf(rad) * 0.20f;
    }
    _targetHeading = norm360(atan2f(_sinAcc, _cosAcc) * RAD_TO_DEG_F);
    _headingOk = true;
}

/* ---------- callbacks ---------- */
static void compassTapCb(lv_event_t* e)
{
    (void)e;
    setPage(PAGE_ATTITUDE);
}

static void openSettingsCb(lv_event_t* e)
{
    (void)e;
    setPage(PAGE_SETTINGS);
}

static void backToCompassCb(lv_event_t* e)
{
    (void)e;
    setPage(PAGE_COMPASS);
}

static void settingsCalCb(lv_event_t* e)
{
    (void)e;
    if (!_magOk) {
        _retryMagRequested = true;
        setLabel(_setMagLabel, "MAG: retrying...", C_WARN);
        return;
    }
    startCalibration();
}

static void settingsAttCb(lv_event_t* e)
{
    (void)e;
    setPage(PAGE_ATTITUDE);
}

static void settingsRetryCb(lv_event_t* e)
{
    (void)e;
    _retryMagRequested = true;
    setLabel(_setMagLabel, "MAG: retrying...", C_WARN);
}

static void clearMsgboxCb(lv_event_t* e)
{
    lv_obj_t* box = lv_event_get_current_target(e);
    const char* txt = lv_msgbox_get_active_btn_text(box);
    if (txt != nullptr && strcmp(txt, "CLEAR") == 0)
        _calClearRequested = true;
    lv_msgbox_close(box);
}

static void settingsClearCb(lv_event_t* e)
{
    (void)e;
    if (!_calibrated) {
        setLabel(_setCalLabel, "CAL: none to clear", C_DIM);
        return;
    }

    static const char* btns[] = {"CANCEL", "CLEAR", ""};
    lv_obj_t* box = lv_msgbox_create(_pageSettings, "CLEAR CALIBRATION",
                                     "Saved offsets and scales will be removed.",
                                     btns, true);
    lv_obj_add_event_cb(box, clearMsgboxCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_center(box);
}

static void calCancelCb(lv_event_t* e)
{
    (void)e;
    _calCancelRequested = true;
}

static void calFinishCb(lv_event_t* e)
{
    (void)e;
    _calFinishRequested = true;
}

/* ---------- page builders ---------- */
static void buildCompassPage()
{
    _pageCompass = makePage();

    _timeLabel = makeLabel(_pageCompass, "00:00", &lv_font_montserrat_24, C_TEXT,
                           80, 2, 120, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(_timeLabel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(_timeLabel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(_timeLabel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(_timeLabel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* gear = lv_btn_create(_pageCompass);
    lv_obj_remove_style_all(gear);
    /* 48 px wide, 10 px from the right edge, and a 4 px extended touch margin. */
    lv_obj_set_pos(gear, 222, 1);
    lv_obj_set_size(gear, 48, 36);
    lv_obj_set_style_radius(gear, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(gear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(gear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(gear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(gear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(gear, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(gear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(gear, 60, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(gear, col(C_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(gear, 4);
    lv_obj_clear_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gear, openSettingsCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gearIcon = lv_label_create(gear);
    lv_obj_remove_style_all(gearIcon);
    lv_label_set_text(gearIcon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gearIcon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(gearIcon, col(C_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(gearIcon);

    /* dial */
    _dial = lv_obj_create(_pageCompass);
    lv_obj_set_pos(_dial, 62, 36);
    lv_obj_set_size(_dial, 156, 156);
    lv_obj_set_style_radius(_dial, 78, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(_dial, col(C_PANEL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_dial, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(_dial, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(_dial, col(C_LINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(_dial, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_dial, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* meter = lv_meter_create(_dial);
    lv_obj_set_pos(meter, 3, 3);
    lv_obj_set_size(meter, 150, 150);
    lv_obj_set_style_bg_opa(meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);

    lv_meter_scale_t* minor = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, minor, 73, 1, 4, col(0x2C4358));
    lv_meter_set_scale_range(meter, minor, 0, 360, 360, 270);

    lv_meter_scale_t* major = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, major, 9, 2, 9, col(C_DIM));
    lv_meter_set_scale_range(meter, major, 0, 360, 360, 270);

    /* fixed top reference */
    makeLine(_dial, 72, 1, 12, 2, C_POINTER, true);
    makeLine(_dial, 77, 1, 2, 7, C_POINTER, true);

    /* needle */
    _needleLine = lv_line_create(_dial);
    lv_obj_set_pos(_needleLine, 0, 0);
    lv_obj_set_size(_needleLine, 156, 156);
    lv_line_set_points(_needleLine, _needlePts, 2);
    lv_obj_set_style_line_width(_needleLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(_needleLine, col(C_POINTER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(_needleLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_needleLine, LV_OBJ_FLAG_CLICKABLE);

    /* cardinal labels */
    lv_obj_t* n = makeLabel(_dial, "N", &lv_font_montserrat_18, C_POINTER, 0, 0, 0, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t* e = makeLabel(_dial, "E", &lv_font_montserrat_18, C_TEXT, 0, 0, 0, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(e, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_t* s = makeLabel(_dial, "S", &lv_font_montserrat_18, C_TEXT, 0, 0, 0, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t* w = makeLabel(_dial, "W", &lv_font_montserrat_18, C_TEXT, 0, 0, 0, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(w, LV_ALIGN_LEFT_MID, 8, 0);

    const char* diagName[4] = {"NE", "SE", "SW", "NW"};
    const lv_coord_t diagX[4] = {44, 44, -44, -44};
    const lv_coord_t diagY[4] = {-44, 44, 44, -44};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* d = makeLabel(_dial, diagName[i], &lv_font_montserrat_12, C_DIM, 0, 0, 0, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(d, LV_ALIGN_CENTER, diagX[i], diagY[i]);
    }

    /* centre heading disc */
    lv_obj_t* centre = lv_obj_create(_dial);
    lv_obj_set_size(centre, 58, 58);
    lv_obj_align(centre, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(centre, 29, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(centre, col(C_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(centre, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(centre, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(centre, col(C_LINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(centre, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(centre, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    _headingLabel = makeLabel(centre, "---", &lv_font_montserrat_24, C_TEXT,
                              0, 0, 46, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(_headingLabel, LV_ALIGN_CENTER, 0, -7);
    _cardinalLabel = makeLabel(centre, "--", &lv_font_montserrat_12, C_ACCENT,
                               0, 0, 58, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(_cardinalLabel, LV_ALIGN_CENTER, 0, 16);

    _degreeRing = lv_obj_create(centre);
    lv_obj_set_size(_degreeRing, 6, 6);
    lv_obj_set_style_radius(_degreeRing, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_degreeRing, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(_degreeRing, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(_degreeRing, col(C_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(_degreeRing, _headingLabel, LV_ALIGN_OUT_TOP_RIGHT, -2, 6);

    /* transparent click target for the attitude page */
    lv_obj_t* overlay = lv_obj_create(_dial);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 156, 156);
    lv_obj_set_style_bg_opa(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, compassTapCb, LV_EVENT_CLICKED, nullptr);

    /* MAG panel */
    lv_obj_t* magPanel = lv_obj_create(_pageCompass);
    lv_obj_set_pos(magPanel, 8, 40);
    lv_obj_set_size(magPanel, 50, 132);
    lv_obj_set_style_bg_color(magPanel, col(C_PANEL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(magPanel, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(magPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(magPanel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(magPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(magPanel, LV_OBJ_FLAG_SCROLLABLE);

    makeLabel(magPanel, "MAG", &lv_font_montserrat_12, C_ACCENT, 0, 2, 50, LV_TEXT_ALIGN_CENTER);
    const char* magNames[4] = {"X", "Y", "Z", "F"};
    lv_obj_t** magValues[4] = {&_xLabel, &_yLabel, &_zLabel, &_fLabel};
    for (int i = 0; i < 4; i++) {
        makeLabel(magPanel, magNames[i], dataFont(), C_DIM, 6, 20 + i * 24, 12, LV_TEXT_ALIGN_LEFT);
        *magValues[i] = makeLabel(magPanel, "--", dataFont(), C_TEXT,
                                  0, 20 + i * 24, 46, LV_TEXT_ALIGN_RIGHT);
    }
    makeLabel(magPanel, "uT", &lv_font_montserrat_12, C_DIM, 0, 112, 50, LV_TEXT_ALIGN_CENTER);

    /* IMU panel */
    lv_obj_t* imuPanel = lv_obj_create(_pageCompass);
    lv_obj_set_pos(imuPanel, 222, 40);
    lv_obj_set_size(imuPanel, 50, 132);
    lv_obj_set_style_bg_color(imuPanel, col(C_PANEL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(imuPanel, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(imuPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(imuPanel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(imuPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(imuPanel, LV_OBJ_FLAG_SCROLLABLE);

    makeLabel(imuPanel, "IMU", &lv_font_montserrat_12, C_ACCENT, 0, 2, 50, LV_TEXT_ALIGN_CENTER);
    makeLabel(imuPanel, "P", dataFont(), C_DIM, 6, 30, 12, LV_TEXT_ALIGN_LEFT);
    _pLabel = makeLabel(imuPanel, "--", dataFontLarge(), C_TEXT, 0, 27, 46, LV_TEXT_ALIGN_RIGHT);
    makeLabel(imuPanel, "R", dataFont(), C_DIM, 6, 58, 12, LV_TEXT_ALIGN_LEFT);
    _rLabel = makeLabel(imuPanel, "--", dataFontLarge(), C_TEXT, 0, 55, 46, LV_TEXT_ALIGN_RIGHT);
    makeLabel(imuPanel, "DEG", &lv_font_montserrat_12, C_DIM, 0, 88, 50, LV_TEXT_ALIGN_CENTER);
    _levelMain = makeLabel(imuPanel, "N/A", &lv_font_montserrat_12, C_WARN,
                           0, 108, 50, LV_TEXT_ALIGN_CENTER);

    _statusLabel = makeLabel(_pageCompass, "MAG INIT", dataFont(), C_WARN,
                             8, 194, 264, LV_TEXT_ALIGN_CENTER);

    /* field bar */
    lv_obj_t* fieldRow = lv_obj_create(_pageCompass);
    lv_obj_set_pos(fieldRow, 8, 208);
    lv_obj_set_size(fieldRow, 264, 26);
    lv_obj_set_style_bg_opa(fieldRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(fieldRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(fieldRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(fieldRow, LV_OBJ_FLAG_SCROLLABLE);

    makeLabel(fieldRow, "FIELD", &lv_font_montserrat_12, C_DIM, 0, 4, 40, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < 10; i++) {
        _fieldSeg[i] = makeRect(fieldRow, 44 + i * 13, 8, 11, 5, C_PANEL_ALT);
        lv_obj_set_style_radius(_fieldSeg[i], 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    _fieldValueLabel = makeLabel(fieldRow, "--", &lv_font_montserrat_14, C_ACCENT,
                                 184, 0, 80, LV_TEXT_ALIGN_RIGHT);
}

/* ---------- attitude scene drawing ----------
 * The scene is drawn directly into the display draw context.  No transformed
 * object and no transparent layer is involved, so it works with
 * LV_COLOR_SCREEN_TRANSP == 0 and does not need a screen-sized buffer.
 */
static const float ATT_PX_PER_DEG = 3.0f;

static void attLine(lv_draw_ctx_t* draw_ctx, uint32_t color, lv_coord_t width,
                    float x1, float y1, float x2, float y2)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = col(color);
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;

    lv_point_t p1;
    lv_point_t p2;
    p1.x = (lv_coord_t)lroundf(x1);
    p1.y = (lv_coord_t)lroundf(y1);
    p2.x = (lv_coord_t)lroundf(x2);
    p2.y = (lv_coord_t)lroundf(y2);
    lv_draw_line(draw_ctx, &dsc, &p1, &p2);
}

static void attitudeDrawCb(lv_event_t* e)
{
    lv_draw_ctx_t* dc = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    if (dc == nullptr || obj == nullptr)
        return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const float cx = (coords.x1 + coords.x2) * 0.5f;
    const float cy = (coords.y1 + coords.y2) * 0.5f;

    const float th = _attRollDisp * DEG_TO_RAD_F;
    const float st = sinf(th);
    const float ct = cosf(th);
    const float nx = -st;       /* screen direction pointing to the ground */
    const float ny = ct;
    const float dx = ct;        /* screen direction along the horizon */
    const float dy = st;

    const float pitchPx = _attPitchDisp * ATT_PX_PER_DEG;
    const float hx = cx + nx * pitchPx;
    const float hy = cy + ny * pitchPx;

    /* Ground half-plane.  The polygon extends far past the screen so no
     * triangular gap can appear at any roll angle. */
    const float ext = 420.0f;
    lv_point_t poly[4];
    poly[0].x = (lv_coord_t)lroundf(hx - dx * ext);
    poly[0].y = (lv_coord_t)lroundf(hy - dy * ext);
    poly[1].x = (lv_coord_t)lroundf(hx + dx * ext);
    poly[1].y = (lv_coord_t)lroundf(hy + dy * ext);
    poly[2].x = (lv_coord_t)lroundf(hx + dx * ext + nx * ext);
    poly[2].y = (lv_coord_t)lroundf(hy + dy * ext + ny * ext);
    poly[3].x = (lv_coord_t)lroundf(hx - dx * ext + nx * ext);
    poly[3].y = (lv_coord_t)lroundf(hy - dy * ext + ny * ext);

    lv_draw_rect_dsc_t ground_dsc;
    lv_draw_rect_dsc_init(&ground_dsc);
    ground_dsc.bg_color = col(C_GROUND);
    ground_dsc.bg_opa = LV_OPA_COVER;
    ground_dsc.radius = 0;
    lv_draw_polygon(dc, &ground_dsc, poly, 4);

    /* Pitch ladder, parallel to the horizon. */
    static const int8_t ladder[] = {-30, -20, -10, 10, 20, 30};
    for (uint8_t i = 0; i < sizeof(ladder) / sizeof(ladder[0]); i++) {
        const int8_t a = ladder[i];
        const float off = pitchPx - (float)a * ATT_PX_PER_DEG;
        const float lx = cx + nx * off;
        const float ly = cy + ny * off;
        if (ly < coords.y1 - 70 || ly > coords.y2 + 70)
            continue;

        float half;
        if (a == 10 || a == -10)
            half = 48.0f;
        else if (a == 20 || a == -20)
            half = 36.0f;
        else
            half = 26.0f;

        const float ax = lx - dx * half;
        const float ay = ly - dy * half;
        const float bx = lx + dx * half;
        const float by = ly + dy * half;
        const float bar = (a > 0) ? 6.0f : -6.0f;

        attLine(dc, C_LADDER, 1, ax, ay, bx, by);
        attLine(dc, C_LADDER, 1, ax, ay, ax + nx * bar, ay + ny * bar);
        attLine(dc, C_LADDER, 1, bx, by, bx + nx * bar, by + ny * bar);

        /* Axis-aligned labels stay crisp without a rotated text layer. */
        const float tx = lx + dx * (half + 16.0f);
        const float ty = ly + dy * (half + 16.0f);
        if (tx > coords.x1 + 26 && tx < coords.x2 - 26 &&
            ty > coords.y1 + 30 && ty < coords.y2 - 26) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", (int)a);
            lv_draw_label_dsc_t text_dsc;
            lv_draw_label_dsc_init(&text_dsc);
            text_dsc.font = &lv_font_montserrat_12;
            text_dsc.color = col(C_LADDER);
            text_dsc.opa = LV_OPA_COVER;
            text_dsc.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t ta;
            ta.x1 = (lv_coord_t)lroundf(tx) - 18;
            ta.x2 = (lv_coord_t)lroundf(tx) + 18;
            ta.y1 = (lv_coord_t)lroundf(ty) - 7;
            ta.y2 = (lv_coord_t)lroundf(ty) + 8;
            lv_draw_label(dc, &text_dsc, &ta, buf, nullptr);
        }
    }

    /* Horizon line, drawn on top of the ladder. */
    attLine(dc, C_HORIZON, 2,
            hx - dx * ext, hy - dy * ext,
            hx + dx * ext, hy + dy * ext);

    /* Fixed roll reference at the top of the screen. */
    const float rollY = coords.y1 + 30.0f;
    attLine(dc, C_POINTER, 2, cx - 7.0f, rollY, cx, rollY + 9.0f);
    attLine(dc, C_POINTER, 2, cx + 7.0f, rollY, cx, rollY + 9.0f);

    /* Fixed aircraft reference symbol. */
    attLine(dc, C_POINTER, 2, cx - 46.0f, cy, cx - 16.0f, cy);
    attLine(dc, C_POINTER, 2, cx + 16.0f, cy, cx + 46.0f, cy);
    attLine(dc, C_POINTER, 2, cx - 16.0f, cy, cx - 16.0f, cy + 6.0f);
    attLine(dc, C_POINTER, 2, cx + 16.0f, cy, cx + 16.0f, cy + 6.0f);

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = col(C_POINTER);
    dot_dsc.bg_opa = LV_OPA_COVER;
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t dot_area;
    dot_area.x1 = (lv_coord_t)cx - 3;
    dot_area.y1 = (lv_coord_t)cy - 3;
    dot_area.x2 = (lv_coord_t)cx + 3;
    dot_area.y2 = (lv_coord_t)cy + 3;
    lv_draw_rect(dc, &dot_dsc, &dot_area);
}

static lv_obj_t* makeSceneBlock(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, lv_coord_t h)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, col(C_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 205, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void buildAttitudePage()
{
    _pageAttitude = makePage();

    /* Full-screen scene: one opaque, non-transformed object.  The horizon is
     * painted geometrically in its draw callback, never by rotating a layer. */
    _attView = lv_obj_create(_pageAttitude);
    lv_obj_remove_style_all(_attView);
    lv_obj_set_pos(_attView, 0, 0);
    lv_obj_set_size(_attView, 280, 240);
    lv_obj_set_style_radius(_attView, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(_attView, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(_attView, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(_attView, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(_attView, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(_attView, col(C_SKY), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_attView, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_attView, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_attView, attitudeDrawCb, LV_EVENT_DRAW_MAIN, nullptr);

    /* Edge HUD chrome.  bg_opa < 255 blends directly, no layer is created. */
    makeSceneBlock(_pageAttitude, 0, 0, 280, 27);
    makeSceneBlock(_pageAttitude, 0, 212, 280, 28);

    makeLabel(_pageAttitude, "ATTITUDE", &lv_font_montserrat_18, C_TEXT,
              10, 4, 120, LV_TEXT_ALIGN_LEFT);
    makeButton(_pageAttitude, "BACK", 218, 2, 52, 24, false, backToCompassCb);
    _levelLabel = makeLabel(_pageAttitude, "N/A", &lv_font_montserrat_12, C_WARN,
                            105, 6, 70, LV_TEXT_ALIGN_CENTER);

    _attPitchLabel = makeLabel(_pageAttitude, "P --", &lv_font_montserrat_18, C_TEXT,
                               10, 215, 110, LV_TEXT_ALIGN_LEFT);
    _attRollLabel = makeLabel(_pageAttitude, "R --", &lv_font_montserrat_18, C_TEXT,
                              160, 215, 110, LV_TEXT_ALIGN_RIGHT);
    makeLabel(_pageAttitude, "DEG", &lv_font_montserrat_12, C_DIM,
              120, 218, 40, LV_TEXT_ALIGN_CENTER);
}

static void buildSettingsPage()
{
    _pageSettings = makePage();

    makeLabel(_pageSettings, "SETTINGS", &lv_font_montserrat_18, C_TEXT,
              12, 8, 180, LV_TEXT_ALIGN_LEFT);
    makeButton(_pageSettings, "BACK", 216, 4, 52, 24, false, backToCompassCb);

    _setMagLabel = makeLabel(_pageSettings, "MAG: --", dataFont(), C_DIM,
                             12, 38, 256, LV_TEXT_ALIGN_LEFT);
    _setCalLabel = makeLabel(_pageSettings, "CAL: --", dataFont(), C_DIM,
                             12, 54, 256, LV_TEXT_ALIGN_LEFT);
    _setImuLabel = makeLabel(_pageSettings, "IMU: --", dataFont(), C_DIM,
                             12, 70, 256, LV_TEXT_ALIGN_LEFT);

    makeButton(_pageSettings, "CALIBRATE", 12, 90, 256, 30, true, settingsCalCb);
    makeButton(_pageSettings, "CLEAR CAL", 12, 126, 256, 30, false, settingsClearCb);
    makeButton(_pageSettings, "ATTITUDE / LEVEL", 12, 162, 256, 30, false, settingsAttCb);
    makeButton(_pageSettings, "RETRY MAG", 12, 198, 256, 30, false, settingsRetryCb);
}

static void buildCalibrationPage()
{
    _pageCal = makePage();

    makeLabel(_pageCal, "CALIBRATE", &lv_font_montserrat_18, C_TEXT,
              12, 8, 256, LV_TEXT_ALIGN_LEFT);

    lv_obj_t* info = makeLabel(_pageCal,
                               "Slowly rotate the device in all directions\n"
                               "(figure 8) to cover X, Y and Z. Keep it\n"
                               "away from magnets and metal.",
                               &lv_font_montserrat_12, C_DIM,
                               12, 34, 256, LV_TEXT_ALIGN_LEFT, true);
    lv_obj_set_style_text_line_space(info, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    _calBar = lv_bar_create(_pageCal);
    lv_obj_set_pos(_calBar, 12, 84);
    lv_obj_set_size(_calBar, 256, 8);
    lv_bar_set_range(_calBar, 0, 100);
    lv_bar_set_value(_calBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_calBar, col(C_PANEL_ALT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_calBar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(_calBar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(_calBar, col(C_ACCENT), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(_calBar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_calBar, LV_OBJ_FLAG_CLICKABLE);

    _calSampleLabel = makeLabel(_pageCal, "SAMPLES 0/100   COVERAGE 0/3", dataFont(), C_TEXT,
                                12, 100, 256, LV_TEXT_ALIGN_LEFT);
    _calRangeLabel = makeLabel(_pageCal, "RANGE X 0  Y 0  Z 0 uT", dataFont(), C_DIM,
                               12, 118, 256, LV_TEXT_ALIGN_LEFT);
    _calStatusLabel = makeLabel(_pageCal, "Rotate slowly...", &lv_font_montserrat_14, C_WARN,
                                12, 140, 256, LV_TEXT_ALIGN_LEFT, true);

    makeButton(_pageCal, "CANCEL", 12, 190, 120, 34, false, calCancelCb);
    makeButton(_pageCal, "FINISH", 148, 190, 120, 34, true, calFinishCb);
}

static void buildUi()
{
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_size(scr, 280, 240);
    lv_obj_set_style_bg_color(scr, col(C_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    buildCompassPage();
    buildAttitudePage();
    buildSettingsPage();
    buildCalibrationPage();
    setPage(PAGE_COMPASS);
}

/* ---------- rendering ---------- */
static void updateCompassUi(bool dataChanged)
{
    if (_headingOk) {
        if (!_displayInit) {
            _displayHeading = _targetHeading;
            _displayInit = true;
        } else {
            _displayHeading = norm360(_displayHeading + wrap180(_targetHeading - _displayHeading) * 0.30f);
        }

        float rad = _displayHeading * DEG_TO_RAD_F;
        _needlePts[0].x = 78;
        _needlePts[0].y = 78;
        _needlePts[1].x = (lv_coord_t)(78.0f + sinf(rad) * 56.0f);
        _needlePts[1].y = (lv_coord_t)(78.0f - cosf(rad) * 56.0f);
        lv_line_set_points(_needleLine, _needlePts, 2);
        lv_obj_clear_flag(_needleLine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_degreeRing, LV_OBJ_FLAG_HIDDEN);

        char buf[8];
        snprintf(buf, sizeof(buf), "%03.0f", (double)_displayHeading);
        setText(_headingLabel, buf);
        setText(_cardinalLabel, cardinal(_displayHeading));
        lv_obj_align_to(_degreeRing, _headingLabel, LV_ALIGN_OUT_TOP_RIGHT, -2, 6);
    } else {
        _displayInit = false;
        _filterInit = false;   /* reacquire cleanly when heading returns */
        lv_obj_add_flag(_needleLine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_degreeRing, LV_OBJ_FLAG_HIDDEN);
        setText(_headingLabel, "---");
        setText(_cardinalLabel, "--");
    }

    if (!dataChanged)
        return;

    char buf[24];
    if (_magOk) {
        snprintf(buf, sizeof(buf), "%+.0f", (double)_mx); setText(_xLabel, buf);
        snprintf(buf, sizeof(buf), "%+.0f", (double)_my); setText(_yLabel, buf);
        snprintf(buf, sizeof(buf), "%+.0f", (double)_mz); setText(_zLabel, buf);
        snprintf(buf, sizeof(buf), "%.1f", (double)_field); setText(_fLabel, buf);
        snprintf(buf, sizeof(buf), "%.1fuT", (double)_field); setText(_fieldValueLabel, buf);
    } else {
        setText(_xLabel, "--");
        setText(_yLabel, "--");
        setText(_zLabel, "--");
        setText(_fLabel, "--");
        setText(_fieldValueLabel, "--");
    }

    if (_imuOk) {
        snprintf(buf, sizeof(buf), "%+.0f", (double)_pitch); setText(_pLabel, buf);
        snprintf(buf, sizeof(buf), "%+.0f", (double)_roll); setText(_rLabel, buf);
        if (fabsf(_pitch) < 2.0f && fabsf(_roll) < 2.0f)
            setLabel(_levelMain, "LEVEL", C_OK);
        else
            setLabel(_levelMain, "TILT", C_DIM);
    } else {
        setText(_pLabel, "--");
        setText(_rLabel, "--");
        setLabel(_levelMain, "N/A", C_WARN);
    }

    if (_magReadFailed) {
        if (_magReadReason == (uint8_t)ChappieMAG::READ_OVERFLOW)
            setLabel(_statusLabel, "MAG SATURATED / BAD SAMPLE", C_DANGER);
        else
            setLabel(_statusLabel, "MAG READ ERROR", C_DANGER);
    } else if (!_magOk) {
        if (_magStatus == (uint8_t)ChappieMAG::STATUS_ABSENT)
            setLabel(_statusLabel, "MAG NOT FOUND", C_DANGER);
        else if (_magStatus == (uint8_t)ChappieMAG::STATUS_ERROR)
            setLabel(_statusLabel, "MAG INIT FAILED", C_DANGER);
        else
            setLabel(_statusLabel, "MAG INIT", C_WARN);
    } else if (!_imuOk) {
        setLabel(_statusLabel, "IMU NOT READY", C_WARN);
    } else if (!_fieldInRange) {
        setLabel(_statusLabel, "FIELD OUT OF RANGE", C_WARN);
    } else if (!_calibrated) {
        setLabel(_statusLabel, "UNCALIBRATED", C_WARN);
    } else {
        setLabel(_statusLabel, "MAG COMPASS READY", C_ACCENT);
    }

    int lit = 0;
    if (_magOk) {
        lit = (int)(_field / 100.0f * 10.0f + 0.5f);
        if (lit < 0) lit = 0;
        if (lit > 10) lit = 10;
    }
    uint32_t litColor = _fieldInRange ? C_ACCENT : C_POINTER;
    for (int i = 0; i < 10; i++)
        lv_obj_set_style_bg_color(_fieldSeg[i], col(i < lit ? litColor : C_PANEL_ALT),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void updateAttitudeUi(bool dataChanged)
{
    (void)dataChanged;

    if (_imuOk) {
        float pitch = _pitch * COMPASS_HORIZON_PITCH_SIGN;
        float roll = wrap180(_roll * COMPASS_HORIZON_ROLL_SIGN);
        if (pitch > 45.0f) pitch = 45.0f;
        if (pitch < -45.0f) pitch = -45.0f;

        if (!_attDispInit) {
            _attPitchDisp = pitch;
            _attRollDisp = roll;
            _attDispInit = true;
        } else {
            _attPitchDisp += (pitch - _attPitchDisp) * 0.35f;
            _attRollDisp += wrap180(roll - _attRollDisp) * 0.35f;
            if (_attPitchDisp > 45.0f) _attPitchDisp = 45.0f;
            if (_attPitchDisp < -45.0f) _attPitchDisp = -45.0f;
        }

    } else {
        _attDispInit = false;
        _attPitchDisp = 0.0f;
        _attRollDisp = 0.0f;
    }

    /* Labels only need a redraw when their printed value changes. */
    const int shownPitch = _imuOk ? (int)lroundf(_pitch) : 1000;
    const int shownRoll = _imuOk ? (int)lroundf(_roll) : 1000;
    if (!_attShownValid || shownPitch != _attShownPitch ||
        shownRoll != _attShownRoll || _imuOk != _attShownImuOk) {
        _attShownValid = true;
        _attShownPitch = shownPitch;
        _attShownRoll = shownRoll;
        _attShownImuOk = _imuOk;

        if (_imuOk) {
            char buf[16];
            snprintf(buf, sizeof(buf), "P %+.0f", (double)_pitch);
            setText(_attPitchLabel, buf);
            snprintf(buf, sizeof(buf), "R %+.0f", (double)_roll);
            setText(_attRollLabel, buf);

            if (fabsf(_pitch) < 2.0f && fabsf(_roll) < 2.0f)
                setLabel(_levelLabel, "LEVEL", C_OK);
            else
                setLabel(_levelLabel, "TILT", C_DIM);
        } else {
            setText(_attPitchLabel, "P --");
            setText(_attRollLabel, "R --");
            setLabel(_levelLabel, "IMU N/A", C_WARN);
        }
    }

    bool needDraw = !_attDrawnValid;
    if (fabsf(_attPitchDisp - _attDrawnPitch) > 0.08f ||
        fabsf(wrap180(_attRollDisp - _attDrawnRoll)) > 0.08f)
        needDraw = true;

    if (needDraw && _attView != nullptr) {
        _attDrawnPitch = _attPitchDisp;
        _attDrawnRoll = _attRollDisp;
        _attDrawnValid = true;
        lv_obj_invalidate(_attView);
    }
}

static void updateSettingsUi()
{
    if (_magReadFailed) {
        if (_magReadReason == (uint8_t)ChappieMAG::READ_OVERFLOW)
            setLabel(_setMagLabel, "MAG: SATURATED", C_DANGER);
        else
            setLabel(_setMagLabel, "MAG: READ ERROR", C_DANGER);
    } else if (_magOk) {
        setLabel(_setMagLabel, "MAG: OK", C_OK);
    } else if (_magStatus == (uint8_t)ChappieMAG::STATUS_ABSENT) {
        setLabel(_setMagLabel, "MAG: NOT FOUND", C_DANGER);
    } else if (_magStatus == (uint8_t)ChappieMAG::STATUS_ERROR) {
        setLabel(_setMagLabel, "MAG: INIT FAILED", C_DANGER);
    } else {
        setLabel(_setMagLabel, "MAG: INIT", C_WARN);
    }

    if (_calActive) {
        setLabel(_setCalLabel, "CAL: COLLECTING", C_WARN);
    } else if (_calibrated) {
        setLabel(_setCalLabel, "CAL: VALID", C_OK);
    } else {
        switch (_calMsg) {
            case CAL_MSG_CLEARED:   setLabel(_setCalLabel, "CAL: CLEARED", C_DIM); break;
            case CAL_MSG_CANCELLED: setLabel(_setCalLabel, "CAL: CANCELLED", C_DIM); break;
            case CAL_MSG_INSUFFICIENT: setLabel(_setCalLabel, "CAL: INCOMPLETE", C_WARN); break;
            case CAL_MSG_FAILED:    setLabel(_setCalLabel, "CAL: FAILED", C_DANGER); break;
            default:                setLabel(_setCalLabel, "CAL: NONE", C_DIM); break;
        }
    }

    setLabel(_setImuLabel, _imuOk ? "IMU: OK" : "IMU: N/A", _imuOk ? C_OK : C_WARN);
}

static void updateCalibrationUi()
{
    lv_bar_set_value(_calBar, _calProgress, LV_ANIM_OFF);

    char buf[64];
    snprintf(buf, sizeof(buf), "SAMPLES %u/%u   COVERAGE %u/3",
             (unsigned)_calSamples, (unsigned)CAL_MIN_SAMPLES, (unsigned)_calCoverage);
    setText(_calSampleLabel, buf);

    float sx = _calMaxX - _calMinX;
    float sy = _calMaxY - _calMinY;
    float sz = _calMaxZ - _calMinZ;
    if (sx < 0.0f) sx = 0.0f;
    if (sy < 0.0f) sy = 0.0f;
    if (sz < 0.0f) sz = 0.0f;
    snprintf(buf, sizeof(buf), "RANGE X %.0f  Y %.0f  Z %.0f uT",
             (double)sx, (double)sy, (double)sz);
    setText(_calRangeLabel, buf);

    const char* text = "Rotate slowly in all directions";
    uint32_t color = C_WARN;
    if (_magReadFailed) {
        text = (_magReadReason == (uint8_t)ChappieMAG::READ_OVERFLOW)
                   ? "MAG saturated / invalid sample"
                   : "MAG read error, check I2C";
        color = C_DANGER;
    } else if (!_magOk) {
        text = "MAG unavailable, check sensor";
        color = C_DANGER;
    } else {
        switch (_calMsg) {
            case CAL_MSG_INSUFFICIENT:
                text = "Coverage insufficient, keep rotating";
                color = C_WARN;
                break;
            case CAL_MSG_OK:
                text = "Calibration saved";
                color = C_OK;
                break;
            case CAL_MSG_CANCELLED:
                text = "Calibration cancelled";
                color = C_DIM;
                break;
            case CAL_MSG_CLEARED:
                text = "Calibration cleared";
                color = C_DIM;
                break;
            case CAL_MSG_FAILED:
                text = "Calibration failed";
                color = C_DANGER;
                break;
            default:
                break;
        }
    }
    setLabel(_calStatusLabel, text, color);
}

static void uiTimerCb(lv_timer_t* timer)
{
    (void)timer;

    if (_returnToSettingsRequested) {
        _returnToSettingsRequested = false;
        if (_page == PAGE_CALIBRATION)
            setPage(PAGE_SETTINGS);
    }

    static uint8_t lastHour = 0xFF, lastMinute = 0xFF;
    static bool lastRtcOk = false;
    if (lastRtcOk != _rtcOk || lastHour != _hour || lastMinute != _minute) {
        lastRtcOk = _rtcOk;
        lastHour = _hour;
        lastMinute = _minute;
        if (_rtcOk) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)_hour, (unsigned)_minute);
            setText(_timeLabel, buf);
        } else {
            setText(_timeLabel, "--:--");
        }
    }

    static uint32_t lastVersion = 0xFFFFFFFFU;
    bool dataChanged = (lastVersion != _sampleVersion);
    if (dataChanged)
        lastVersion = _sampleVersion;

    if (_page == PAGE_COMPASS)
        updateCompassUi(dataChanged);
    else if (_page == PAGE_ATTITUDE)
        updateAttitudeUi(dataChanged);
    else if (_page == PAGE_SETTINGS)
        updateSettingsUi();
    else if (_page == PAGE_CALIBRATION)
        updateCalibrationUi();
}

/* ---------- sensor sampling ---------- */
static void sampleRtc()
{
    I2C_BM8563_TimeTypeDef t = {};
    device->Rtc.getTime(&t);
    if (t.hours >= 0 && t.hours < 24 && t.minutes >= 0 && t.minutes < 60) {
        _hour = (uint8_t)t.hours;
        _minute = (uint8_t)t.minutes;
        _rtcOk = true;
    } else {
        _rtcOk = false;
    }
}

static void sampleSensors()
{
    /* IMU: use the DMP gravity vector, not the raw Euler axes.
     * The DMP's "pitch" is rotation about the sensor Y (left/right tilt) and
     * its "roll" is rotation about sensor X (top/bottom tilt).  Using those
     * directly made the horizon move vertically for a left/right tilt and
     * rotate for a top/bottom tilt.  Gravity lets us build the device-frame
     * angles once, with explicit signs, and also feeds the compass tilt
     * compensation. */
    if (device->Imu.isReady()) {
        /* yaw is intentionally ignored: the DMP yaw is relative and drifts;
         * it is never used as an absolute compass heading. */
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        float gx = 0.0f, gy = 0.0f, gz = 1.0f;
        uint8_t ret = device->Imu.getYawPitchRoll(yaw, pitch, roll, &gx, &gy, &gz);
        if (ret == 0) {
            _gravityX = gx;
            _gravityY = gy;
            _gravityZ = gz;

            /* MPU sensor -> device frame (180 deg about Z). */
            float dx = COMPASS_IMU_INVERT_X ? -gx : gx;
            float dy = COMPASS_IMU_INVERT_Y ? -gy : gy;
            float dz = COMPASS_IMU_INVERT_Z ? -gz : gz;

            /* Device frame: pitch + when the top edge is raised,
             * roll + when the left edge is raised (right bank). */
            _pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) * RAD_TO_DEG_F;
            _roll = -atan2f(dx, sqrtf(dy * dy + dz * dz)) * RAD_TO_DEG_F;

            _haveImuSample = true;
            _imuOk = true;
        } else if (ret == 2) {
            _imuOk = _haveImuSample;
        } else {
            _imuOk = false;
        }
    } else {
        _imuOk = false;
    }

    /* BMM150 */
    bool magReadOk = false;
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    if (device->Mag.isReady() && device->Mag.read(mx, my, mz))
        magReadOk = true;

    _magStatus = (uint8_t)device->Mag.status();
    _magReadReason = (uint8_t)device->Mag.lastReadStatus();

    if (magReadOk) {
        _magReadFailed = false;
        _magFailCount = 0;
        _magOk = true;
    } else {
        _magReadFailed = (device->Mag.status() == ChappieMAG::STATUS_READY);
        if (_magFailCount < 255)
            _magFailCount++;
        if (_magFailCount >= MAG_FAIL_LIMIT) {
            _magOk = false;
            _headingOk = false;
        }

#if COMPASS_MAG_DIAG_LOG
        if (_magReadFailed) {
            static uint32_t lastLogMs = 0;
            static uint8_t lastReason = 0xFF;
            uint32_t now = millis();
            if ((uint8_t)_magReadReason != lastReason || (now - lastLogMs) >= 2000) {
                lastReason = _magReadReason;
                lastLogMs = now;
                printf("[Compass] MAG read failed: %s | raw=(%d,%d,%d) rhall=%u "
                       "| consecutive=%u\n",
                       ChappieMAG::readStatusText((ChappieMAG::ReadStatus)_magReadReason),
                       (int)device->Mag.rawX(), (int)device->Mag.rawY(),
                       (int)device->Mag.rawZ(), (unsigned)device->Mag.rawHall(),
                       (unsigned)_magFailCount);
            }
        }
#endif
    }

    if (_calActive) {
        if (magReadOk) {
            /* Calibration is captured in the BMM150 native frame.  The
             * native -> device rotation is applied later, in updateHeading,
             * so the stored offsets/scales are never in a rotated frame. */
            if (mx < _calMinX) _calMinX = mx;
            if (mx > _calMaxX) _calMaxX = mx;
            if (my < _calMinY) _calMinY = my;
            if (my > _calMaxY) _calMaxY = my;
            if (mz < _calMinZ) _calMinZ = mz;
            if (mz > _calMaxZ) _calMaxZ = mz;
            if (_calSamples < 0xFFFFU)
                _calSamples++;
            updateCalibrationProgress();
        }
        _headingOk = false;
        _sampleVersion++;
        return;
    }

    if (magReadOk) {
        /* Pass native values; calibration and the axis rotation are both
         * applied inside updateHeading, in the correct order. */
        updateHeading(mx, my, mz);
    } else if (_magFailCount >= MAG_FAIL_LIMIT) {
        _headingOk = false;
    }

    _sampleVersion++;
}

/* ---------- app lifecycle ---------- */
namespace App {

    std::string App_Compass_appName()
    {
        return app_name;
    }

    void* App_Compass_appIcon()
    {
        return (void*)&ui_img_icon_compass_png;
    }

    void App_Compass_onCreate()
    {
        UI_LOG("[%s] onCreate\n", app_name.c_str());

        _page = PAGE_COMPASS;
        _lastSampleMs = 0;
        _lastRtcMs = 0;
        _magOk = false;
        _imuOk = false;
        _headingOk = false;
        _haveImuSample = false;
        _fieldInRange = false;
        _magReadFailed = false;
        _magReadReason = (uint8_t)ChappieMAG::READ_NOT_READY;
        _magFailCount = 0;
        _fieldBadCount = 0;
        _gravityX = 0.0f;
        _gravityY = 0.0f;
        _gravityZ = 1.0f;
        _pitch = 0.0f;
        _roll = 0.0f;
        _rtcOk = false;
        _filterInit = false;
        _displayInit = false;
        _displayHeading = 0.0f;
        _calActive = false;
        _calFinishRequested = false;
        _calCancelRequested = false;
        _calClearRequested = false;
        _returnToSettingsRequested = false;
        _calProgress = 0;
        _calSamples = 0;
        _calCoverage = 0;
        _retryMagRequested = true;
        _linePtNext = 0;
        _attPitchDisp = 0.0f;
        _attRollDisp = 0.0f;
        _attDrawnPitch = 0.0f;
        _attDrawnRoll = 0.0f;
        _attDispInit = false;
        _attDrawnValid = false;
        _attShownValid = false;
        _attShownImuOk = false;
        _attShownPitch = 1000;
        _attShownRoll = 1000;
#if COMPASS_MAG_DIAG_LOG
        _magDiagLastMs = 0;
        _magDiagLastRange = true;
        _magDiagFirst = true;
#endif

        loadCalibration();
        buildUi();

        _uiTimer = lv_timer_create(uiTimerCb, UI_PERIOD_MS, nullptr);
        updateSettingsUi();
        updateCompassUi(true);
    }

    void App_Compass_onLoop()
    {
        if (device == nullptr)
            return;

        uint32_t now = millis();

        if (_retryMagRequested) {
            _retryMagRequested = false;
            /* The BSP brings the BMM150 up once at boot (CHAPPIE::init ->
             * Mag.init). Only re-probe here when that failed or the sensor was
             * lost; the retry reuses the same instance, never a second one. */
            bool ok = device->Mag.isReady() || device->Mag.init();
            _magOk = ok;
            _magStatus = (uint8_t)device->Mag.status();
            _magReadFailed = false;
            _magFailCount = 0;
            if (!ok) {
                _headingOk = false;
                _filterInit = false;
                _displayInit = false;
            }
        }

        if (_calClearRequested) {
            _calClearRequested = false;
            clearCalibration();
        }
        if (_calCancelRequested) {
            _calCancelRequested = false;
            _calActive = false;
            _calMsg = CAL_MSG_CANCELLED;
            _returnToSettingsRequested = true;
        }
        if (_calFinishRequested) {
            _calFinishRequested = false;
            finishCalibration();
        }

        if (now - _lastRtcMs >= RTC_PERIOD_MS) {
            _lastRtcMs = now;
            sampleRtc();
        }

        if (now - _lastSampleMs < SAMPLE_PERIOD_MS)
            return;
        _lastSampleMs = now;
        sampleSensors();
    }

    void App_Compass_onDestroy()
    {
        UI_LOG("[%s] onDestroy\n", app_name.c_str());
        if (_uiTimer != nullptr) {
            lv_timer_del(_uiTimer);
            _uiTimer = nullptr;
        }

        _pageCompass = nullptr;
        _pageAttitude = nullptr;
        _pageSettings = nullptr;
        _pageCal = nullptr;
        _timeLabel = nullptr;
        _statusLabel = nullptr;
        _dial = nullptr;
        _needleLine = nullptr;
        _headingLabel = nullptr;
        _cardinalLabel = nullptr;
        _degreeRing = nullptr;
        _xLabel = nullptr;
        _yLabel = nullptr;
        _zLabel = nullptr;
        _fLabel = nullptr;
        _pLabel = nullptr;
        _rLabel = nullptr;
        _levelMain = nullptr;
        _fieldValueLabel = nullptr;
        for (int i = 0; i < 10; i++)
            _fieldSeg[i] = nullptr;
        _attView = nullptr;
        _attPitchLabel = nullptr;
        _attRollLabel = nullptr;
        _levelLabel = nullptr;
        _attDispInit = false;
        _attDrawnValid = false;
        _setMagLabel = nullptr;
        _setCalLabel = nullptr;
        _setImuLabel = nullptr;
        _calBar = nullptr;
        _calSampleLabel = nullptr;
        _calRangeLabel = nullptr;
        _calStatusLabel = nullptr;

        _calActive = false;
        _calFinishRequested = false;
        _calCancelRequested = false;
        _calClearRequested = false;
        _retryMagRequested = false;
        _returnToSettingsRequested = false;
        _page = PAGE_COMPASS;
    }

    void App_Compass_getBsp(void* bsp)
    {
        device = (CHAPPIE*)bsp;
    }

}

#endif
