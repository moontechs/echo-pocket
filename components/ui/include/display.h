#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ST7789 LCD over SPI, allocate the PSRAM framebuffer,
 *        and draw the boot test pattern.
 *
 * Must be called once at startup before any other display_* function.
 */
void display_init(void);

/**
 * @brief Fill the display with a solid 16-bit RGB565 color.
 *
 * @p color 16-bit RGB565 pixel value (e.g. 0x0000 = black, 0xFFFF = white).
 */
void display_clear(uint16_t color);

/**
 * @brief Draw a null-terminated ASCII string at pixel coordinates.
 *
 * Uses a built-in 8×16 monospace bitmap font. Characters outside the
 * printable ASCII range (0x20–0x7E) are rendered as spaces.
 *
 * @p x       Horizontal pixel position (0 = left).
 * @p y       Vertical pixel position (0 = top).
 * @p text    Null-terminated ASCII string.
 * @p color   16-bit RGB565 foreground color.
 */
void display_draw_text(int x, int y, const char *text, uint16_t color);

/**
 * @brief Fill a rectangle with a solid color.
 *
 * Pixels outside the display bounds are silently clipped.
 *
 * @p x, y   Top-left corner.
 * @p w, h   Width and height in pixels.
 * @p color  16-bit RGB565 fill color.
 */
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a 1-pixel outline rectangle.
 *
 * @p x, y   Top-left corner.
 * @p w, h   Width and height in pixels.
 * @p color  16-bit RGB565 outline color.
 */
void display_draw_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a filled circle (midpoint algorithm).
 *
 * @p cx, cy  Center point.
 * @p r       Radius in pixels.
 * @p color   16-bit RGB565 fill color.
 */
void display_fill_circle(int cx, int cy, int r, uint16_t color);

/**
 * @brief Draw a horizontal line.
 *
 * @p x, y   Starting point.
 * @p w      Width in pixels.
 * @p color  16-bit RGB565 color.
 */
void display_draw_hline(int x, int y, int w, uint16_t color);

#ifdef __cplusplus
}
#endif
