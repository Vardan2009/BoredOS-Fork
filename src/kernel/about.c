#include "about.h"
#include "graphics.h"
#include "wm.h"
#include <stddef.h>

Window win_about;

static void about_paint(Window *win) {
    int offset_x = win->x + 15;
    int offset_y = win->y + 35;
    

    draw_boredos_logo(win->x + 60, offset_y, 4);
    
    // Version info
    draw_string(offset_x, offset_y + 105, "BoredOS 'Panda'", COLOR_WHITE);
    draw_string(offset_x, offset_y + 120, "BoredOS Version 1.61", COLOR_WHITE);
    draw_string(offset_x, offset_y + 135, "Kernel Version 2.5.2", COLOR_WHITE);
    
    // Copyright
    draw_string(offset_x, offset_y + 150, "(C) 2026 boreddevnl.", COLOR_WHITE);
    draw_string(offset_x, offset_y + 165, "All rights reserved.", COLOR_WHITE);
}

static void about_click(Window *win, int x, int y) {
    (void)win;
    (void)x;
    (void)y;
    // No interactive elements needed for About dialog
}

void about_init(void) {
    win_about.title = "About BoredOS";
    win_about.x = 250;
    win_about.y = 180;
    win_about.w = 185;
    win_about.h = 240;
    win_about.visible = false;
    win_about.focused = false;
    win_about.z_index = 0;
    win_about.paint = about_paint;
    win_about.handle_click = about_click;
    win_about.handle_right_click = NULL;
    win_about.handle_key = NULL;
}
