#include "LEDS.h"

// ========== Effect Definitions ==========
enum LEDEffect : uint8_t {
    EFF_STATIC,   EFF_BREATHE,  EFF_RAINBOW,  EFF_TWINKLE,  EFF_STROBE,
    EFF_SMOOTH,   EFF_FIRE,     EFF_OCEAN,    EFF_CONFETTI, EFF_PULSE,
    EFF_SPARKLE,  EFF_METEOR,   EFF_GLITTER,  EFF_BEAT,     EFF_CANDLE,
    EFF_COUNT
};

static const char* EFF_NAMES[] = {
    "Static", "Breathe", "Rainbow", "Twinkle", "Strobe",
    "Smooth", "Fire", "Ocean", "Confetti", "Pulse",
    "Sparkle", "Meteor", "Glitter", "Beat", "Candle"
};

// Effect uses palette color? 1=yes, 0=auto
static const uint8_t EFF_USES_COLOR[] = {
    1, 1, 0, 1, 1,
    0, 0, 0, 0, 1,
    1, 0, 0, 1, 0
};

// ========== State ==========
static uint8_t _r = 255, _g = 255, _b = 255;
static LEDEffect _currEff = EFF_STATIC;
static uint8_t _effSpeed = 60;

// Animation state
static uint8_t _hue = 0;
static uint8_t _breath = 0;
static int8_t _breathDir = 1;
static uint8_t _fireHeat = 0;
static uint8_t _sparklePhase = 0;
static uint8_t _meteorPos = 0;
static uint32_t _lastTick = 0;

// ========== UI ==========
static lv_obj_t* _scr = nullptr;
static lv_obj_t* _colorWheel = nullptr;
static lv_obj_t* _infoLabel = nullptr;
static lv_obj_t* _effNameLabel = nullptr;
static lv_obj_t* _speedSlider = nullptr;
static lv_obj_t* _effList = nullptr;

// ========== Forward ==========
static void _applyColor(uint8_t r, uint8_t g, uint8_t b);
static void _runEffect();
static void _rebuildEffHighlight();

// ========== LED Control ==========
static void _applyColor(uint8_t r, uint8_t g, uint8_t b) {
    _r = r; _g = g; _b = b;
    CRGB c(r, g, b);
    FastLED.showColor(c, 10);
}

static void _setFromWheel() {
    lv_color_t lc = lv_colorwheel_get_rgb(_colorWheel);
    _applyColor(lc.ch.red, lc.ch.green, lc.ch.blue);
}

// ========== Effects (single LED) ==========
static void _effStatic() { _applyColor(_r, _g, _b); }

static void _effBreathe() {
    _breath += _breathDir * 4;
    if (_breath >= 240) { _breath = 240; _breathDir = -1; }
    if (_breath <= 15)  { _breath = 15;  _breathDir = 1;  }
    CRGB c((uint16_t)_r * _breath / 255, (uint16_t)_g * _breath / 255, (uint16_t)_b * _breath / 255);
    FastLED.showColor(c, 5);
}

static void _effRainbow() {
    _hue += 4;
    CRGB c; c.setHue(_hue);
    FastLED.showColor(c, 5);
}

static void _effTwinkle() {
    if (random8() < 96) {
        CRGB c((uint16_t)_r * random8() / 255, (uint16_t)_g * random8() / 255, (uint16_t)_b * random8() / 255);
        FastLED.showColor(c, 3);
    } else FastLED.showColor(CRGB::Black, 3);
}

static void _effStrobe() {
    static uint8_t cnt = 0;
    if ((++cnt) % 4 < 2) _applyColor(_r, _g, _b);
    else FastLED.showColor(CRGB::Black, 3);
}

static void _effSmooth() { _hue += 2; CRGB c; c.setHue(_hue); FastLED.showColor(c, 10); }

static void _effFire() {
    _fireHeat = qadd8(_fireHeat, random8(0, 64));
    if (_fireHeat > 240) _fireHeat -= 40;
    CRGB c(_fireHeat, _fireHeat / 2, _fireHeat / 4);
    FastLED.showColor(c, 5);
}

static void _effOcean() {
    static uint8_t ph = 0;
    ph += 3;
    uint8_t v = sin8(ph);
    FastLED.showColor(CRGB(0, v / 2, v), 5);
}

static void _effConfetti() {
    if (random8() < 96) { CRGB c; c.setHue(random8()); FastLED.showColor(c, 5); }
    else FastLED.showColor(CRGB::Black, 5);
}

static void _effPulse() {
    static uint8_t ph = 0;
    ph += 5;
    uint8_t v = sin8(ph);
    CRGB c((uint16_t)_r * v / 255, (uint16_t)_g * v / 255, (uint16_t)_b * v / 255);
    FastLED.showColor(c, 5);
}

// New effects
static void _effSparkle() {
    _sparklePhase += 8;
    uint8_t v = sin8(_sparklePhase);
    CRGB c((uint16_t)_r * v / 255, (uint16_t)_g * v / 255, (uint16_t)_b * v / 255);
    FastLED.showColor(c, 5);
}

static void _effMeteor() {
    _meteorPos += 2;
    if (_meteorPos > 255) _meteorPos = 0;
    CRGB c; c.setHue(_meteorPos);
    FastLED.showColor(c, 5);
}

static void _effGlitter() {
    static uint8_t ph = 0;
    ph += 1;
    uint8_t v = (random8() < (sin8(ph*2)/8+16)) ? 255 : 0;
    CRGB c(v, v, v);
    FastLED.showColor(c, 5);
}

static void _effBeat() {
    static uint8_t ph = 0;
    ph += 3;
    // Sharp attack, slow decay
    uint8_t v = (ph < 30) ? 255 : (255 - (ph - 30) * 2);
    if (ph > 158) ph = 0;
    CRGB c((uint16_t)_r * v / 255, (uint16_t)_g * v / 255, (uint16_t)_b * v / 255);
    FastLED.showColor(c, 5);
}

static void _effCandle() {
    // Random flicker around warm base
    uint8_t flicker = random8(180, 255);
    CRGB c(flicker, flicker * 3 / 5, flicker / 4);
    FastLED.showColor(c, 5);
}

typedef void (*EffFn)();
static const EffFn EFF_FNS[EFF_COUNT] = {
    _effStatic, _effBreathe, _effRainbow, _effTwinkle, _effStrobe,
    _effSmooth, _effFire, _effOcean, _effConfetti, _effPulse,
    _effSparkle, _effMeteor, _effGlitter, _effBeat, _effCandle
};

static void _runEffect() {
    uint32_t now = millis();
    uint32_t interval = map(_effSpeed, 0, 100, 100, 10);
    if (now - _lastTick < interval) return;
    _lastTick = now;
    if (_currEff < EFF_COUNT && EFF_FNS[_currEff]) EFF_FNS[_currEff]();
}

// ========== Event Handlers ==========
static void _wheelCb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        _setFromWheel();
        lv_label_set_text_fmt(_infoLabel, "R:%d G:%d B:%d", _r, _g, _b);
    }
}

static void _effCb(lv_event_t* e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < EFF_COUNT) {
        _currEff = (LEDEffect)idx;
        lv_label_set_text(_effNameLabel, EFF_NAMES[idx]);
        _rebuildEffHighlight();
    }
}

static void _speedCb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
        _effSpeed = lv_slider_get_value(_speedSlider);
}

// ========== UI Creation ==========
void LEDSPage(void) {
    _scr = lv_scr_act();
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(_scr, 280);
    lv_obj_set_height(_scr, 240);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(0x2D3B54), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_scr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Left: Color Wheel
    _colorWheel = lv_colorwheel_create(_scr, true);
    lv_obj_set_size(_colorWheel, 120, 120);
    lv_obj_set_pos(_colorWheel, 5, 5);
    lv_obj_add_event_cb(_colorWheel, _wheelCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Right: Info
    _infoLabel = lv_label_create(_scr);
    lv_obj_set_pos(_infoLabel, 133, 8);
    lv_obj_set_style_text_color(_infoLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_infoLabel, &lv_font_montserrat_18, 0);
    lv_label_set_text(_infoLabel, "R:255 G:255 B:255");

    // Effect name
    _effNameLabel = lv_label_create(_scr);
    lv_obj_set_pos(_effNameLabel, 133, 32);
    lv_obj_set_style_text_color(_effNameLabel, lv_color_hex(0x88CCFF), 0);
    lv_obj_set_style_text_font(_effNameLabel, &lv_font_montserrat_18, 0);
    lv_label_set_text(_effNameLabel, "Static");

    // Speed
    lv_obj_t* spL = lv_label_create(_scr);
    lv_obj_set_pos(spL, 133, 56);
    lv_obj_set_style_text_color(spL, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(spL, &lv_font_montserrat_18, 0);
    lv_label_set_text(spL, "Speed");

    _speedSlider = lv_slider_create(_scr);
    lv_obj_set_size(_speedSlider, 130, 14);
    lv_obj_set_pos(_speedSlider, 133, 78);
    lv_slider_set_range(_speedSlider, 0, 100);
    lv_slider_set_value(_speedSlider, _effSpeed, LV_ANIM_OFF);
    lv_obj_add_event_cb(_speedSlider, _speedCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Bottom: Scrollable Effect List (15 buttons x 1 column)
    _effList = lv_obj_create(_scr);
    lv_obj_set_pos(_effList, 5, 130);
    lv_obj_set_size(_effList, 270, 105);
    lv_obj_set_style_bg_color(_effList, lv_color_hex(0x1E2B44), 0);
    lv_obj_set_style_border_width(_effList, 0, 0);
    lv_obj_set_style_radius(_effList, 6, 0);
    lv_obj_set_scrollbar_mode(_effList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(_effList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_effList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(_effList, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < EFF_COUNT; i++) {
        // Container row
        lv_obj_t* row = lv_obj_create(_effList);
        lv_obj_set_size(row, 180, 60);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A3B55), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 7, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_align(row, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(row, _effCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        // Indicator dot (color or "A" for auto)
        lv_obj_t* dot = lv_label_create(row);
        lv_obj_set_pos(dot, 8, 3);
        lv_obj_set_style_text_font(dot, &lv_font_montserrat_18, 0);
        if (EFF_USES_COLOR[i]) {
            lv_obj_set_style_text_color(dot, lv_color_hex(0x53DE77), 0);
            lv_label_set_text(dot, LV_SYMBOL_OK);
        } else {
            lv_obj_set_style_text_color(dot, lv_color_hex(0xFFCC44), 0);
            lv_label_set_text(dot, LV_SYMBOL_REFRESH);
        }

        // Effect name
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_pos(lbl, 32, 3);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_label_set_text(lbl, EFF_NAMES[i]);

        // Active indicator bar
        lv_obj_t* bar = lv_obj_create(row);
        lv_obj_set_size(bar, 4, 22);
        lv_obj_set_pos(bar, 247, 2);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3355AA), 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_opa(bar, (i == _currEff) ? 255 : 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Initial state
    lv_event_send(_colorWheel, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void _rebuildEffHighlight() {
    // Update active indicator bars for all effect rows
    int childCount = lv_obj_get_child_cnt(_effList);
    for (int i = 0; i < childCount; i++) {
        lv_obj_t* row = lv_obj_get_child(_effList, i);
        if (!row) continue;
        // Last child of each row is the indicator bar
        int rowChildCount = lv_obj_get_child_cnt(row);
        if (rowChildCount > 0) {
            lv_obj_t* bar = lv_obj_get_child(row, rowChildCount - 1);
            lv_obj_set_style_bg_opa(bar, (i == _currEff) ? 255 : 0, 0);
        }
    }
}

void LEDS_Loop() { _runEffect(); }

void LEDS_DeInit() { FastLED.showColor(CRGB::Black); }
