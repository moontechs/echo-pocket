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

#ifdef __cplusplus
}
#endif
