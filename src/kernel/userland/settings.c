#include "libc/syscall.h"
#include "libc/libui.h"
#include <stddef.h>

#define COLOR_COFFEE    0xFF6B4423
#define COLOR_TEAL      0xFF008080
#define COLOR_GREEN     0xFF008000
#define COLOR_BLUE_BG   0xFF000080
#define COLOR_PURPLE    0xFF800080
#define COLOR_GREY      0xFF454545
#define COLOR_BLACK     0xFF000000

#define COLOR_DARK_PANEL  0xFF2D2D2D
#define COLOR_DARK_TEXT   0xFFE0E0E0
#define COLOR_DARK_BORDER 0xFF404040
#define COLOR_DKGRAY      0xFFAAAAAA
#define COLOR_DARK_BG     0xFF1E1E1E

// Control panel state
#define VIEW_MAIN 0
#define VIEW_WALLPAPER 1
#define VIEW_NETWORK 2
#define VIEW_DESKTOP 3
#define VIEW_MOUSE 4

static int current_view = VIEW_MAIN;
static char rgb_r[4] = "";
static char rgb_g[4] = "";
static char rgb_b[4] = "";
static int focused_field = -1;
static int input_cursor = 0;

static char net_status[64] = "";

// Pattern buffers (128x128)
#define PATTERN_SIZE 128
static uint32_t pattern_lumberjack[PATTERN_SIZE * PATTERN_SIZE];
static uint32_t pattern_blue_diamond[PATTERN_SIZE * PATTERN_SIZE];

#define WALLPAPER_THUMB_W 100
#define WALLPAPER_THUMB_H 60
static uint32_t moon_thumb[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
static uint32_t mtn_thumb[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
static _Bool moon_thumb_valid = 0;
static _Bool mtn_thumb_valid = 0;

static _Bool desktop_snap_to_grid = 1;
static _Bool desktop_auto_align = 1;
static int desktop_max_rows_per_col = 10;
static int desktop_max_cols = 10;
static int mouse_speed = 10;

static void cli_itoa(int num, char *str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    int t = num;
    int len = 0;
    while (t > 0) { len++; t /= 10; }
    str[len] = '\0';
    for (int i = len - 1; i >= 0; i--) {
        str[i] = (num % 10) + '0';
        num /= 10;
    }
}

static void generate_lumberjack_pattern(void) {
    uint32_t red = 0xFFDC143C;
    uint32_t dark_grey = 0xFF404040;
    uint32_t black = 0xFF000000;
    int scale = 5;
    
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            int cell_x = (x / scale) % 3;
            int cell_y = (y / scale) % 3;
            uint32_t color;
            if (cell_x == 1 && cell_y == 1) {
                color = black;
            } else if (cell_x == 1 || cell_y == 1) {
                color = dark_grey;
            } else {
                color = red;
            }
            pattern_lumberjack[y * PATTERN_SIZE + x] = color;
        }
    }
}

static void generate_blue_diamond_pattern(void) {
    uint32_t bg_color = 0xFFADD8E6;
    uint32_t diamond_color = 0xFF0000CD;
    
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            pattern_blue_diamond[y * PATTERN_SIZE + x] = bg_color;
        }
    }
    for (int dy = -24; dy <= 24; dy++) {
        for (int dx = -24; dx <= 24; dx++) {
            int abs_dx = dx < 0 ? -dx : dx;
            int abs_dy = dy < 0 ? -dy : dy;
            if (abs_dx + abs_dy <= 24) {
                int x1 = 32 + dx;
                int y1 = 32 + dy;
                if (x1 >= 0 && x1 < PATTERN_SIZE && y1 >= 0 && y1 < PATTERN_SIZE) {
                    pattern_blue_diamond[y1 * PATTERN_SIZE + x1] = diamond_color;
                }
                int x2 = 96 + dx;
                int y2 = 96 + dy;
                if (x2 >= 0 && x2 < PATTERN_SIZE && y2 >= 0 && y2 < PATTERN_SIZE) {
                    pattern_blue_diamond[y2 * PATTERN_SIZE + x2] = diamond_color;
                }
            }
        }
    }
}

static uint32_t parse_rgb_separate(const char *r, const char *g, const char *b) {
    int rv = 0, gv = 0, bv = 0;
    for (int i = 0; r[i] && i < 3; i++) {
        if (r[i] >= '0' && r[i] <= '9') rv = rv * 10 + (r[i] - '0');
    }
    for (int i = 0; g[i] && i < 3; i++) {
        if (g[i] >= '0' && g[i] <= '9') gv = gv * 10 + (g[i] - '0');
    }
    for (int i = 0; b[i] && i < 3; i++) {
        if (b[i] >= '0' && b[i] <= '9') bv = bv * 10 + (b[i] - '0');
    }
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    return 0xFF000000 | (rv << 16) | (gv << 8) | bv;
}

static void control_panel_paint_main(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 21;
    int win_w = 350;
    
    int item_y = 15;
    int item_h = 60;
    int item_spacing = 10;
    
    // Wallpaper
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    ui_draw_rect(win, offset_x + 12, offset_y + item_y + 8, 40, 40, 0xFF87CEEB);
    ui_draw_rect(win, offset_x + 12, offset_y + item_y + 28, 40, 20, 0xFF90EE90);
    ui_draw_rect(win, offset_x + 24, offset_y + item_y + 22, 3, 6, 0xFF654321);
    ui_draw_rect(win, offset_x + 21, offset_y + item_y + 18, 9, 8, 0xFF228B22);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Wallpaper", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Choose wallpaper", COLOR_DKGRAY);
    
    // Network
    item_y += item_h + item_spacing;
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    ui_draw_rect(win, offset_x + 18, offset_y + item_y + 12, 24, 24, 0xFF4169E1);
    ui_draw_rect(win, offset_x + 22, offset_y + item_y + 16, 16, 16, 0xFF87CEEB);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Network", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Internet and connectivity", COLOR_DKGRAY);
    
    // Desktop
    item_y += item_h + item_spacing;
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    ui_draw_rect(win, offset_x + 12, offset_y + item_y + 10, 36, 8, 0xFFE0C060);
    ui_draw_rect(win, offset_x + 12, offset_y + item_y + 18, 36, 22, 0xFFD4A574);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Desktop", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Desktop alignment", COLOR_DKGRAY);
    
    // Mouse
    item_y += item_h + item_spacing;
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    ui_draw_rect(win, offset_x + 18, offset_y + item_y + 8, 20, 28, 0xFFD3D3D3);
    ui_draw_rect(win, offset_x + 20, offset_y + item_y + 10, 16, 10, 0xFFB0B0B0);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Mouse", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Pointer settings", COLOR_DKGRAY);
}

static void control_panel_paint_wallpaper(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 21;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    ui_draw_string(win, offset_x, offset_y + 40, "Presets:", COLOR_DARK_TEXT);
    
    int button_y = offset_y + 65;
    int button_x = offset_x;
    
    // Colors
    ui_draw_rounded_rect_filled(win, button_x, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 8, button_y + 6, 18, 13, COLOR_COFFEE);
    ui_draw_string(win, button_x + 35, button_y + 8, "Coffee", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, button_x + 100, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 108, button_y + 6, 18, 13, COLOR_TEAL);
    ui_draw_string(win, button_x + 135, button_y + 8, "Teal", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, button_x + 200, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 208, button_y + 6, 18, 13, COLOR_GREEN);
    ui_draw_string(win, button_x + 235, button_y + 8, "Green", COLOR_DARK_TEXT);
    
    button_y += 35;
    ui_draw_rounded_rect_filled(win, button_x, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 8, button_y + 6, 18, 13, COLOR_BLUE_BG);
    ui_draw_string(win, button_x + 35, button_y + 8, "Blue", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, button_x + 100, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 108, button_y + 6, 18, 13, COLOR_PURPLE);
    ui_draw_string(win, button_x + 132, button_y + 8, "Purple", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, button_x + 200, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    ui_draw_rect(win, button_x + 208, button_y + 6, 18, 13, COLOR_GREY);
    ui_draw_string(win, button_x + 235, button_y + 8, "Grey", COLOR_DARK_TEXT);
    
    // Patterns
    button_y += 40;
    ui_draw_string(win, offset_x, button_y, "Patterns:", COLOR_DARK_TEXT);
    
    button_y += 20;
    ui_draw_rounded_rect_filled(win, button_x, button_y, 132, 25, 6, COLOR_DARK_PANEL);
    for (int py = 0; py < 10; py++) {
        for (int px = 0; px < 12; px++) {
            int cell_x = px % 3;
            int cell_y = py % 3;
            uint32_t color = (cell_x == 1 && cell_y == 1) ? 0xFF000000 : 
                           (cell_x == 1 || cell_y == 1) ? 0xFF404040 : 0xFFDC143C;
            ui_draw_rect(win, button_x + 8 + px, button_y + 7 + py, 1, 1, color);
        }
    }
    ui_draw_string(win, button_x + 28, button_y + 8, "Lumberjack", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, button_x + 145, button_y, 132, 25, 6, COLOR_DARK_PANEL);
    for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 10; px++) {
            int cx = px - 5;
            int cy = py - 4;
            int abs_cx = cx < 0 ? -cx : cx;
            int abs_cy = cy < 0 ? -cy : cy;
            uint32_t color = (abs_cx + abs_cy <= 3) ? 0xFF0000CD : 0xFFADD8E6;
            ui_draw_rect(win, button_x + 153 + px, button_y + 8 + py, 1, 1, color);
        }
    }
    ui_draw_string(win, button_x + 165, button_y + 8, "Blue Diamond", COLOR_DARK_TEXT);
    
    // Custom color
    button_y += 40;
    ui_draw_string(win, offset_x, button_y, "Custom color:", COLOR_DARK_TEXT);
    button_y += 20;
    
    ui_draw_string(win, button_x, button_y + 4, "R:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, button_x + 25, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, button_x + 30, button_y + 4, rgb_r, (focused_field == 0) ? 0xFFFF6B6B : COLOR_DARK_TEXT);
    if (focused_field == 0) {
        int cursor_x = button_x + 30 + input_cursor * 8;
        ui_draw_rect(win, cursor_x, button_y + 4, 1, 9, 0xFFFF6B6B);
    }
    
    ui_draw_string(win, button_x + 90, button_y + 4, "G:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, button_x + 115, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, button_x + 120, button_y + 4, rgb_g, (focused_field == 1) ? 0xFF90EE90 : COLOR_DARK_TEXT);
    if (focused_field == 1) {
        int cursor_x = button_x + 120 + input_cursor * 8;
        ui_draw_rect(win, cursor_x, button_y + 4, 1, 9, 0xFF90EE90);
    }
    
    ui_draw_string(win, button_x + 180, button_y + 4, "B:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, button_x + 205, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, button_x + 210, button_y + 4, rgb_b, (focused_field == 2) ? 0xFF87CEEB : COLOR_DARK_TEXT);
    if (focused_field == 2) {
        int cursor_x = button_x + 210 + input_cursor * 8;
        ui_draw_rect(win, cursor_x, button_y + 4, 1, 9, 0xFF87CEEB);
    }
    
    ui_draw_rounded_rect_filled(win, button_x, button_y + 25, 70, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, button_x + 18, button_y + 33, "Apply", COLOR_DARK_TEXT);
    
    // Wallpapers section
    button_y += 60;
    ui_draw_string(win, offset_x, button_y, "Wallpapers:", COLOR_DARK_TEXT);
    button_y += 20;
    
    ui_draw_rounded_rect_filled(win, button_x, button_y, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 24, 6, COLOR_DARK_PANEL);
    if (moon_thumb_valid) {
        for (int ty = 0; ty < WALLPAPER_THUMB_H; ty++) {
            for (int tx = 0; tx < WALLPAPER_THUMB_W; tx++) {
                ui_draw_rect(win, button_x + 4 + tx, button_y + 4 + ty, 1, 1, moon_thumb[ty * WALLPAPER_THUMB_W + tx]);
            }
        }
    } else {
        ui_draw_string(win, button_x + 20, button_y + 30, "Error", 0xFFFF4444);
    }
    ui_draw_string(win, button_x + 30, button_y + WALLPAPER_THUMB_H + 8, "Moon", COLOR_DARK_TEXT);
    
    int thumb2_x = button_x + WALLPAPER_THUMB_W + 20;
    ui_draw_rounded_rect_filled(win, thumb2_x, button_y, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 24, 6, COLOR_DARK_PANEL);
    if (mtn_thumb_valid) {
        for (int ty = 0; ty < WALLPAPER_THUMB_H; ty++) {
            for (int tx = 0; tx < WALLPAPER_THUMB_W; tx++) {
                ui_draw_rect(win, thumb2_x + 4 + tx, button_y + 4 + ty, 1, 1, mtn_thumb[ty * WALLPAPER_THUMB_W + tx]);
            }
        }
    } else {
        ui_draw_string(win, thumb2_x + 20, button_y + 30, "Error", 0xFFFF4444);
    }
    ui_draw_string(win, thumb2_x + 16, button_y + WALLPAPER_THUMB_H + 8, "Mountain", COLOR_DARK_TEXT);
}

static void control_panel_paint_network(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 21;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    ui_draw_string(win, offset_x, offset_y + 40, "Network:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 55, 140, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, offset_y + 63, "Init Network", COLOR_DARK_TEXT);
    
    if (net_status[0] != '\0') {
        ui_draw_string(win, offset_x + 150, offset_y + 63, net_status, 0xFF90EE90);
    }
}

static void control_panel_paint_desktop(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 21;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x, offset_y + 40, "Desktop Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    
    ui_draw_rounded_rect_filled(win, offset_x, section_y, 16, 16, 3, COLOR_DARK_PANEL);
    if (desktop_snap_to_grid) ui_draw_string(win, offset_x + 3, section_y + 1, "v", 0xFF90EE90);
    ui_draw_string(win, offset_x + 25, section_y + 3, "Snap to Grid", COLOR_DARK_TEXT);
    
    section_y += 25;
    ui_draw_rounded_rect_filled(win, offset_x, section_y, 16, 16, 3, COLOR_DARK_PANEL);
    if (desktop_auto_align) ui_draw_string(win, offset_x + 3, section_y + 1, "v", 0xFF90EE90);
    ui_draw_string(win, offset_x + 25, section_y + 3, "Auto Align Icons", COLOR_DARK_TEXT);
    
    section_y += 30;
    ui_draw_string(win, offset_x, section_y + 3, "Apps per column:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x + 130, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    
    char num[4];
    cli_itoa(desktop_max_rows_per_col, num);
    ui_draw_string(win, offset_x + 160, section_y + 5, num, COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, offset_x + 180, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
    
    section_y += 30;
    ui_draw_string(win, offset_x, section_y + 3, "Columns:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x + 130, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    
    char num_c[4];
    cli_itoa(desktop_max_cols, num_c);
    ui_draw_string(win, offset_x + 160, section_y + 5, num_c, COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, offset_x + 180, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
}

static void control_panel_paint_mouse(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 21;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x, offset_y + 40, "Mouse Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    ui_draw_string(win, offset_x, section_y, "Speed:", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, offset_x + 60, section_y + 8, 200, 8, 4, COLOR_DARK_PANEL);
    
    int knob_x = offset_x + 60 + (mouse_speed - 1) * 190 / 49;
    ui_draw_rounded_rect_filled(win, knob_x, section_y + 2, 10, 14, 3, 0xFF4A90E2);
    
    ui_draw_string(win, offset_x + 270, section_y + 4, "x", COLOR_DARK_TEXT);
    char speed_str[4];
    cli_itoa(mouse_speed, speed_str);
    ui_draw_string(win, offset_x + 280, section_y + 4, speed_str, COLOR_DARK_TEXT);
}

static void control_panel_paint(ui_window_t win) {
    // Fill background
    ui_draw_rect(win, 0, 20, 350, 480, COLOR_DARK_BG);

    if (current_view == VIEW_MAIN) {
        control_panel_paint_main(win);
    } else if (current_view == VIEW_WALLPAPER) {
        control_panel_paint_wallpaper(win);
    } else if (current_view == VIEW_NETWORK) {
        control_panel_paint_network(win);
    } else if (current_view == VIEW_DESKTOP) {
        control_panel_paint_desktop(win);
    } else if (current_view == VIEW_MOUSE) {
        control_panel_paint_mouse(win);
    }
}

static void save_desktop_config(void) {
    sys_system(4 /*SET_DESKTOP_PROP*/, 1, desktop_snap_to_grid, 0, 0);
    sys_system(4, 2, desktop_auto_align, 0, 0);
    sys_system(4, 3, desktop_max_rows_per_col, 0, 0);
    sys_system(4, 4, desktop_max_cols, 0, 0);
}

static void save_mouse_config(void) {
    sys_system(5 /*SET_MOUSE_SPEED*/, mouse_speed, 0, 0, 0);
}

static void fetch_kernel_state(void) {
    desktop_snap_to_grid = sys_system(7 /*GET_DESKTOP_PROP*/, 1, 0, 0, 0);
    desktop_auto_align = sys_system(7, 2, 0, 0, 0);
    desktop_max_rows_per_col = sys_system(7, 3, 0, 0, 0);
    desktop_max_cols = sys_system(7, 4, 0, 0, 0);
    mouse_speed = sys_system(8 /*GET_MOUSE_SPEED*/, 0, 0, 0, 0);
    
    if (sys_system(9, 0, (uint64_t)moon_thumb, 0, 0) == 0) {
        moon_thumb_valid = 1;
    }
    if (sys_system(9, 1, (uint64_t)mtn_thumb, 0, 0) == 0) {
        mtn_thumb_valid = 1;
    }
}

static void control_panel_handle_click(int x, int y) {
    int win_w = 350;
    
    if (current_view == VIEW_MAIN) {
        int offset_x = 8;
        int offset_y = 21;
        int item_h = 60;
        int item_spacing = 10;
        
        int item_y = offset_y + 15;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_WALLPAPER;
            focused_field = -1;
        }
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_NETWORK;
            focused_field = -1;
        }
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_DESKTOP;
        }
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_MOUSE;
        }
    } else if (current_view == VIEW_WALLPAPER) {
        int offset_x = 8;
        int offset_y = 21;
        int button_y = offset_y + 65;
        int button_x = offset_x;
        
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        if (x >= button_x && x < button_x + 91 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_COFFEE, 0, 0, 0);
            return;
        }
        if (x >= button_x + 100 && x < button_x + 191 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_TEAL, 0, 0, 0);
            return;
        }
        if (x >= button_x + 200 && x < button_x + 291 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_GREEN, 0, 0, 0);
            return;
        }
        
        button_y += 35;
        if (x >= button_x && x < button_x + 91 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_BLUE_BG, 0, 0, 0);
            return;
        }
        if (x >= button_x + 100 && x < button_x + 191 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_PURPLE, 0, 0, 0);
            return;
        }
        if (x >= button_x + 200 && x < button_x + 291 && y >= button_y && y < button_y + 25) {
            sys_system(1, COLOR_GREY, 0, 0, 0);
            return;
        }
        
        button_y += 60; // 40 + 20
        if (x >= button_x && x < button_x + 132 && y >= button_y && y < button_y + 25) {
            sys_system(2, (uint64_t)pattern_lumberjack, 0, 0, 0);
            return;
        }
        if (x >= button_x + 145 && x < button_x + 277 && y >= button_y && y < button_y + 25) {
            sys_system(2, (uint64_t)pattern_blue_diamond, 0, 0, 0);
            return;
        }
        
        button_y += 60;
        if (x >= button_x + 25 && x < button_x + 75 && y >= button_y && y < button_y + 18) {
            if (focused_field != 0) rgb_r[0] = 0;
            focused_field = 0; input_cursor = 0; return;
        }
        if (x >= button_x + 115 && x < button_x + 165 && y >= button_y && y < button_y + 18) {
            if (focused_field != 1) rgb_g[0] = 0;
            focused_field = 1; input_cursor = 0; return;
        }
        if (x >= button_x + 205 && x < button_x + 255 && y >= button_y && y < button_y + 18) {
            if (focused_field != 2) rgb_b[0] = 0;
            focused_field = 2; input_cursor = 0; return;
        }
        
        if (x >= button_x && x < button_x + 70 && y >= button_y + 25 && y < button_y + 50) {
            uint32_t cust = parse_rgb_separate(rgb_r, rgb_g, rgb_b);
            sys_system(1, cust, 0, 0, 0);
            return;
        }
        
        button_y += 80;
        if (x >= button_x && x < button_x + WALLPAPER_THUMB_W + 8 && y >= button_y && y < button_y + WALLPAPER_THUMB_H + 24) {
            sys_system(3, 0, 0, 0, 0);
            return;
        }
        int thumb2_x = button_x + WALLPAPER_THUMB_W + 20;
        if (x >= thumb2_x && x < thumb2_x + WALLPAPER_THUMB_W + 8 && y >= button_y && y < button_y + WALLPAPER_THUMB_H + 24) {
            sys_system(3, 1, 0, 0, 0);
            return;
        }
    } else if (current_view == VIEW_NETWORK) {
        int offset_x = 8;
        int offset_y = 21;
        
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            focused_field = -1;
            return;
        }
        
        if (x >= offset_x && x < offset_x + 140 && y >= offset_y + 55 && y < offset_y + 80) {
            if (sys_system(6, 0, 0, 0, 0) == 0) {
                net_status[0] = 'I'; net_status[1] = 'n'; net_status[2] = 'i'; 
                net_status[3] = 't'; net_status[4] = 'e'; net_status[5] = 'd'; net_status[6] = 0;
            } else {
                net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i'; 
                net_status[3] = 'l'; net_status[4] = 'e'; net_status[5] = 'd'; net_status[6] = 0;
            }
        }
    } else if (current_view == VIEW_DESKTOP) {
        int offset_x = 8;
        int offset_y = 21;
        
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        int section_y = offset_y + 65;
        if (x >= offset_x && x < offset_x + 16 && y >= section_y && y < section_y + 16) {
            desktop_snap_to_grid = !desktop_snap_to_grid;
            if (!desktop_snap_to_grid) desktop_auto_align = 0;
            save_desktop_config();
            return;
        }
        
        section_y += 25;
        if (x >= offset_x && x < offset_x + 16 && y >= section_y && y < section_y + 16) {
            desktop_auto_align = !desktop_auto_align;
            if (desktop_auto_align) desktop_snap_to_grid = 1;
            save_desktop_config();
            return;
        }
        
        section_y += 25;
        if (x >= offset_x + 130 && x < offset_x + 150 && y >= section_y && y < section_y + 20) {
            if (desktop_max_rows_per_col > 1) {
                desktop_max_rows_per_col--;
                save_desktop_config();
            }
        }
        if (x >= offset_x + 180 && x < offset_x + 200 && y >= section_y && y < section_y + 20) {
            if (desktop_max_rows_per_col < 15) desktop_max_rows_per_col++;
            save_desktop_config();
        }
        
        section_y += 25;
        if (x >= offset_x + 130 && x < offset_x + 150 && y >= section_y && y < section_y + 20) {
            if (desktop_max_cols > 1) {
                desktop_max_cols--;
                save_desktop_config();
            }
        }
        if (x >= offset_x + 180 && x < offset_x + 200 && y >= section_y && y < section_y + 20) {
            desktop_max_cols++;
            save_desktop_config();
        }
    } else if (current_view == VIEW_MOUSE) {
        int offset_x = 8;
        int offset_y = 21;
        
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        int section_y = offset_y + 65;
        if (x >= offset_x + 60 && x <= offset_x + 260 && y >= section_y && y <= section_y + 20) {
            int new_speed = 1 + (x - (offset_x + 60)) * 49 / 200;
            if (new_speed < 1) new_speed = 1;
            if (new_speed > 50) new_speed = 50;
            mouse_speed = new_speed;
            save_mouse_config();
            return;
        }
    }
}

static void control_panel_handle_key(char c) {
    if (focused_field < 0) return;
    
    if (current_view == VIEW_WALLPAPER) {
        char *focused_buffer = NULL;
        int max_len = 3;
        
        if (focused_field == 0) focused_buffer = rgb_r;
        else if (focused_field == 1) focused_buffer = rgb_g;
        else if (focused_field == 2) focused_buffer = rgb_b;
        else return;
        
        if (c == '\b') {
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c >= '0' && c <= '9') {
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {
            focused_field = (focused_field + 1) % 3;
            input_cursor = 0;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ui_window_t win = ui_window_create("Settings", 200, 150, 350, 500);
    if (!win) return 1;

    generate_lumberjack_pattern();
    generate_blue_diamond_pattern();
    
    fetch_kernel_state();
    
    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                control_panel_paint(win);
                ui_mark_dirty(win, 0, 0, 350, 500);
            } else if (ev.type == GUI_EVENT_CLICK) {
                control_panel_handle_click(ev.arg1, ev.arg2);
                control_panel_paint(win);
                ui_mark_dirty(win, 0, 0, 350, 500);
            } else if (ev.type == GUI_EVENT_KEY) {
                control_panel_handle_key((char)ev.arg1);
                control_panel_paint(win);
                ui_mark_dirty(win, 0, 0, 350, 500);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }
    }
    return 0;
}
