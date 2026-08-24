#include "pt/pt_display.h"

#include <cstring>

#if __has_include("esp_lcd_panel_rgb.h")
#include "esp_lcd_panel_rgb.h"
#define PT_HAS_RGB_PANEL_RESTART 1
#endif

TAMC_GT911 pt_touchpanel(
    PT_I2C0_SDA_PIN,
    PT_I2C0_SCL_PIN,
    PT_GT911_IRQ_PIN,
    PT_GT911_RST_PIN,
    std::max(PT_LCD_H_RES, 0),
    std::max(PT_LCD_V_RES, 0));

#if defined(ESP_ARDUINO_VERSION_MAJOR)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
// Arduino 3.x: bounce_buffer_size_px = PT_LCD_RENDER_BOUNCE_LINES * H_RES.
// 20-line SRAM bounce stops horizontal shift from PSRAM+WiFi contention.
// Factory 14.8 MHz PCLK (12 MHz produced a white screen).
Arduino_ESP32RGBPanel pt_rgbpanel(
    PT_LCD_DE_PIN, PT_LCD_VSYNC_PIN, PT_LCD_HSYNC_PIN, PT_LCD_PCLK_PIN,
    PT_LCD_B3_PIN, PT_LCD_B4_PIN, PT_LCD_B5_PIN, PT_LCD_B6_PIN, PT_LCD_B7_PIN,
    PT_LCD_G2_PIN, PT_LCD_G3_PIN, PT_LCD_G4_PIN, PT_LCD_G5_PIN, PT_LCD_G6_PIN, PT_LCD_G7_PIN,
    PT_LCD_R3_PIN, PT_LCD_R4_PIN, PT_LCD_R5_PIN, PT_LCD_R6_PIN, PT_LCD_R7_PIN,
    0, PT_LCD_HSYNC_PULSE_WIDTH, PT_LCD_HSYNC_BACK_PORCH, PT_LCD_HSYNC_FRONT_PORCH,
    0, PT_LCD_VSYNC_PULSE_WIDTH, PT_LCD_VSYNC_BACK_PORCH, PT_LCD_VSYNC_FRONT_PORCH,
    1,
    PT_LCD_PCLK_HZ, false,
    0, 0, PT_LCD_RENDER_BOUNCE_LINES * PT_LCD_H_RES);
#else
// Arduino 2.x / GFX 1.5.0: constructor has no bounce_buffer_size_px (and IDF 4.x
// RGB panel driver has no bounce path). Use env paxxtouch-remote-arduino-3x for the fix.
Arduino_ESP32RGBPanel pt_rgbpanel(
    PT_LCD_DE_PIN, PT_LCD_VSYNC_PIN, PT_LCD_HSYNC_PIN, PT_LCD_PCLK_PIN,
    PT_LCD_B3_PIN, PT_LCD_B4_PIN, PT_LCD_B5_PIN, PT_LCD_B6_PIN, PT_LCD_B7_PIN,
    PT_LCD_G2_PIN, PT_LCD_G3_PIN, PT_LCD_G4_PIN, PT_LCD_G5_PIN, PT_LCD_G6_PIN, PT_LCD_G7_PIN,
    PT_LCD_R3_PIN, PT_LCD_R4_PIN, PT_LCD_R5_PIN, PT_LCD_R6_PIN, PT_LCD_R7_PIN,
    0, PT_LCD_HSYNC_PULSE_WIDTH, PT_LCD_HSYNC_BACK_PORCH, PT_LCD_HSYNC_FRONT_PORCH,
    0, PT_LCD_VSYNC_PULSE_WIDTH, PT_LCD_VSYNC_BACK_PORCH, PT_LCD_VSYNC_FRONT_PORCH,
    1,
    PT_LCD_PCLK_HZ, false);
#endif
#endif

Arduino_RGB_Display pt_gfx(PT_LCD_H_RES, PT_LCD_V_RES, &pt_rgbpanel, 0, true);

lv_color_t *pt_disp_draw_buf = nullptr;
lv_color_t *pt_disp_draw_buf2 = nullptr;

#if PT_HAS_RGB_PANEL_RESTART && defined(ESP_ARDUINO_VERSION_MAJOR)
// Arduino_ESP32RGBPanel keeps esp_lcd_panel_handle_t as its last private member.
static esp_lcd_panel_handle_t pt_rgb_panel_handle() {
    esp_lcd_panel_handle_t handle = nullptr;
    const char *bytes = reinterpret_cast<const char *>(&pt_rgbpanel);
    std::memcpy(&handle, bytes + sizeof(pt_rgbpanel) - sizeof(handle), sizeof(handle));
    return handle;
}
#endif

void pt_display_resync() {
#if PT_HAS_RGB_PANEL_RESTART && defined(ESP_ARDUINO_VERSION_MAJOR)
    // Espressif: DMA underrun permanently shifts the image until restart at next VSYNC.
    if (esp_lcd_panel_handle_t handle = pt_rgb_panel_handle()) {
        const esp_err_t err = esp_lcd_rgb_panel_restart(handle);
        if (err != ESP_OK) {
            Serial.printf("[Display] RGB DMA restart failed: %s\n", esp_err_to_name(err));
        }
    }
#endif
}

void pt_display_resync_and_redraw() {
    pt_display_resync();
    lv_obj_t *scr = lv_screen_active();
    if (scr) lv_obj_invalidate(scr);
}
