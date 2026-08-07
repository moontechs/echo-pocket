#include "buttons.h"

void button_debounce_init(ButtonDebounce *db)
{
    db->history_idx = 0; db->debounced_pressed = false; db->event_pending = false;
    for (int i = 0; i < BUTTON_DEBOUNCE_SAMPLES; i++) db->history[i] = false;
}

bool button_debounce_feed(ButtonDebounce *db, bool raw_pressed, uint32_t now_ms)
{
    (void)now_ms;
    db->history[db->history_idx] = raw_pressed;
    db->history_idx = (db->history_idx + 1) % BUTTON_DEBOUNCE_SAMPLES;
    for (int i = 0; i < BUTTON_DEBOUNCE_SAMPLES; i++) if (db->history[i] != raw_pressed) { db->event_pending = false; return false; }
    if (raw_pressed && !db->debounced_pressed) { db->debounced_pressed = true; return false; }
    if (!raw_pressed && db->debounced_pressed) { db->debounced_pressed = false; return true; }
    db->event_pending = false; return false;
}
