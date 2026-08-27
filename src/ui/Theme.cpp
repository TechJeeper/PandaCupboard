#include "ui/Theme.h"

#include <string.h>

namespace {
struct Palette {
    const char *name;
    uint32_t bg;
    uint32_t surface;
    uint32_t header;
    uint32_t row;
    uint32_t rowAlt;
    uint32_t text;
    uint32_t muted;
    uint32_t primary;
    uint32_t accent;
    uint32_t warn;
    uint32_t danger;
    bool dark;
};

constexpr Palette kPalettes[] = {
    {"Night", 0x111827, 0x1F2937, 0x1F2937, 0x111827, 0x0B1220, 0xF9FAFB, 0x9CA3AF, 0x2563EB, 0x10B981, 0xF59E0B,
     0xEF4444, true},
    {"Dark Gray", 0x1C1C1C, 0x2C2C2C, 0x333333, 0x1C1C1C, 0x252525, 0xEEEEEE, 0x9E9E9E, 0x6B7280, 0xF97316, 0xF59E0B,
     0xEF4444, true},
    {"Ocean", 0x0B1620, 0x132433, 0x1A3348, 0x0B1620, 0x102030, 0xE2E8F0, 0x94A3B8, 0x0EA5E9, 0x22D3EE, 0xF59E0B,
     0xF43F5E, true},
    {"Forest", 0x101A14, 0x1B2B22, 0x234032, 0x101A14, 0x16241C, 0xECFDF3, 0x86A89A, 0x16A34A, 0x84CC16, 0xF59E0B,
     0xEF4444, true},
    {"Light", 0xF3F4F6, 0xFFFFFF, 0xE5E7EB, 0xFFFFFF, 0xF3F4F6, 0x111827, 0x6B7280, 0x2563EB, 0x059669, 0xD97706,
     0xDC2626, false},
};

UiTheme gTheme = UiTheme::Night;
UiTextSize gTextSize = UiTextSize::Medium;

const Palette &palette(UiTheme theme) {
    const int i = static_cast<int>(theme);
    if (i < 0 || i >= static_cast<int>(UiTheme::Count)) return kPalettes[0];
    return kPalettes[i];
}

const Palette &cur() { return palette(gTheme); }
}  // namespace

void PaxxTheme::set(UiTheme theme, UiTextSize textSize) {
    gTheme = theme;
    gTextSize = textSize;
}

void PaxxTheme::apply() {
    lv_theme_t *theme = lv_theme_default_init(
        lv_display_get_default(),
        primary(), text(), isDark(),
        fontBody());
    lv_display_set_theme(lv_display_get_default(), theme);

    lv_obj_t *scr = lv_scr_act();
    if (!scr) return;
    lv_obj_set_style_bg_color(scr, bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(scr, text(), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, fontBody(), LV_PART_MAIN);
}

UiTheme PaxxTheme::theme() { return gTheme; }
UiTextSize PaxxTheme::textSize() { return gTextSize; }
bool PaxxTheme::isDark() { return cur().dark; }
const char *PaxxTheme::themeName(UiTheme theme) { return palette(theme).name; }

lv_color_t PaxxTheme::primary() { return lv_color_hex(cur().primary); }
lv_color_t PaxxTheme::accent() { return lv_color_hex(cur().accent); }
lv_color_t PaxxTheme::warn() { return lv_color_hex(cur().warn); }
lv_color_t PaxxTheme::danger() { return lv_color_hex(cur().danger); }
lv_color_t PaxxTheme::bg(bool) { return lv_color_hex(cur().bg); }
lv_color_t PaxxTheme::surface(bool) { return lv_color_hex(cur().surface); }
lv_color_t PaxxTheme::header() { return lv_color_hex(cur().header); }
lv_color_t PaxxTheme::row() { return lv_color_hex(cur().row); }
lv_color_t PaxxTheme::rowAlt() { return lv_color_hex(cur().rowAlt); }
lv_color_t PaxxTheme::text(bool) { return lv_color_hex(cur().text); }
lv_color_t PaxxTheme::muted(bool) { return lv_color_hex(cur().muted); }
lv_color_t PaxxTheme::preview(UiTheme theme) { return lv_color_hex(palette(theme).bg); }
lv_color_t PaxxTheme::previewAccent(UiTheme theme) { return lv_color_hex(palette(theme).accent); }

const lv_font_t *PaxxTheme::fontBody() {
    if (gTextSize == UiTextSize::Small) return &lv_font_montserrat_14;
    if (gTextSize == UiTextSize::Large) return &lv_font_montserrat_18;
    return &lv_font_montserrat_16;
}

const lv_font_t *PaxxTheme::fontTitle() {
    if (gTextSize == UiTextSize::Small) return &lv_font_montserrat_16;
    if (gTextSize == UiTextSize::Large) return &lv_font_montserrat_20;
    return &lv_font_montserrat_18;
}

const lv_font_t *PaxxTheme::fontLarge() {
    if (gTextSize == UiTextSize::Small) return &lv_font_montserrat_18;
    return &lv_font_montserrat_20;
}

int PaxxTheme::rowHeight() {
    if (gTextSize == UiTextSize::Small) return 44;
    if (gTextSize == UiTextSize::Large) return 56;
    return 48;
}

void paxx_disable_input(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *paxx_create_nav_bar(lv_obj_t *parent, const char *title, lv_event_cb_t backCb, void *userData, bool /*dark*/,
                              lv_obj_t **outBackBtn) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, PaxxTheme::surface(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 8, LV_PART_MAIN);
    paxx_disable_input(bar);

    if (backCb) {
        lv_obj_t *back = lv_btn_create(bar);
        lv_obj_set_size(back, 72, 32);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(back, PaxxTheme::accent(), LV_PART_MAIN);
        paxx_mark_accent_fill(back);
        lv_obj_add_event_cb(back, backCb, LV_EVENT_CLICKED, userData);
        lv_obj_t *lbl = lv_label_create(back);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_center(lbl);
        if (outBackBtn) *outBackBtn = back;
    } else if (outBackBtn) {
        *outBackBtn = nullptr;
    }

    lv_obj_t *ttl = lv_label_create(bar);
    lv_label_set_text(ttl, title);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(ttl, PaxxTheme::fontTitle(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ttl, PaxxTheme::text(), LV_PART_MAIN);
    return bar;
}

lv_obj_t *paxx_create_nav_save_btn(lv_obj_t *nav, lv_event_cb_t cb, void *userData) {
    if (!nav) return nullptr;
    lv_obj_t *btn = lv_button_create(nav);
    lv_obj_set_size(btn, 40, 32);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 80, 0);
    lv_obj_set_style_bg_color(btn, PaxxTheme::accent(), LV_PART_MAIN);
    paxx_mark_accent_fill(btn);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    paxx_set_centered_icon(btn, LV_SYMBOL_SAVE);
    return btn;
}

lv_obj_t *paxx_create_status_chip(lv_obj_t *parent, const char *label, lv_color_t color) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chip, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chip, 4, LV_PART_MAIN);
    paxx_disable_input(chip);
    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    return chip;
}

void paxx_spinner_anim(void *obj, int32_t v) {
    lv_arc_set_end_angle(static_cast<lv_obj_t *>(obj), static_cast<int>(v));
}

static void paxx_spinner_start(lv_obj_t *arc) {
    if (!arc || lv_obj_get_user_data(arc)) return;

    lv_obj_set_user_data(arc, reinterpret_cast<void *>(1));
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, arc);
    lv_anim_set_exec_cb(&anim, paxx_spinner_anim);
    lv_anim_set_values(&anim, 30, 390);
    lv_anim_set_time(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

static void paxx_spinner_stop(lv_obj_t *arc) {
    if (!arc) return;
    lv_anim_delete(arc, paxx_spinner_anim);
    lv_obj_set_user_data(arc, nullptr);
}

lv_obj_t *paxx_create_loading_arc(lv_obj_t *parent) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 52, 52);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -24);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_FLOATING);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 90);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x4DA3FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    paxx_disable_input(arc);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

void paxx_set_loading_visible(lv_obj_t *arc, lv_obj_t *label, bool visible, const char *text) {
    if (arc) {
        const bool wasHidden = lv_obj_has_flag(arc, LV_OBJ_FLAG_HIDDEN);
        if (visible) {
            if (wasHidden) {
                lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
                paxx_spinner_start(arc);
                lv_obj_move_foreground(arc);
            }
        } else if (!wasHidden) {
            paxx_spinner_stop(arc);
            lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (label) {
        const bool showText = visible && text && text[0];
        const bool wasHidden = lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN);
        if (showText) {
            const char *cur = lv_label_get_text(label);
            if (!cur || strcmp(cur, text) != 0) {
                lv_label_set_text(label, text);
            }
            if (wasHidden) {
                lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(label);
            }
        } else if (!wasHidden || !visible) {
            if (lv_label_get_text(label)[0] != '\0') {
                lv_label_set_text(label, "");
            }
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_obj_t *paxx_set_centered_icon(lv_obj_t *btn, const char *symbol) {
    if (!btn) return nullptr;
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_layout(btn, LV_LAYOUT_NONE);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol ? symbol : "");
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
    return icon;
}

void paxx_mark_accent_fill(lv_obj_t *obj) {
    if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_1);
}

void paxx_mark_accent_text(lv_obj_t *obj) {
    if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_2);
}

void paxx_apply_accent_chrome(lv_obj_t *root) {
    if (!root) return;
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_USER_1)) {
        lv_obj_set_style_bg_color(root, PaxxTheme::accent(), LV_PART_MAIN);
        const uint32_t n = lv_obj_get_child_count(root);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t *ch = lv_obj_get_child(root, i);
            if (ch && !lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_2)) {
                lv_obj_set_style_text_color(ch, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }
        }
    }
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_USER_2)) {
        lv_obj_set_style_text_color(root, PaxxTheme::accent(), LV_PART_MAIN);
    }
    const uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; ++i) {
        paxx_apply_accent_chrome(lv_obj_get_child(root, i));
    }
}

void paxx_style_form_screen(lv_obj_t *screen) {
    if (!screen) return;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(screen, 0, LV_PART_MAIN);
}

void paxx_set_form_width(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_set_width(obj, kPaxxFormWidth);
}

void paxx_ui_refresh() {
    lv_timer_handler();
}
