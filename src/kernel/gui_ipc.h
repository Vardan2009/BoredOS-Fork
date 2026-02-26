#ifndef GUI_IPC_H
#define GUI_IPC_H

#define GUI_CMD_WINDOW_CREATE 1
#define GUI_CMD_DRAW_RECT     2
#define GUI_CMD_DRAW_STRING   3
#define GUI_CMD_MARK_DIRTY    4
#define GUI_CMD_GET_EVENT     5
#define GUI_CMD_DRAW_ROUNDED_RECT_FILLED 6

#define GUI_EVENT_NONE        0
#define GUI_EVENT_PAINT       1
#define GUI_EVENT_CLICK       2
#define GUI_EVENT_RIGHT_CLICK 3
#define GUI_EVENT_CLOSE       4
#define GUI_EVENT_KEY         5
#define GUI_EVENT_MOUSE_DOWN  6
#define GUI_EVENT_MOUSE_UP    7
#define GUI_EVENT_MOUSE_MOVE  8

typedef struct {
    int type;
    int arg1; // For click: x
    int arg2; // For click: y
    int arg3; // For click: button state
} gui_event_t;

#endif
