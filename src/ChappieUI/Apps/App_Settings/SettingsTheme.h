/**
 * @file SettingsTheme.h
 * @brief Visual language for the settings centre.
 *
 * Layout mirrors the LILYGO factory reference: near-black background, a white
 * line icon on the left, a thin two-colour (orange/teal) vertical bar, then a
 * muted field label and a bright value. No brand strip is drawn anywhere.
 */
#pragma once
#include "lvgl.h"

namespace STheme {

#define ST_BG        lv_color_hex(0x0A0C0F)
#define ST_BG_ALT    lv_color_hex(0x0D1013)
#define ST_PANEL     lv_color_hex(0x121519)
#define ST_PANEL_HI  lv_color_hex(0x181D23)
#define ST_LINE      lv_color_hex(0x1E2329)
#define ST_TEXT      lv_color_hex(0xF2F4F7)
#define ST_MUTED     lv_color_hex(0x8A919B)
#define ST_ORANGE    lv_color_hex(0xFFB02E)
#define ST_TEAL      lv_color_hex(0x18C7A8)
#define ST_BLUE      lv_color_hex(0x3B82F6)
#define ST_RED       lv_color_hex(0xE5484D)
#define ST_DANGER    lv_color_hex(0xC0392B)

constexpr lv_coord_t SCREEN_W = 280;
constexpr lv_coord_t SCREEN_H = 240;
constexpr lv_coord_t HEADER_H = 30;
constexpr lv_coord_t SUBHEADER_H = 30;   /* sub-page title bar */
constexpr lv_coord_t PAGE_H   = 210;

struct RowRefs {
    lv_obj_t *row = nullptr;
    lv_obj_t *value = nullptr;
};

/**
 * @brief Module header used at the top of every sub-page: the module's icon
 *        on the left and a right-hand detail column whose title is centred
 *        over the values, followed by compact label/value rows.
 */
struct ModuleHero {
    lv_obj_t *box = nullptr;
    lv_obj_t *rows = nullptr;
};

/* Fonts from the project resource pool. */
lv_font_t *fontSmall();
lv_font_t *fontSmallBold();
lv_font_t *fontValue();
lv_font_t *fontTitle();

void styleScreen(lv_obj_t *scr);

/**
 * @brief Header bar. `back` may be null for the root page.
 */
lv_obj_t *header(lv_obj_t *parent, const char *title, lv_event_cb_t back, void *user_data);

/**
 * @brief Scrollable page body that starts below the sub-page bar.
 */
lv_obj_t *page(lv_obj_t *parent);

/**
 * @brief Build the module header (module icon + detail rows). `title` is
 *        optional: sub-pages show the page name in the top title bar instead,
 *        so they pass nullptr and the rows move up.
 *        `rowCount` reserves the exact height needed so the body below never
 *        overlaps.
 */
ModuleHero moduleHero(lv_obj_t *parent, const void *icon, const char *title, int rowCount);

/**
 * @brief One label/value row inside a ModuleHero. Values are right aligned to
 *        the detail column and truncated with "..." when too long.
 *        Returns the value label for refresh.
 */
lv_obj_t *detailRow(lv_obj_t *rows, const char *label, const char *value);

/**
 * @brief Section caption (uppercase, muted).
 */
lv_obj_t *section(lv_obj_t *parent, const char *title);

/**
 * @brief Plain label/value row used on sub-pages.
 */
RowRefs infoRow(lv_obj_t *parent, const char *label, const char *value);

/**
 * @brief Switch row. The event callback runs on the switch itself.
 */
lv_obj_t *switchRow(lv_obj_t *parent, const char *title, bool checked,
                    lv_event_cb_t cb, void *user_data, lv_obj_t **switchOut = nullptr);

/**
 * @brief Slider row with a percentage readout above the slider.
 */
lv_obj_t *sliderRow(lv_obj_t *parent, const char *title, int32_t min, int32_t max,
                    int32_t value, lv_event_cb_t cb, void *user_data,
                    lv_obj_t **sliderOut = nullptr, lv_obj_t **valueOut = nullptr);

/**
 * @brief Full-width action button.
 */
lv_obj_t *button(lv_obj_t *parent, const char *text, lv_color_t bg,
                 lv_event_cb_t cb, void *user_data);

/**
 * @brief Collapsible section. Returns the content container; store it to add
 *        children. The header toggles visibility.
 */
lv_obj_t *collapsible(lv_obj_t *parent, const char *title, bool open);

/**
 * @brief Apply the dark theme to a dropdown and its option list.
 */
void styleDropdown(lv_obj_t *dd);

void setValue(lv_obj_t *label, const char *text);
void setText(lv_obj_t *label, const char *text);       /* only if changed */

/**
 * @brief Single-line text that is manually truncated with "..." when it does
 *        not fit into `maxW` pixels.
 *
 * LVGL's LV_LABEL_LONG_DOT only truncates reliably once a fixed width has been
 * applied; labels that get their width from a flex layout can still wrap and
 * spill out of their row (a long SSID such as "Chappie2-sasfl" was split at
 * the hyphen). Measuring with the real font and truncating here keeps every
 * name on one line regardless of hyphens, underscores or long words.
 */
void setEllipsis(lv_obj_t *label, const char *text, lv_coord_t maxW);
void setVisible(lv_obj_t *obj, bool visible);
void setSwitch(lv_obj_t *sw, bool on);
void setDanger(lv_obj_t *label, bool danger);

/**
 * @brief Simple modal panel on lv_layer_top() with a scrim. Returns the panel;
 *        the caller adds children. `closeCb` runs after the panel is deleted.
 */
lv_obj_t *modalPanel(lv_obj_t **overlayOut);

/**
 * @brief Delete a modal created by modalPanel().
 */
void closeModal(lv_obj_t *overlay);

} // namespace STheme
