#ifndef LIBUI_H
#define LIBUI_H

#include <stdint.h>
#include <stdbool.h>

// GUI Command IDs
#define GUI_CMD_WINDOW_CREATE 1
#define GUI_CMD_DRAW_RECT     2
#define GUI_CMD_DRAW_STRING   3
#define GUI_CMD_MARK_DIRTY    4
#define GUI_CMD_GET_EVENT     5
#define GUI_CMD_DRAW_ROUNDED_RECT_FILLED 6

// Event Types
#define GUI_EVENT_NONE        0
#define GUI_EVENT_PAINT       1
#define GUI_EVENT_CLICK       2
#define GUI_EVENT_RIGHT_CLICK 3
#define GUI_EVENT_CLOSE       4
#define GUI_EVENT_KEY         5

typedef struct {
    int type;
    int arg1; // For click: x
    int arg2; // For click: y
} gui_event_t;

// Window Handle
typedef uint64_t ui_window_t;

// libui API
ui_window_t ui_window_create(const char *title, int x, int y, int w, int h);
bool ui_get_event(ui_window_t win, gui_event_t *ev);

void ui_draw_rect(ui_window_t win, int x, int y, int w, int h, uint32_t color);
void ui_draw_rounded_rect_filled(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_string(ui_window_t win, int x, int y, const char *str, uint32_t color);
void ui_mark_dirty(ui_window_t win, int x, int y, int w, int h);

#endif
