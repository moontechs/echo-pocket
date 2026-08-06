/** @file recordings_list.c
 * @brief Recordings list screen — paged list of /echo-pocket/rec/ WAV files.
 *
 * Scans the SD card's recording directory on entry and displays
 * filenames with cursor-based scrolling.  Browse only — no playback
 * in v1.0 (per AGENTS.md).
 *
 * Layout (240×240):
 *   Row   0–23  Title bar ("Recordings")
 *   Row  24–215 List items (scrolling if needed)
 *   Row 216–239 Help bar (button hints)
 *
 * Left  = back to menu
 * Right = move cursor down / next
 * Center = no-op (browse only)
 *
 * #include "esp_vfs_fat.h" is needed for dirent, but it's pulled in
 * transitively through sd_storage.h / the IDF build system.
 */

#include "list_screens.h"
#include "display.h"
#include "sd_storage.h"
#include "ui_colors.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ── Constants ───────────────────────────────────────────────────────── */

/** Maximum number of recording files we track in the list. */
#define MAX_RECORDING_FILES     200

/** Maximum WAV filename length (including path). */
#define REC_PATH_MAX            320  /* SD_REC_DIR + '/' + d_name[256] */

/* Screen layout (mirrors menu.c so the look is consistent). */
#define TITLE_BAR_H             24
#define HELP_BAR_H              24
#define HELP_BAR_Y              (240 - HELP_BAR_H)
#define LIST_TOP_Y              TITLE_BAR_H
#define LIST_BOTTOM_Y           HELP_BAR_Y
#define LIST_ITEM_H             20

/* ── Item type ───────────────────────────────────────────────────────── */

typedef struct {
    char filename[REC_PATH_MAX];    /**< Sanitised display name           */
    char full_path[REC_PATH_MAX];   /**< Full VFS path (reserved)         */
    bool has_duration;              /**< true if duration_ms is valid     */
    uint32_t duration_ms;           /**< Duration from WAV header or 0    */
} recording_item_t;

/* ── Static state ────────────────────────────────────────────────────── */

static recording_item_t  s_items[MAX_RECORDING_FILES];
static int               s_item_count = 0;
static list_pagination_t s_pagination;

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Draw the title bar. */
static void draw_title_bar(const char *title)
{
    display_fill_rect(0, 0, 240, TITLE_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, TITLE_BAR_H - 1, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, 4, title, UI_COLOR_TEXT);
}

/** Draw the help bar at the bottom. */
static void draw_help_bar(void)
{
    display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, HELP_BAR_Y, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, HELP_BAR_Y + 5, "Back", UI_COLOR_TEXT_DIM);

    /* Center: nothing (browse only) */
    display_draw_text(240 - 8 * 4 - 4, HELP_BAR_Y + 5, "Next", UI_COLOR_TEXT_DIM);

    /* Show item count */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d",
             s_pagination.cursor + 1, s_pagination.total_items);
    int cw = (int)strlen(buf) * 8;
    display_draw_text((240 - cw) / 2, HELP_BAR_Y + 5, buf, UI_COLOR_TEXT);
}

/** Draw a single list row. */
static void draw_row(int y, const char *label, bool selected)
{
    uint16_t bg_color = selected ? UI_COLOR_SELECT_BG : UI_COLOR_VOID;
    uint16_t fg_color = selected ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;

    display_fill_rect(0, y, 240, LIST_ITEM_H, bg_color);

    if (selected) {
        display_draw_text(4, y + 2, ">", UI_COLOR_ACCENT_AMBER);
        display_draw_text(16, y + 2, label, fg_color);
    } else {
        display_draw_text(8, y + 2, label, fg_color);
    }
}

/**
 * Extract just the filename stem (no path, no extension) for display.
 * e.g. "/sdcard/echo-pocket/rec/REC_2025..." → "REC_2025..."
 */
static void sanitise_display_name(const char *full_path, char *out,
                                  size_t out_size)
{
    /* Find last '/' */
    const char *name = strrchr(full_path, '/');
    if (name) {
        name++;  /* skip '/' */
    } else {
        name = full_path;
    }

    /* Strip .wav extension if present */
    size_t len = strlen(name);
    if (len > 4 && strcasecmp(name + len - 4, ".wav") == 0) {
        len -= 4;
    }

    if (len >= out_size) len = out_size - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

/** Scan /echo-pocket/rec/ for .wav files and populate s_items[]. */
static void scan_recordings(void)
{
    s_item_count = 0;

    DIR *dir = opendir(SD_REC_DIR);
    if (!dir) {
        /* Directory doesn't exist or SD not mounted — show empty list */
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_item_count < MAX_RECORDING_FILES) {
        /* Only .wav files (case-insensitive) */
        size_t len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".wav") != 0) continue;

        recording_item_t *item = &s_items[s_item_count];

        /* Build full path */
        snprintf(item->full_path, sizeof(item->full_path),
                 "%s/%s", SD_REC_DIR, entry->d_name);

        /* Display name: filename without extension */
        sanitise_display_name(item->full_path, item->filename,
                              sizeof(item->filename));

        /* Duration: v1.0 does not parse WAV headers for duration;
         * mark as unknown so future tasks can wire this. */
        item->has_duration = false;
        item->duration_ms = 0;

        s_item_count++;
    }

    closedir(dir);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void recordings_list_enter(void)
{
    /* Scan SD once when entering the screen. */
    scan_recordings();

    /* Re-initialise pagination state to match fresh scan. */
    int visible = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H;
    if (visible < 1) visible = 1;
    list_pagination_init(&s_pagination, s_item_count, visible);
}

void recordings_list_screen_draw(void)
{
    /* Draw only — no re-scan.  recordings_list_enter() must be
     * called before the first draw when entering the screen. */
    display_clear(UI_COLOR_VOID);
    draw_title_bar("Recordings");
    draw_help_bar();

    int scroll = list_pagination_scroll_offset(&s_pagination);
    int visible = s_pagination.page_size;

    for (int i = 0; i < visible; i++) {
        int item_idx = scroll + i;
        if (item_idx >= s_item_count) break;

        int y = LIST_TOP_Y + i * LIST_ITEM_H;
        draw_row(y, s_items[item_idx].filename,
                 item_idx == s_pagination.cursor);
    }

    if (s_item_count == 0) {
        display_draw_text(8, LIST_TOP_Y + 20, "No recordings found",
                          UI_COLOR_TEXT_DIM);
    }
}

void recordings_list_navigate(ButtonId button, bool *should_exit)
{
    if (!should_exit) return;
    *should_exit = false;

    switch (button) {
    case BUTTON_LEFT:
        *should_exit = true;
        break;

    case BUTTON_RIGHT:
        list_pagination_cursor_down(&s_pagination);
        break;

    case BUTTON_CENTER:
        /* Browse only — no action on CENTER */
        break;

    default:
        break;
    }
}
