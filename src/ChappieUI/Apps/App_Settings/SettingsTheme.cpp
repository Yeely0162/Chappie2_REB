#include "SettingsTheme.h"
#include "../../Resource/ResourcePool.h"

#include <string.h>

namespace STheme {

lv_font_t *fontSmall()
{
    lv_font_t *f = ResourcePool::GetFont("bahnschrift_13");
    return f ? f : (lv_font_t *)LV_FONT_DEFAULT;
}

lv_font_t *fontBold18()
{
    lv_font_t *f = ResourcePool::GetFont("UbuntuBold18");
    return f ? f : (lv_font_t *)LV_FONT_DEFAULT;
}

lv_font_t *fontBold16()
{
    lv_font_t *f = ResourcePool::GetFont("UbuntuBold16");
    return f ? f : (lv_font_t *)LV_FONT_DEFAULT;
}

lv_font_t *fontValue()
{
    lv_font_t *f = ResourcePool::GetFont("bahnschrift_17");
    return f ? f : (lv_font_t *)LV_FONT_DEFAULT;
}

lv_font_t *fontTitle()
{
    lv_font_t *f = ResourcePool::GetFont("UbuntuBold18");
    return f ? f : (lv_font_t *)LV_FONT_DEFAULT;
}

static void plain(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void styleScreen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, ST_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void back_click(lv_event_t *e)
{
    lv_event_cb_t cb = (lv_event_cb_t)lv_event_get_user_data(e);
    if (cb)
        cb(e);
}

lv_obj_t *header(lv_obj_t *parent, const char *title, lv_event_cb_t back, void *user_data)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, SCREEN_W, HEADER_H);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, ST_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 4, 0);
    lv_obj_set_style_pad_right(obj, 10, 0);
    lv_obj_set_style_pad_column(obj, 4, 0);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    if (back) {
        lv_obj_t *btn = lv_btn_create(obj);
        lv_obj_set_size(btn, 28, 28);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, back_click, LV_EVENT_CLICKED, (void *)back);
        lv_obj_t *ico = lv_label_create(btn);
        lv_label_set_text(ico, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(ico, ST_TEXT, 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_18, 0);
        lv_obj_center(ico);
    } else {
        lv_obj_t *spacer = lv_obj_create(obj);
        lv_obj_set_size(spacer, 8, 1);
        plain(spacer);
    }

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontTitle(), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);

    return obj;
}

lv_obj_t *page(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, SCREEN_W, SCREEN_H - SUBHEADER_H);
    lv_obj_set_pos(obj, 0, SUBHEADER_H);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_set_style_pad_row(obj, 6, 0);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_snap_y(obj, LV_SCROLL_SNAP_NONE);
    return obj;
}

ModuleHero moduleHero(lv_obj_t *parent, const void *icon, const char *title, int rowCount)
{
    ModuleHero hero;

    /* Optional title (24 px + 2 px gap) + rows at a 24 px pitch. */
    const bool has_title = (title != nullptr && title[0] != '\0');
    const lv_coord_t h = (lv_coord_t)((has_title ? 28 : 4) + rowCount * 24);
    /* Page body content width (280 - 2 * 8) minus the icon column. */
    const lv_coord_t detail_w = (SCREEN_W - 16) - 64;

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_PCT(100), h);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    hero.box = box;

    /* Left: the module's own icon. It is a normal image widget; the rail is
     * never used here. */
    if (icon) {
        lv_obj_t *img = lv_img_create(box);
        const lv_img_dsc_t *dsc = (const lv_img_dsc_t *)icon;
        lv_img_set_src(img, dsc);
        lv_img_set_zoom(img, 256);                  /* 56 px, drawn 1:1 */
        lv_obj_set_size(img,
                        (lv_coord_t)((int32_t)dsc->header.w * 330 / 256),
                        (lv_coord_t)((int32_t)dsc->header.h * 330 / 256));
        lv_obj_set_style_img_recolor(img, ST_TEXT, 0);
        lv_obj_set_style_img_recolor_opa(img, 255, 0);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(img, LV_ALIGN_LEFT_MID, 4, 0);
    }

    /* Right: title centred over the detail column, then the rows. */
    lv_obj_t *detail = lv_obj_create(box);
    lv_obj_set_pos(detail, 64, 0);
    lv_obj_set_size(detail, detail_w, h);
    lv_obj_set_style_bg_opa(detail, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detail, 0, 0);
    lv_obj_set_style_radius(detail, 0, 0);
    lv_obj_set_style_pad_all(detail, 0, 0);
    lv_obj_set_style_pad_row(detail, 2, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(detail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_CLICKABLE);

    if (has_title) {
        lv_obj_t *t = lv_label_create(detail);
        lv_obj_set_width(t, LV_PCT(100));
        lv_obj_set_height(t, 24);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(t, ST_TEXT, 0);
        lv_obj_set_style_text_font(t, fontTitle(), 0);
        lv_obj_set_style_text_letter_space(t, 1, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_label_set_text(t, title);
    }

    lv_obj_t *rows = lv_obj_create(detail);
    lv_obj_set_width(rows, LV_PCT(100));
    lv_obj_set_height(rows, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rows, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rows, 0, 0);
    lv_obj_set_style_radius(rows, 0, 0);
    lv_obj_set_style_pad_all(rows, 0, 0);
    lv_obj_set_style_pad_row(rows, 2, 0);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rows, LV_OBJ_FLAG_CLICKABLE);
    /* Layout inside detailRow needs the final column width; the flex layout
     * has not run yet at this point. */
    lv_obj_set_user_data(rows, (void *)(intptr_t)detail_w);
    hero.rows = rows;

    return hero;
}

lv_obj_t *detailRow(lv_obj_t *rows, const char *label, const char *value)
{
    lv_coord_t w = 0;
    if (rows)
        w = (lv_coord_t)(intptr_t)lv_obj_get_user_data(rows);
    if (w <= 0)
        w = (SCREEN_W - 16) - 64;

    lv_obj_t *row = lv_obj_create(rows);
    lv_obj_set_size(row, LV_PCT(100), 22);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lab = lv_label_create(row);
    lv_obj_set_pos(lab, 0, 1);
    lv_obj_set_size(lab, 58, 20);
    lv_obj_set_style_text_color(lab, ST_MUTED, 0);
    lv_obj_set_style_text_font(lab, fontSmall(), 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_label_set_text(lab, label ? label : "");

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_pos(val, 62, 0);
    lv_obj_set_size(val, w - 62, 22);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(val, ST_TEXT, 0);
    lv_obj_set_style_text_font(val, fontValue(), 0);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    setEllipsis(val, value ? value : "--", w - 64);

    return val;
}

lv_obj_t *section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, SCREEN_W - 16);
    lv_obj_set_style_text_color(label, ST_MUTED, 0);
    lv_obj_set_style_text_font(label, fontBold16(), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_set_style_pad_left(label, 4, 0);
    lv_obj_set_style_pad_top(label, 2, 0);
    return label;
}

void setEllipsis(lv_obj_t *label, const char *text, lv_coord_t maxW)
{
    if (!label)
        return;
    if (!text)
        text = "";

    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (!font)
        font = (const lv_font_t *)LV_FONT_DEFAULT;

    char buf[136];
    size_t len = strlen(text);
    if (len > 128)
        len = 128;                 /* keep room for the ellipsis */
    memcpy(buf, text, len);
    buf[len] = '\0';

    if (lv_txt_get_width(buf, len, font, 0, LV_TEXT_FLAG_NONE) > maxW) {
        static const char dots[] = "...";
        const lv_coord_t dots_w = lv_txt_get_width(dots, 3, font, 0, LV_TEXT_FLAG_NONE);

        while (len > 0) {
            /* Never cut in the middle of a UTF-8 sequence. */
            do {
                len--;
            } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            buf[len] = '\0';
            if (lv_txt_get_width(buf, len, font, 0, LV_TEXT_FLAG_NONE) + dots_w <= maxW) {
                strcat(buf, dots);
                break;
            }
        }
    }
    setText(label, buf);
}

RowRefs infoRow(lv_obj_t *parent, const char *label, const char *value)
{
    RowRefs refs;
    lv_obj_t *row = lv_obj_create(parent);
    refs.row = row;
    lv_obj_set_size(row, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 4, 0);
    lv_obj_set_style_pad_right(row, 4, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lab = lv_label_create(row);
    lv_label_set_text(lab, label);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(lab, 1);
    lv_obj_set_style_text_color(lab, ST_MUTED, 0);
    lv_obj_set_style_text_font(lab, fontSmall(), 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value ? value : "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(val, 140);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(val, ST_TEXT, 0);
    lv_obj_set_style_text_font(val, fontValue(), 0);
    refs.value = val;

    return refs;
}

static void switch_row_click(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    lv_obj_t *sw = (lv_obj_t *)lv_obj_get_user_data(row);
    if (!sw) return;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_event_send(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *switchRow(lv_obj_t *parent, const char *title, bool checked,
                    lv_event_cb_t cb, void *user_data, lv_obj_t **switchOut)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(row, ST_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, ST_PANEL_HI, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 9, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontValue(), 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 22);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x2A3038), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, ST_TEAL, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, ST_TEXT, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sw, 2, LV_PART_KNOB | LV_STATE_DEFAULT);
    if (checked)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (cb)
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);

    lv_obj_set_user_data(row, sw);
    lv_obj_add_event_cb(row, switch_row_click, LV_EVENT_CLICKED, NULL);

    if (switchOut)
        *switchOut = sw;
    return row;
}

lv_obj_t *sliderRow(lv_obj_t *parent, const char *title, int32_t min, int32_t max,
                    int32_t value, lv_event_cb_t cb, void *user_data,
                    lv_obj_t **sliderOut, lv_obj_t **valueOut)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_PCT(100), 56);
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
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(box);
    lv_obj_set_size(top, LV_PCT(100), 18);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(top);
    lv_label_set_text(label, title);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontValue(), 0);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)value);
    lv_obj_t *val = lv_label_create(top);
    lv_label_set_text(val, buf);
    lv_obj_set_style_text_color(val, ST_TEAL, 0);
    lv_obj_set_style_text_font(val, fontValue(), 0);

    lv_obj_t *slider = lv_slider_create(box);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 10);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A3038), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, ST_TEAL, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, ST_TEXT, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_width(slider, 16, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_height(slider, 16, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
    if (cb)
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, user_data);

    if (sliderOut) *sliderOut = slider;
    if (valueOut) *valueOut = val;
    return box;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_color_t bg,
                 lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 9, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontValue(), 0);
    lv_obj_center(label);

    if (cb)
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

static void collapse_click(lv_event_t *e)
{
    lv_obj_t *head = lv_event_get_target(e);
    lv_obj_t *body = (lv_obj_t *)lv_obj_get_user_data(head);
    if (!body) return;

    bool hidden = lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN);
    if (hidden)
        lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *chev = lv_obj_get_child(head, 1);
    if (chev)
        lv_label_set_text(chev, hidden ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
}

lv_obj_t *collapsible(lv_obj_t *parent, const char *title, bool open)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_row(wrap, 4, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(wrap);
    lv_obj_set_size(head, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(head, ST_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 9, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_left(head, 12, 0);
    lv_obj_set_style_pad_right(head, 10, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(head);
    lv_label_set_text(label, title);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, ST_TEXT, 0);
    lv_obj_set_style_text_font(label, fontValue(), 0);

    lv_obj_t *chev = lv_label_create(head);
    lv_label_set_text(chev, open ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(chev, ST_MUTED, 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_12, 0);

    lv_obj_t *body = lv_obj_create(wrap);
    lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    if (!open)
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_user_data(head, body);
    lv_obj_add_event_cb(head, collapse_click, LV_EVENT_CLICKED, NULL);

    return body;
}

void styleDropdown(lv_obj_t *dd)
{
    if (!dd) return;
    lv_obj_set_style_bg_color(dd, ST_BG_ALT, 0);
    lv_obj_set_style_text_color(dd, ST_TEXT, 0);
    lv_obj_set_style_text_font(dd, fontSmall(), 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x262C34), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 7, 0);
    lv_obj_set_style_pad_left(dd, 8, 0);

    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, ST_PANEL, 0);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(list, ST_TEXT, 0);
        lv_obj_set_style_text_font(list, fontSmall(), 0);
        lv_obj_set_style_border_color(list, lv_color_hex(0x2A3038), 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_radius(list, 8, 0);
        lv_obj_set_style_bg_color(list, ST_TEAL, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, ST_BG, LV_PART_SELECTED | LV_STATE_CHECKED);
    }
}

void setValue(lv_obj_t *label, const char *text)
{
    if (!label) return;
    setText(label, text);
}

void setText(lv_obj_t *label, const char *text)
{
    if (!label) return;
    const char *cur = lv_label_get_text(label);
    if (text == nullptr) text = "";
    if (cur && strcmp(cur, text) == 0)
        return;
    lv_label_set_text(label, text);
}

void setVisible(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible)
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void setSwitch(lv_obj_t *sw, bool on)
{
    if (!sw) return;
    if (on)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

void setDanger(lv_obj_t *label, bool danger)
{
    if (!label) return;
    lv_obj_set_style_text_color(label, danger ? ST_RED : ST_TEXT, 0);
}

lv_obj_t *modalPanel(lv_obj_t **overlayOut)
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, 170, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 268, 228);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x151A20), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    if (overlayOut) *overlayOut = overlay;
    return panel;
}

void closeModal(lv_obj_t *overlay)
{
    if (overlay)
        lv_obj_del(overlay);
}

} // namespace STheme
