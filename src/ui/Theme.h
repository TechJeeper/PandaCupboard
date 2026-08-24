#pragma once

#include <lvgl.h>
#include <cstdint>

enum class UiTheme : uint8_t {
    Night = 0,
    DarkGray,
    Ocean,
    Forest,
    Light,
    Count
};

enum class UiTextSize : uint8_t {
    Small = 0,
    Medium,
    Large
};

namespace PaxxTheme {
    void set(UiTheme theme, UiTextSize textSize);
    void apply();
    UiTheme theme();
    UiTextSize textSize();
    bool isDark();
    const char *themeName(UiTheme theme);

    lv_color_t primary();
    lv_color_t accent();
    lv_color_t warn();
    lv_color_t danger();
    lv_color_t bg(bool dark = true);
    lv_color_t surface(bool dark = true);
    lv_color_t header();
    lv_color_t row();
    lv_color_t rowAlt();
    lv_color_t text(bool dark = true);
    lv_color_t muted(bool dark = true);
    lv_color_t preview(UiTheme theme);
    lv_color_t previewAccent(UiTheme theme);

    const lv_font_t *fontBody();
    const lv_font_t *fontTitle();
    const lv_font_t *fontLarge();
    int rowHeight();
}

lv_obj_t *paxx_create_nav_bar(lv_obj_t *parent, const char *title, lv_event_cb_t backCb, void *userData,
                              bool dark = true, lv_obj_t **outBackBtn = nullptr);
lv_obj_t *paxx_create_status_chip(lv_obj_t *parent, const char *label, lv_color_t color);
void paxx_disable_input(lv_obj_t *obj);
void paxx_spinner_anim(void *obj, int32_t v);
lv_obj_t *paxx_create_loading_arc(lv_obj_t *parent);
void paxx_set_loading_visible(lv_obj_t *arc, lv_obj_t *label, bool visible, const char *text = nullptr);
void paxx_style_form_screen(lv_obj_t *screen);
void paxx_set_form_width(lv_obj_t *obj);

constexpr int kPaxxFormWidth = 736;

/** Pump LVGL once so the display updates (call after showing UI, before blocking work). */
void paxx_ui_refresh();
