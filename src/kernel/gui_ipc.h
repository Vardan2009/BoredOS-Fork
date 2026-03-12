// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef GUI_IPC_H
#define GUI_IPC_H

#define GUI_CMD_WINDOW_CREATE 1
#define GUI_CMD_DRAW_RECT     2
#define GUI_CMD_DRAW_STRING   3
#define GUI_CMD_MARK_DIRTY    4
#define GUI_CMD_GET_EVENT     5
#define GUI_CMD_DRAW_ROUNDED_RECT_FILLED 6
#define GUI_CMD_DRAW_IMAGE    7
#define GUI_CMD_GET_STRING_WIDTH 8
#define GUI_CMD_GET_FONT_HEIGHT  9
#define GUI_CMD_WINDOW_SET_RESIZABLE 14
#define GUI_CMD_GET_SCREEN_SIZE  17
#define GUI_CMD_GET_SCREENBUFFER 18
#define GUI_CMD_SHOW_NOTIFICATION 19
#define GUI_CMD_GET_DATETIME     20

#define GUI_EVENT_NONE        0
#define GUI_EVENT_PAINT       1
#define GUI_EVENT_CLICK       2
#define GUI_EVENT_RIGHT_CLICK 3
#define GUI_EVENT_CLOSE       4
#define GUI_EVENT_KEY         5
#define GUI_EVENT_MOUSE_DOWN  6
#define GUI_EVENT_MOUSE_UP    7
#define GUI_EVENT_MOUSE_MOVE  8
#define GUI_EVENT_KEYUP       10
#define GUI_EVENT_RESIZE      11

typedef struct {
    int type;
    int arg1; // For click: x
    int arg2; // For click: y
    int arg3; // For click: button state
} gui_event_t;

#endif
