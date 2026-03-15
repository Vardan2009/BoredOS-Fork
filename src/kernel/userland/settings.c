// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "stb_image.h"
#include <stddef.h>
#include <stdint.h>

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
#define VIEW_FONTS 5
#define VIEW_DISPLAY 6

static int disp_sel_res = 2;
static int disp_sel_color = 0;

static int current_view = VIEW_MAIN;
static char rgb_r[4] = "";
static char rgb_g[4] = "";
static char rgb_b[4] = "";
static char custom_res_w[6] = "";
static char custom_res_h[6] = "";
static char net_ip[16] = "";
static char net_dns[16] = "";
static int focused_field = -1;
static int input_cursor = 0;

static int dyn_res_w[3];
static int dyn_res_h[3];
static char dyn_res_str[3][32];

static void init_dynamic_resolutions(void) {
    uint64_t phys_w = 1920, phys_h = 1080;
    ui_get_screen_size(&phys_w, &phys_h);
    
    dyn_res_w[2] = (int)phys_w; dyn_res_h[2] = (int)phys_h;
    dyn_res_w[1] = (int)((phys_w * 3) / 4); dyn_res_h[1] = (int)((phys_h * 3) / 4);
    dyn_res_w[0] = (int)(phys_w / 2); dyn_res_h[0] = (int)(phys_h / 2);
    
    for (int i = 0; i < 2; i++) {
        dyn_res_w[i] &= ~1;
        dyn_res_h[i] &= ~1;
    }

    for (int i = 0; i < 3; i++) {
        char bw[16], bh[16];
        itoa(dyn_res_w[i], bw);
        itoa(dyn_res_h[i], bh);
        strcpy(dyn_res_str[i], bw);
        strcat(dyn_res_str[i], "x");
        strcat(dyn_res_str[i], bh);
    }
}

static char net_status[64] = "";

// Pattern buffers (128x128)
#define PATTERN_SIZE 128
static uint32_t pattern_lumberjack[PATTERN_SIZE * PATTERN_SIZE];
static uint32_t pattern_blue_diamond[PATTERN_SIZE * PATTERN_SIZE];

#define MAX_WALLPAPERS 10
#define WALLPAPER_THUMB_W 80
#define WALLPAPER_THUMB_H 50

typedef struct {
    char path[128];
    char name[64];
    uint32_t thumb[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
    _Bool valid;
} wallpaper_entry_t;

static wallpaper_entry_t wallpapers[MAX_WALLPAPERS];
static int wallpaper_count = 0;

static _Bool desktop_snap_to_grid = 1;
static _Bool desktop_auto_align = 1;
static int desktop_max_rows_per_col = 10;
static int desktop_max_cols = 10;
static int mouse_speed = 10;

// Font selection
#define MAX_FONTS 16
typedef struct {
    char path[128];
    char name[48];
} font_entry_t;
static font_entry_t fonts[MAX_FONTS];
static int font_count = 0;
static int selected_font = -1;

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

static void k_itoa_hex(uint64_t num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    char buf[64];
    int len = 0;
    while (num > 0) {
        int rem = num % 16;
        if (rem < 10) buf[len++] = rem + '0';
        else buf[len++] = rem - 10 + 'a';
        num /= 16;
    }
    for (int i = 0; i < len; i++) {
        str[i] = buf[len - 1 - i];
    }
    str[len] = '\0';
}

static void scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h, uint32_t *dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
        int src_y = y * src_h / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int src_x = x * src_w / dst_w;
            int idx = (src_y * src_w + src_x) * 4;
            uint8_t r = rgba[idx];
            uint8_t g = rgba[idx + 1];
            uint8_t b = rgba[idx + 2];
            uint8_t a = rgba[idx + 3];
            dst[y * dst_w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void load_wallpapers(void) {
    wallpaper_count = 0;
    FAT32_FileInfo info[MAX_WALLPAPERS];
    int count = sys_list("/Library/images/Wallpapers", info, MAX_WALLPAPERS);
    if (count < 0) return;

    for (int i = 0; i < count && wallpaper_count < MAX_WALLPAPERS; i++) {
        if (info[i].is_directory) continue; // Skip directories
        
        // check if .jpg (case-insensitive)
        int len = 0; while (info[i].name[len]) len++;
        if (len < 4) continue;
        char c1 = info[i].name[len-1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        char c2 = info[i].name[len-2]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        char c3 = info[i].name[len-3]; if (c3 >= 'A' && c3 <= 'Z') c3 += 32;
        if (c1 != 'g' || c2 != 'p' || c3 != 'j') continue;

        wallpaper_entry_t *wp = &wallpapers[wallpaper_count];
        // Set path
        char *pref = "/Library/images/Wallpapers/";
        int pl = 0; while (pref[pl]) { wp->path[pl] = pref[pl]; pl++; }
        int nl = 0; while (info[i].name[nl]) { wp->path[pl+nl] = info[i].name[nl]; nl++; }
        wp->path[pl+nl] = 0;

        // Set name (strip .jpg)
        for (int j = 0; j < nl - 4 && j < 63; j++) wp->name[j] = info[i].name[j];
        wp->name[(nl-4 < 63) ? nl-4 : 63] = 0;

        // Load and generate thumbnail
        int fd = sys_open(wp->path, "r");
        if (fd >= 0) {
            int size = sys_seek(fd, 0, 2); // SEEK_END
            sys_seek(fd, 0, 0); // SEEK_SET
            if (size > 0 && size < 8 * 1024 * 1024) {
                    unsigned char *buf = (unsigned char *)malloc(size);
                    if (buf) {
                        sys_read(fd, buf, size);
                        int img_w, img_h, channels;
                        unsigned char *img = stbi_load_from_memory(buf, size, &img_w, &img_h, &channels, 4);
                        if (img && img_w > 0 && img_h > 0) {
                            scale_rgba_to_argb(img, img_w, img_h, wp->thumb, WALLPAPER_THUMB_W, WALLPAPER_THUMB_H);
                            wp->valid = 1;
                            stbi_image_free(img);
                        }
                        free(buf); // Release memory
                    }
            }
            sys_close(fd);
        }

        wallpaper_count++;
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
    int offset_y = 6;
    int win_w = 350;
    
    int item_y = 0;
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
    
    // Fonts
    item_y += item_h + item_spacing;
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    // Font icon: "Aa" stylized
    ui_draw_string(win, offset_x + 14, offset_y + item_y + 10, "Aa", 0xFF6A9EF5);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Fonts", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Choose system font", COLOR_DKGRAY);

    // Display
    item_y += item_h + item_spacing;
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + item_y, win_w - 16, item_h, 8, COLOR_DARK_PANEL);
    // Monitor icon
    ui_draw_rect(win, offset_x + 14, offset_y + item_y + 12, 32, 22, 0xFF4A90E2);
    ui_draw_rect(win, offset_x + 16, offset_y + item_y + 14, 28, 18, 0xFF87CEEB);
    ui_draw_rect(win, offset_x + 26, offset_y + item_y + 34, 8, 4, 0xFFB0B0B0);
    ui_draw_rect(win, offset_x + 22, offset_y + item_y + 38, 16, 2, 0xFFB0B0B0);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Display", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Screen resolution & color", COLOR_DKGRAY);
}

static void control_panel_paint_wallpaper(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
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
    
    for (int i = 0; i < wallpaper_count; i++) {
        int tx = (i % 3) * (WALLPAPER_THUMB_W + 15);
        int ty = (i / 3) * (WALLPAPER_THUMB_H + 25);
        
        ui_draw_rounded_rect_filled(win, button_x + tx, button_y + ty, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 20, 6, COLOR_DARK_PANEL);
        if (wallpapers[i].valid) {
            for (int py = 0; py < WALLPAPER_THUMB_H; py++) {
                for (int px = 0; px < WALLPAPER_THUMB_W; px++) {
                    ui_draw_rect(win, button_x + tx + 4 + px, button_y + ty + 4 + py, 1, 1, wallpapers[i].thumb[py * WALLPAPER_THUMB_W + px]);
                }
            }
        }
        ui_draw_string(win, button_x + tx + 8, button_y + ty + WALLPAPER_THUMB_H + 6, wallpapers[i].name, COLOR_DARK_TEXT);
    }
}

static void control_panel_paint_network(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    ui_draw_string(win, offset_x, offset_y + 40, "Network Adapter:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 55, 140, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, offset_y + 63, "Init Network", COLOR_DARK_TEXT);
    
    if (net_status[0] != '\0') {
        ui_draw_string(win, offset_x + 150, offset_y + 63, net_status, 0xFF90EE90);
    }

    int info_y = offset_y + 90;
    
    // Adapter Info
    char nic_name[64];
    ui_draw_string(win, offset_x, info_y, "NIC:", COLOR_DARK_TEXT);
    if (sys_network_is_initialized() && sys_network_get_nic_name(nic_name) == 0) {
        ui_draw_string(win, offset_x + 40, info_y, nic_name, COLOR_DKGRAY);
    } else {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    }
    info_y += 20;

    ui_draw_string(win, offset_x, info_y, "MAC:", COLOR_DARK_TEXT);
    net_mac_address_t mac;
    if (sys_network_is_initialized() && sys_network_get_mac(&mac) == 0) {
        char mac_str[32];
        char b[4];
        mac_str[0] = 0;
        for (int i=0; i<6; i++) {
            k_itoa_hex(mac.bytes[i], b);
            if (b[1] == 0) { b[1] = b[0]; b[0] = '0'; b[2] = 0; } // zero pad
            strcat(mac_str, b);
            if (i < 5) strcat(mac_str, ":");
        }
        ui_draw_string(win, offset_x + 40, info_y, mac_str, COLOR_DKGRAY);
    } else {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    }
    info_y += 30;

    // Current IP Address
    ui_draw_string(win, offset_x, info_y, "IP:", COLOR_DARK_TEXT);
    if (!sys_network_has_ip()) {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    } else {
        net_ipv4_address_t ip;
        if (sys_network_get_ip(&ip) == 0) {
            char ip_str[32];
            char b[4];
            ip_str[0] = 0;
            for (int i=0; i<4; i++) {
                cli_itoa(ip.bytes[i], b);
                strcat(ip_str, b);
                if (i < 3) strcat(ip_str, ".");
            }
            ui_draw_string(win, offset_x + 40, info_y, ip_str, COLOR_DKGRAY);
        }
    }
    info_y += 20;

    // Current DNS Address
    ui_draw_string(win, offset_x, info_y, "DNS:", COLOR_DARK_TEXT);
    if (!sys_network_has_ip()) {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    } else {
        net_ipv4_address_t dns;
        if (sys_get_dns_server(&dns) == 0) {
            char dns_str[32];
            char b[4];
            dns_str[0] = 0;
            for (int i=0; i<4; i++) {
                cli_itoa(dns.bytes[i], b);
                strcat(dns_str, b);
                if (i < 3) strcat(dns_str, ".");
            }
            ui_draw_string(win, offset_x + 40, info_y, dns_str, COLOR_DKGRAY);
        }
    }
    info_y += 30;

    // IP SET
    ui_draw_string(win, offset_x, info_y + 4, "IPSET:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x + 60, info_y, 140, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 65, info_y + 4, net_ip, (focused_field == 5) ? 0xFF90EE90 : COLOR_DARK_TEXT);
    if (focused_field == 5) {
        int cursor_x = offset_x + 65 + input_cursor * 8;
        ui_draw_rect(win, cursor_x, info_y + 4, 1, 10, 0xFF90EE90);
    }
    
    ui_draw_rounded_rect_filled(win, offset_x + 210, info_y, 50, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 225, info_y + 4, "SET", COLOR_DARK_TEXT);
    
    info_y += 30;

    // DNS SET
    ui_draw_string(win, offset_x, info_y + 4, "DNSSET:", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x + 60, info_y, 140, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 65, info_y + 4, net_dns, (focused_field == 6) ? 0xFF87CEEB : COLOR_DARK_TEXT);
    if (focused_field == 6) {
        int cursor_x = offset_x + 65 + input_cursor * 8;
        ui_draw_rect(win, cursor_x, info_y + 4, 1, 10, 0xFF87CEEB);
    }
    
    ui_draw_rounded_rect_filled(win, offset_x + 210, info_y, 50, 20, 4, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 225, info_y + 4, "SET", COLOR_DARK_TEXT);
}

static void control_panel_paint_desktop(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
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
    int offset_y = 6;
    
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

static void load_fonts(void) {
    font_count = 0;
    FAT32_FileInfo info[MAX_FONTS];
    int count = sys_list("/Library/Fonts", info, MAX_FONTS);
    if (count < 0) return;

    for (int i = 0; i < count && font_count < MAX_FONTS; i++) {
        if (info[i].is_directory) continue;
        // check if .ttf (case-insensitive)
        int len = 0; while (info[i].name[len]) len++;
        if (len < 4) continue;
        char c1 = info[i].name[len-1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        char c2 = info[i].name[len-2]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        char c3 = info[i].name[len-3]; if (c3 >= 'A' && c3 <= 'Z') c3 += 32;
        char c4 = info[i].name[len-4]; if (c4 >= 'A' && c4 <= 'Z') c4 += 32;
        if (c4 != '.' || c3 != 't' || c2 != 't' || c1 != 'f') continue;

        font_entry_t *fe = &fonts[font_count];
        // Build full path
        char *pref = "/Library/Fonts/";
        int pl = 0; while (pref[pl]) { fe->path[pl] = pref[pl]; pl++; }
        int nl = 0; while (info[i].name[nl]) { fe->path[pl+nl] = info[i].name[nl]; nl++; }
        fe->path[pl+nl] = 0;
        // Store display name (strip .ttf)
        for (int j = 0; j < nl - 4 && j < 47; j++) fe->name[j] = info[i].name[j];
        fe->name[(nl-4 < 47) ? nl-4 : 47] = 0;
        font_count++;
    }
}

static void control_panel_paint_fonts(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    ui_draw_string(win, offset_x, offset_y + 40, "System Font:", COLOR_DARK_TEXT);
    
    int item_y = offset_y + 60;
    for (int i = 0; i < font_count; i++) {
        uint32_t bg_color = (i == selected_font) ? 0xFF3D5A80 : COLOR_DARK_PANEL;
        ui_draw_rounded_rect_filled(win, offset_x, item_y, 330, 35, 6, bg_color);
        // Font icon
        ui_draw_string(win, offset_x + 10, item_y + 9, "Aa", 0xFF6A9EF5);
        // Font name
        ui_draw_string(win, offset_x + 40, item_y + 9, fonts[i].name, COLOR_DARK_TEXT);
        if (i == selected_font) {
            ui_draw_string(win, offset_x + 290, item_y + 9, "*", 0xFF90EE90);
        }
        item_y += 40;
    }
}

static void control_panel_paint_display(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    int right_x = offset_x + 160;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x, offset_y + 40, "Resolution:", COLOR_DARK_TEXT);
    
    int btn_y = offset_y + 60;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 0) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, "640x480", COLOR_DARK_TEXT);
    
    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 1) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, "800x600", COLOR_DARK_TEXT);
    
    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 2) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, dyn_res_str[0], COLOR_DARK_TEXT);
    
    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 3) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, dyn_res_str[1], COLOR_DARK_TEXT);
    
    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 4) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, dyn_res_str[2], COLOR_DARK_TEXT);

    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 140, 30, 6, (disp_sel_res == 5) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 30, btn_y + 10, "Custom:", COLOR_DARK_TEXT);

    btn_y += 35;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 60, 25, 4, (focused_field == 3) ? 0xFF4A90E2 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 5, btn_y + 7, custom_res_w[0] ? custom_res_w : "W", (custom_res_w[0] || focused_field == 3) ? 0xFFFFFFFF : 0xFF888888);
    ui_draw_string(win, offset_x + 65, btn_y + 7, "x", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, offset_x + 80, btn_y, 60, 25, 4, (focused_field == 4) ? 0xFF4A90E2 : COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 85, btn_y + 7, custom_res_h[0] ? custom_res_h : "H", (custom_res_h[0] || focused_field == 4) ? 0xFFFFFFFF : 0xFF888888);

    btn_y = offset_y + 60;
    ui_draw_string(win, right_x, offset_y + 40, "Color Depth:", COLOR_DARK_TEXT);

    ui_draw_rounded_rect_filled(win, right_x, btn_y, 140, 30, 6, (disp_sel_color == 0) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, right_x + 40, btn_y + 10, "32-bit", COLOR_DARK_TEXT);
    
    btn_y += 35;
    ui_draw_rounded_rect_filled(win, right_x, btn_y, 140, 30, 6, (disp_sel_color == 1) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, right_x + 40, btn_y + 10, "16-bit", COLOR_DARK_TEXT);

    btn_y += 35;
    ui_draw_rounded_rect_filled(win, right_x, btn_y, 140, 30, 6, (disp_sel_color == 2) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, right_x + 25, btn_y + 10, "256 Colors", COLOR_DARK_TEXT);

    btn_y += 35;
    ui_draw_rounded_rect_filled(win, right_x, btn_y, 140, 30, 6, (disp_sel_color == 3) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, right_x + 30, btn_y + 10, "Grayscale", COLOR_DARK_TEXT);

    btn_y += 35;
    ui_draw_rounded_rect_filled(win, right_x, btn_y, 140, 30, 6, (disp_sel_color == 4) ? 0xFF3D5A80 : COLOR_DARK_PANEL);
    ui_draw_string(win, right_x + 25, btn_y + 10, "Monochrome", COLOR_DARK_TEXT);

    btn_y = offset_y + 320;
    ui_draw_rounded_rect_filled(win, offset_x, btn_y, 300, 35, 6, 0xFF4A90E2);
    ui_draw_string(win, offset_x + 125, btn_y + 12, "Apply", 0xFFFFFFFF);
}

static void control_panel_paint(ui_window_t win) {
    // Fill background
    ui_draw_rect(win, 0, 0, 350, 500, COLOR_DARK_BG);

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
    } else if (current_view == VIEW_FONTS) {
        control_panel_paint_fonts(win);
    } else if (current_view == VIEW_DISPLAY) {
        control_panel_paint_display(win);
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

static int parse_ip(const char* str, net_ipv4_address_t* ip) {
    int val = 0;
    int part = 0;
    const char* p = str;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
        } else if (*p == '.') {
            if (part > 3) return -1;
            ip->bytes[part++] = (uint8_t)val;
            val = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (part != 3) return -1;
    ip->bytes[3] = (uint8_t)val;
    return 0;
}

static void fetch_kernel_state(void) {
    desktop_snap_to_grid = sys_system(7 /*GET_DESKTOP_PROP*/, 1, 0, 0, 0);
    desktop_auto_align = sys_system(7, 2, 0, 0, 0);
    desktop_max_rows_per_col = sys_system(7, 3, 0, 0, 0);
    desktop_max_cols = sys_system(7, 4, 0, 0, 0);
    mouse_speed = sys_system(8 /*GET_MOUSE_SPEED*/, 0, 0, 0, 0);
    
    net_ipv4_address_t kip;
    if (sys_network_get_ip(&kip) == 0) {
        char bp[4];
        net_ip[0] = 0;
        for (int i=0; i<4; i++) {
            cli_itoa(kip.bytes[i], bp);
            strcat(net_ip, bp);
            if (i < 3) strcat(net_ip, ".");
        }
    }

    if (sys_get_dns_server(&kip) == 0) {
        char bp[4];
        net_dns[0] = 0;
        for (int i=0; i<4; i++) {
            cli_itoa(kip.bytes[i], bp);
            strcat(net_dns, bp);
            if (i < 3) strcat(net_dns, ".");
        }
    }

    init_dynamic_resolutions();
    load_wallpapers();
}

static void control_panel_handle_click(int x, int y) {
    int win_w = 350;
    
    if (current_view == VIEW_MAIN) {
        int offset_x = 8;
        int offset_y = 6;
        int item_h = 60;
        int item_spacing = 10;
        
        int item_y = offset_y + 0;
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
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_FONTS;
            if (font_count == 0) load_fonts();
        }
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win_w - 8 && y >= item_y && y < item_y + item_h) {
            current_view = VIEW_DISPLAY;
        }
    } else if (current_view == VIEW_WALLPAPER) {
        int offset_x = 8;
        int offset_y = 6;
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
        for (int i = 0; i < wallpaper_count; i++) {
            int tx = (i % 3) * (WALLPAPER_THUMB_W + 15);
            int ty = (i / 3) * (WALLPAPER_THUMB_H + 25);
            if (x >= button_x + tx && x < button_x + tx + WALLPAPER_THUMB_W + 8 && 
                y >= button_y + ty && y < button_y + ty + WALLPAPER_THUMB_H + 20) {
                sys_system(31, (uint64_t)wallpapers[i].path, 0, 0, 0);
                return;
            }
        }
    } else if (current_view == VIEW_NETWORK) {
        int offset_x = 8;
        int offset_y = 6;
        
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
            return;
        }

        int info_y = offset_y + 90 + 20 + 30; // Info + MAC Y positions
        
        info_y += 20; // IP display
        info_y += 30; // DNS display

        // IPSET click bounds
        if (x >= offset_x + 60 && x < offset_x + 200 && y >= info_y && y < info_y + 20) {
            focused_field = 5;
            int len = 0; while (net_ip[len]) len++; input_cursor = len;
            return;
        }

        // Apply IPSET click block
        if (x >= offset_x + 210 && x < offset_x + 260 && y >= info_y && y < info_y + 20) {
            net_ipv4_address_t ip;
            if (parse_ip(net_ip, &ip) == 0) {
                sys_network_set_ip(&ip);
            }
            return;
        }

        info_y += 30;
        
        // DNSSET click bounds
        if (x >= offset_x + 60 && x < offset_x + 200 && y >= info_y && y < info_y + 20) {
            focused_field = 6;
            int len = 0; while (net_dns[len]) len++; input_cursor = len;
            return;
        }

        // Apply DNSSET click block
        if (x >= offset_x + 210 && x < offset_x + 260 && y >= info_y && y < info_y + 20) {
            net_ipv4_address_t ip;
            if (parse_ip(net_dns, &ip) == 0) {
                sys_set_dns_server(&ip);
            }
            return;
        }
    } else if (current_view == VIEW_DESKTOP) {
        int offset_x = 8;
        int offset_y = 6;
        
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
        int offset_y = 6;
        
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
    } else if (current_view == VIEW_FONTS) {
        int offset_x = 8;
        int offset_y = 6;
        
        // Back button
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        // Font items
        int item_y = offset_y + 60;
        for (int i = 0; i < font_count; i++) {
            if (x >= offset_x && x < offset_x + 330 && y >= item_y && y < item_y + 35) {
                selected_font = i;
                sys_system(40 /*SET_FONT*/, (uint64_t)fonts[i].path, 0, 0, 0);
                return;
            }
            item_y += 40;
        }
    } else if (current_view == VIEW_DISPLAY) {
        int offset_x = 8;
        int offset_y = 6;
        int right_x = offset_x + 160;
        
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }

        int btn_y = offset_y + 60;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 0;
        if (x >= right_x && x < right_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_color = 0;
        
        btn_y += 35;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 1;
        if (x >= right_x && x < right_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_color = 1;

        btn_y += 35;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 2;
        if (x >= right_x && x < right_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_color = 2;

        btn_y += 35;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 3;
        if (x >= right_x && x < right_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_color = 3;

        btn_y += 35;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 4;
        if (x >= right_x && x < right_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_color = 4;

        btn_y += 35;
        if (x >= offset_x && x < offset_x + 140 && y >= btn_y && y < btn_y + 30) disp_sel_res = 5;

        // Custom Inputs
        btn_y += 35;
        if (x >= offset_x && x < offset_x + 60 && y >= btn_y && y < btn_y + 25) {
            focused_field = 3; disp_sel_res = 5;
            int len = 0; while (custom_res_w[len]) len++; input_cursor = len;
        }
        if (x >= offset_x + 80 && x < offset_x + 140 && y >= btn_y && y < btn_y + 25) {
            focused_field = 4; disp_sel_res = 5;
            int len = 0; while (custom_res_h[len]) len++; input_cursor = len;
        }

        btn_y = offset_y + 320;
        if (x >= offset_x && x < offset_x + 300 && y >= btn_y && y < btn_y + 35) {
            int w = 1024, h = 768;
            if (disp_sel_res == 0) { w = 640; h = 480; }
            else if (disp_sel_res == 1) { w = 800; h = 600; }
            else if (disp_sel_res >= 2 && disp_sel_res <= 4) {
                w = dyn_res_w[disp_sel_res - 2];
                h = dyn_res_h[disp_sel_res - 2];
            } else if (disp_sel_res == 5) {
                extern int atoi(const char *str);
                int cw = atoi(custom_res_w);
                int ch = atoi(custom_res_h);
                if (cw >= 320 && ch >= 200) { w = cw; h = ch; }
            }
            
            int bpp = 32, mode = 0;
            if (disp_sel_color == 1) { bpp = 16; }
            if (disp_sel_color == 2) { bpp = 8; mode = 0; }
            if (disp_sel_color == 3) { bpp = 8; mode = 1; }
            if (disp_sel_color == 4) { bpp = 8; mode = 2; }
            
            sys_system(47 /*SET_RESOLUTION*/, w, h, bpp, mode);
        }
    }
}

static void control_panel_handle_key(char c, bool pressed) {
    if (!pressed) return;
    if (focused_field < 0) return;
    
    if (current_view == VIEW_WALLPAPER || current_view == VIEW_DISPLAY || current_view == VIEW_NETWORK) {
        char *focused_buffer = NULL;
        int max_len = 3;
        
        if (focused_field == 0 && current_view == VIEW_WALLPAPER) focused_buffer = rgb_r;
        else if (focused_field == 1 && current_view == VIEW_WALLPAPER) focused_buffer = rgb_g;
        else if (focused_field == 2 && current_view == VIEW_WALLPAPER) focused_buffer = rgb_b;
        else if (focused_field == 3 && current_view == VIEW_DISPLAY) { focused_buffer = custom_res_w; max_len = 5; }
        else if (focused_field == 4 && current_view == VIEW_DISPLAY) { focused_buffer = custom_res_h; max_len = 5; }
        else if (focused_field == 5 && current_view == VIEW_NETWORK) { focused_buffer = net_ip; max_len = 15; }
        else if (focused_field == 6 && current_view == VIEW_NETWORK) { focused_buffer = net_dns; max_len = 15; }
        else return;
        
        if (c == '\b') {
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if ((c >= '0' && c <= '9') || c == '.') {
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {
            if (current_view == VIEW_WALLPAPER) focused_field = (focused_field + 1) % 3;
            else if (current_view == VIEW_NETWORK) focused_field = (focused_field == 5) ? 6 : 5;
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
                control_panel_handle_key((char)ev.arg1, true);
                control_panel_paint(win);
                ui_mark_dirty(win, 0, 0, 350, 500);
            } else if (ev.type == GUI_EVENT_KEYUP) {
                control_panel_handle_key((char)ev.arg1, false);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    return 0;
}

