/** @file ui_colors.h
 * @brief Shared RGB565 palette for all non-face UI chrome (bars, menus,
 *        lists, status text). Face themes keep their own colors.
 *
 * A cool slate base with three warm accents carrying meaning, not five
 * unrelated primaries:
 *   mint  = positive / connected / success
 *   amber = attention / pending / warning
 *   coral = critical / error / recording
 *
 * All text/accent colors hold >=4.5:1 contrast against both UI_COLOR_VOID
 * and UI_COLOR_INK.
 */
#pragma once

#include <stdint.h>

#define UI_COLOR_VOID          ((uint16_t)0x0000)  /* screen background */
#define UI_COLOR_INK           ((uint16_t)0x10A3)  /* bar/panel background */
#define UI_COLOR_TEXT          ((uint16_t)0xEF7C)  /* primary text (warm white) */
#define UI_COLOR_TEXT_DIM      ((uint16_t)0x9D15)  /* secondary text/labels */
#define UI_COLOR_HAIRLINE      ((uint16_t)0x2987)  /* separators/borders */
#define UI_COLOR_SELECT_BG     ((uint16_t)0x3A0A)  /* selected row background */

#define UI_COLOR_ACCENT_MINT   ((uint16_t)0x6E72)  /* connected / success */
#define UI_COLOR_ACCENT_AMBER  ((uint16_t)0xEE49)  /* pending / warning / cursor */
#define UI_COLOR_ACCENT_CORAL  ((uint16_t)0xEA6B)  /* critical / error / REC */
