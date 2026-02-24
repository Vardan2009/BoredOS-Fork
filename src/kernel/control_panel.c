#include "control_panel.h"
#include "graphics.h"
#include <stddef.h>
#include "wm.h"
#include "network.h"
#include "cli_apps/cli_utils.h"
#include "wallpaper.h"

Window win_control_panel;

#define COLOR_COFFEE    0xFF6B4423
#define COLOR_TEAL      0xFF008080
#define COLOR_GREEN     0xFF008000
#define COLOR_BLUE_BG   0xFF000080
#define COLOR_PURPLE    0xFF800080
#define COLOR_GREY      0xFF454545
#define MOUSE_BEIGE     0xFFD6D2C4

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

// Network panel state
static char ip_1[4] = "";
static char ip_2[4] = "";
static char ip_3[4] = "";
static char ip_4[4] = "";
static char dest_ip_1[4] = "";
static char dest_ip_2[4] = "";
static char dest_ip_3[4] = "";
static char dest_ip_4[4] = "";
static char udp_port[6] = "";
static char udp_message[128] = "";
static char net_status[64] = "";

// Pattern buffers (128x128)
#define PATTERN_SIZE 128
static uint32_t pattern_lumberjack[PATTERN_SIZE * PATTERN_SIZE];
static uint32_t pattern_blue_diamond[PATTERN_SIZE * PATTERN_SIZE];

static void generate_lumberjack_pattern(void) {
    // Lumberjack pattern: 3x3 repeating cell, scaled 5x
    // Red corners, dark grey cross (top/left/right/bottom), black center
    uint32_t red = 0xFFDC143C;
    uint32_t dark_grey = 0xFF404040;
    uint32_t black = 0xFF000000;
    int scale = 5;
    
    // Fill entire pattern with the 3x3 repeating cell
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            int cell_x = (x / scale) % 3;
            int cell_y = (y / scale) % 3;
            
            uint32_t color;
            
            // Determine color based on position in 3x3 cell
            if (cell_x == 1 && cell_y == 1) {
                // Center: black
                color = black;
            } else if (cell_x == 1 || cell_y == 1) {
                // Cross (top, left, right, bottom): dark grey
                color = dark_grey;
            } else {
                // Corners: red
                color = red;
            }
            
            pattern_lumberjack[y * PATTERN_SIZE + x] = color;
        }
    }
}

static void generate_blue_diamond_pattern(void) {
    // Blue diamond pattern on light blue background
    uint32_t bg_color = 0xFFADD8E6;  // Light blue
    uint32_t diamond_color = 0xFF0000CD;  // Medium blue
    
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            pattern_blue_diamond[y * PATTERN_SIZE + x] = bg_color;
        }
    }
    
    // Draw diamonds (centered at 32, 32 and 96, 96)
    for (int dy = -24; dy <= 24; dy++) {
        for (int dx = -24; dx <= 24; dx++) {
            // Diamond shape: |dx| + |dy| <= 24
            int abs_dx = dx < 0 ? -dx : dx;
            int abs_dy = dy < 0 ? -dy : dy;
            if (abs_dx + abs_dy <= 24) {
                // Top-left diamond
                int x1 = 32 + dx;
                int y1 = 32 + dy;
                if (x1 >= 0 && x1 < PATTERN_SIZE && y1 >= 0 && y1 < PATTERN_SIZE) {
                    pattern_blue_diamond[y1 * PATTERN_SIZE + x1] = diamond_color;
                }
                
                // Bottom-right diamond
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
    
    // Parse R
    for (int i = 0; r[i] && i < 3; i++) {
        if (r[i] >= '0' && r[i] <= '9') {
            rv = rv * 10 + (r[i] - '0');
        }
    }
    
    // Parse G
    for (int i = 0; g[i] && i < 3; i++) {
        if (g[i] >= '0' && g[i] <= '9') {
            gv = gv * 10 + (g[i] - '0');
        }
    }
    
    // Parse B
    for (int i = 0; b[i] && i < 3; i++) {
        if (b[i] >= '0' && b[i] <= '9') {
            bv = bv * 10 + (b[i] - '0');
        }
    }
    
    // Clamp values
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    
    return 0xFF000000 | (rv << 16) | (gv << 8) | bv;
}

static void control_panel_paint_main(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Draw settings items with rounded boxes
    int item_y = 15;
    int item_h = 60;
    int item_spacing = 10;
    
    // Wallpaper Settings
    draw_rounded_rect_filled(offset_x, offset_y + item_y, win->w - 16, item_h, 8, COLOR_DARK_PANEL);
    // Wallpaper icon: landscape
    draw_rect(offset_x + 12, offset_y + item_y + 8, 40, 40, 0xFF87CEEB);  // Sky
    draw_rect(offset_x + 12, offset_y + item_y + 28, 40, 20, 0xFF90EE90);  // Grass
    draw_rect(offset_x + 24, offset_y + item_y + 22, 3, 6, 0xFF654321);  // Tree trunk
    draw_rect(offset_x + 21, offset_y + item_y + 18, 9, 8, 0xFF228B22);  // Tree leaves
    draw_string(offset_x + 60, offset_y + item_y + 15, "Wallpaper", COLOR_DARK_TEXT);
    draw_string(offset_x + 60, offset_y + item_y + 35, "Choose wallpaper", COLOR_DKGRAY);
    
    // Network Settings
    item_y += item_h + item_spacing;
    draw_rounded_rect_filled(offset_x, offset_y + item_y, win->w - 16, item_h, 8, COLOR_DARK_PANEL);
    // Network icon
    draw_rect(offset_x + 18, offset_y + item_y + 12, 24, 24, 0xFF4169E1);
    draw_rect(offset_x + 22, offset_y + item_y + 16, 16, 16, 0xFF87CEEB);
    draw_string(offset_x + 60, offset_y + item_y + 15, "Network", COLOR_DARK_TEXT);
    draw_string(offset_x + 60, offset_y + item_y + 35, "Internet and connectivity", COLOR_DKGRAY);
    
    // Desktop Settings
    item_y += item_h + item_spacing;
    if (offset_y + item_y + item_h < win->y + win->h) {
        draw_rounded_rect_filled(offset_x, offset_y + item_y, win->w - 16, item_h, 8, COLOR_DARK_PANEL);
        // Desktop icon: folder
        draw_rect(offset_x + 12, offset_y + item_y + 10, 36, 8, 0xFFE0C060);
        draw_rect(offset_x + 12, offset_y + item_y + 18, 36, 22, 0xFFD4A574);
        draw_string(offset_x + 60, offset_y + item_y + 15, "Desktop", COLOR_DARK_TEXT);
        draw_string(offset_x + 60, offset_y + item_y + 35, "Desktop alignment", COLOR_DKGRAY);
    }
    
    // Mouse Settings
    item_y += item_h + item_spacing;
    draw_rounded_rect_filled(offset_x, offset_y + item_y, win->w - 16, item_h, 8, COLOR_DARK_PANEL);
    // Mouse icon
    draw_rect(offset_x + 18, offset_y + item_y + 8, 20, 28, 0xFFD3D3D3);
    draw_rect(offset_x + 20, offset_y + item_y + 10, 16, 10, 0xFFB0B0B0);
    draw_string(offset_x + 60, offset_y + item_y + 15, "Mouse", COLOR_DARK_TEXT);
    draw_string(offset_x + 60, offset_y + item_y + 35, "Pointer settings", COLOR_DKGRAY);
}

static void control_panel_paint_wallpaper(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button (rounded) - padded lower to avoid title bar
    draw_rounded_rect_filled(offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    draw_string(offset_x, offset_y + 40, "Presets:", COLOR_DARK_TEXT);
    
    // Color buttons (rounded) - 30% wider
    int button_y = offset_y + 65;
    int button_x = offset_x;
    
    // Coffee button
    draw_rounded_rect_filled(button_x, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 8, button_y + 6, 18, 13, COLOR_COFFEE);
    draw_string(button_x + 35, button_y + 8, "Coffee", COLOR_DARK_TEXT);
    
    // Teal button
    draw_rounded_rect_filled(button_x + 100, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 108, button_y + 6, 18, 13, COLOR_TEAL);
    draw_string(button_x + 135, button_y + 8, "Teal", COLOR_DARK_TEXT);
    
    // Green button
    draw_rounded_rect_filled(button_x + 200, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 208, button_y + 6, 18, 13, COLOR_GREEN);
    draw_string(button_x + 235, button_y + 8, "Green", COLOR_DARK_TEXT);
    
    // Blue button
    button_y += 35;
    draw_rounded_rect_filled(button_x, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 8, button_y + 6, 18, 13, COLOR_BLUE_BG);
    draw_string(button_x + 35, button_y + 8, "Blue", COLOR_DARK_TEXT);
    
    // Purple button
    draw_rounded_rect_filled(button_x + 100, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 108, button_y + 6, 18, 13, COLOR_PURPLE);
    draw_string(button_x + 132, button_y + 8, "Purple", COLOR_DARK_TEXT);
    
    // Grey button
    draw_rounded_rect_filled(button_x + 200, button_y, 91, 25, 6, COLOR_DARK_PANEL);
    draw_rect(button_x + 208, button_y + 6, 18, 13, COLOR_GREY);
    draw_string(button_x + 235, button_y + 8, "Grey", COLOR_DARK_TEXT);
    
    // Pattern section
    button_y += 40;
    draw_string(offset_x, button_y, "Patterns:", COLOR_DARK_TEXT);
    
    button_y += 20;
    
    // Lumberjack pattern button - 20% wider
    draw_rounded_rect_filled(button_x, button_y, 132, 25, 6, COLOR_DARK_PANEL);
    // Draw small pattern preview (3x3 repeating)
    for (int py = 0; py < 10; py++) {
        for (int px = 0; px < 12; px++) {
            int cell_x = px % 3;
            int cell_y = py % 3;
            uint32_t color = (cell_x == 1 && cell_y == 1) ? 0xFF000000 : 
                           (cell_x == 1 || cell_y == 1) ? 0xFF404040 : 0xFFDC143C;
            draw_rect(button_x + 8 + px, button_y + 7 + py, 1, 1, color);
        }
    }
    draw_string(button_x + 28, button_y + 8, "Lumberjack", COLOR_DARK_TEXT);
    
    // Blue Diamond pattern button - 20% wider
    draw_rounded_rect_filled(button_x + 145, button_y, 132, 25, 6, COLOR_DARK_PANEL);
    // Draw small diamond preview
    for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 10; px++) {
            int cx = px - 5;
            int cy = py - 4;
            int abs_cx = cx < 0 ? -cx : cx;
            int abs_cy = cy < 0 ? -cy : cy;
            uint32_t color = (abs_cx + abs_cy <= 3) ? 0xFF0000CD : 0xFFADD8E6;
            draw_rect(button_x + 153 + px, button_y + 8 + py, 1, 1, color);
        }
    }
    draw_string(button_x + 165, button_y + 8, "Blue Diamond", COLOR_DARK_TEXT);
    
    // Custom color section
    button_y += 40;
    draw_string(offset_x, button_y, "Custom color:", COLOR_DARK_TEXT);
    
    button_y += 20;
    
    // R input box (dark mode)
    draw_string(button_x, button_y + 4, "R:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(button_x + 25, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    draw_string(button_x + 30, button_y + 4, rgb_r, (focused_field == 0) ? 0xFFFF6B6B : COLOR_DARK_TEXT);
    if (focused_field == 0) {
        int cursor_x = button_x + 30 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 4, 1, 9, 0xFFFF6B6B);
    }
    
    // G input box (dark mode)
    draw_string(button_x + 90, button_y + 4, "G:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(button_x + 115, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    draw_string(button_x + 120, button_y + 4, rgb_g, (focused_field == 1) ? 0xFF90EE90 : COLOR_DARK_TEXT);
    if (focused_field == 1) {
        int cursor_x = button_x + 120 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 4, 1, 9, 0xFF90EE90);
    }
    
    // B input box (dark mode)
    draw_string(button_x + 180, button_y + 4, "B:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(button_x + 205, button_y, 50, 18, 4, COLOR_DARK_PANEL);
    draw_string(button_x + 210, button_y + 4, rgb_b, (focused_field == 2) ? 0xFF87CEEB : COLOR_DARK_TEXT);
    if (focused_field == 2) {
        int cursor_x = button_x + 210 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 4, 1, 9, 0xFF87CEEB);
    }
    
    // Apply button (rounded)
    draw_rounded_rect_filled(button_x, button_y + 25, 70, 25, 6, COLOR_DARK_PANEL);
    draw_string(button_x + 18, button_y + 33, "Apply", COLOR_DARK_TEXT);
    
    // Wallpaper Images section
    button_y += 60;
    draw_string(offset_x, button_y, "Wallpapers:", COLOR_DARK_TEXT);
    
    button_y += 20;
    
    // Draw Moon thumbnail (pre-generated at init time)
    uint32_t *moon_thumb = wallpaper_get_thumb(0);
    draw_rounded_rect_filled(button_x, button_y, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 24, 6, COLOR_DARK_PANEL);
    if (moon_thumb && wallpaper_thumb_valid(0)) {
        for (int ty = 0; ty < WALLPAPER_THUMB_H; ty++) {
            for (int tx = 0; tx < WALLPAPER_THUMB_W; tx++) {
                put_pixel(button_x + 4 + tx, button_y + 4 + ty, moon_thumb[ty * WALLPAPER_THUMB_W + tx]);
            }
        }
    } else {
        draw_string(button_x + 20, button_y + 30, "Error", 0xFFFF4444);
    }
    draw_string(button_x + 30, button_y + WALLPAPER_THUMB_H + 8, "Moon", COLOR_DARK_TEXT);
    
    // Draw Mountain thumbnail
    uint32_t *mtn_thumb = wallpaper_get_thumb(1);
    int thumb2_x = button_x + WALLPAPER_THUMB_W + 20;
    draw_rounded_rect_filled(thumb2_x, button_y, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 24, 6, COLOR_DARK_PANEL);
    if (mtn_thumb && wallpaper_thumb_valid(1)) {
        for (int ty = 0; ty < WALLPAPER_THUMB_H; ty++) {
            for (int tx = 0; tx < WALLPAPER_THUMB_W; tx++) {
                put_pixel(thumb2_x + 4 + tx, button_y + 4 + ty, mtn_thumb[ty * WALLPAPER_THUMB_W + tx]);
            }
        }
    } else {
        draw_string(thumb2_x + 20, button_y + 30, "Error", 0xFFFF4444);
    }
    draw_string(thumb2_x + 16, button_y + WALLPAPER_THUMB_H + 8, "Mountain", COLOR_DARK_TEXT);
}

static void draw_input_box(int x, int y, int width, const char *text, bool focused, int cursor_pos) {
    // Draw box
    draw_rect(x, y, width, 18, 0xFFFFFFFF);
    draw_rect(x, y, width, 1, COLOR_BLACK);
    draw_rect(x, y, 1, 18, COLOR_BLACK);
    draw_rect(x + width - 1, y, 1, 18, COLOR_BLACK);
    draw_rect(x, y + 17, width, 1, COLOR_BLACK);
    
    // Draw text
    uint32_t text_color = focused ? 0xFF0000FF : COLOR_BLACK;
    draw_string(x + 3, y + 4, text, text_color);
    
    // Draw cursor if focused
    if (focused) {
        int cursor_x = x + 3 + cursor_pos * 8;
        draw_rect(cursor_x, y + 4, 1, 9, 0xFF0000FF);
    }
}

static void control_panel_paint_network(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button (rounded) - padded lower to avoid title bar
    draw_rounded_rect_filled(offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    
    // Network Init Button
    draw_string(offset_x, offset_y + 40, "Network:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(offset_x, offset_y + 55, 140, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 30, offset_y + 63, "Init Network", COLOR_DARK_TEXT);
    
    // Status message
    if (net_status[0] != '\0') {
        draw_string(offset_x + 150, offset_y + 63, net_status, 0xFF90EE90);
    }
    
    // Set IP Section
    int section_y = offset_y + 85;
    draw_string(offset_x, section_y, "Set Static IP:", COLOR_DARK_TEXT);
    
    section_y += 20;
    // IP input boxes (4 octets, dark mode rounded) - with cursor indicators
    uint32_t ip1_color = (focused_field == 0) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 5, section_y + 4, ip_1, ip1_color);
    if (focused_field == 0) draw_rect(offset_x + 5 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 40, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t ip2_color = (focused_field == 1) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 50, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 55, section_y + 4, ip_2, ip2_color);
    if (focused_field == 1) draw_rect(offset_x + 55 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 90, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t ip3_color = (focused_field == 2) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 100, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 105, section_y + 4, ip_3, ip3_color);
    if (focused_field == 2) draw_rect(offset_x + 105 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 140, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t ip4_color = (focused_field == 3) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 150, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 155, section_y + 4, ip_4, ip4_color);
    if (focused_field == 3) draw_rect(offset_x + 155 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    
    // Apply IP button (rounded)
    draw_rounded_rect_filled(offset_x + 200, section_y, 70, 20, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 218, section_y + 4, "Apply", COLOR_DARK_TEXT);
    
    // Send UDP Section
    section_y += 30;
    draw_string(offset_x, section_y, "Send UDP Message:", COLOR_DARK_TEXT);
    
    section_y += 20;
    draw_string(offset_x, section_y + 4, "IP:", COLOR_DARK_TEXT);
    uint32_t dip1_color = (focused_field == 4) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 25, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 30, section_y + 4, dest_ip_1, dip1_color);
    if (focused_field == 4) draw_rect(offset_x + 30 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 65, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t dip2_color = (focused_field == 5) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 70, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 75, section_y + 4, dest_ip_2, dip2_color);
    if (focused_field == 5) draw_rect(offset_x + 75 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 110, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t dip3_color = (focused_field == 6) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 115, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 120, section_y + 4, dest_ip_3, dip3_color);
    if (focused_field == 6) draw_rect(offset_x + 120 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    draw_string(offset_x + 155, section_y + 4, ".", COLOR_DARK_TEXT);
    
    uint32_t dip4_color = (focused_field == 7) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 160, section_y, 35, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 165, section_y + 4, dest_ip_4, dip4_color);
    if (focused_field == 7) draw_rect(offset_x + 165 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    
    section_y += 25;
    draw_string(offset_x, section_y + 4, "Port:", COLOR_DARK_TEXT);
    uint32_t port_color = (focused_field == 8) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 40, section_y, 60, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 45, section_y + 4, udp_port, port_color);
    if (focused_field == 8) draw_rect(offset_x + 45 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    
    section_y += 25;
    draw_string(offset_x, section_y + 4, "Msg:", COLOR_DARK_TEXT);
    uint32_t msg_color = (focused_field == 9) ? 0xFF4A90E2 : COLOR_DARK_TEXT;
    draw_rounded_rect_filled(offset_x + 40, section_y, 180, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 45, section_y + 4, udp_message, msg_color);
    if (focused_field == 9) draw_rect(offset_x + 45 + input_cursor * 8, section_y + 4, 1, 9, 0xFF4A90E2);
    
    // Send button (rounded)
    section_y += 25;
    draw_rounded_rect_filled(offset_x, section_y, 80, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 22, section_y + 7, "Send", COLOR_DARK_TEXT);
}


static void control_panel_paint_desktop(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button (rounded) - padded lower to avoid title bar
    draw_rounded_rect_filled(offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    draw_string(offset_x, offset_y + 40, "Desktop Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    
    // Snap to Grid checkbox (rounded)
    draw_rounded_rect_filled(offset_x, section_y, 16, 16, 3, COLOR_DARK_PANEL);
    if (desktop_snap_to_grid) draw_string(offset_x + 3, section_y + 1, "✓", 0xFF90EE90);
    draw_string(offset_x + 25, section_y + 3, "Snap to Grid", COLOR_DARK_TEXT);
    
    // Auto Align checkbox (rounded)
    section_y += 25;
    draw_rounded_rect_filled(offset_x, section_y, 16, 16, 3, COLOR_DARK_PANEL);
    if (desktop_auto_align) draw_string(offset_x + 3, section_y + 1, "✓", 0xFF90EE90);
    draw_string(offset_x + 25, section_y + 3, "Auto Align Icons", COLOR_DARK_TEXT);
    
    // Max Rows
    section_y += 30;
    draw_string(offset_x, section_y + 3, "Apps per column:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(offset_x + 130, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    char num[4]; num[0] = '0' + (desktop_max_rows_per_col / 10); num[1] = '0' + (desktop_max_rows_per_col % 10); num[2] = 0;
    if (num[0] == '0') { num[0] = num[1]; num[1] = 0; }
    draw_string(offset_x + 160, section_y + 5, num, COLOR_DARK_TEXT);
    draw_rounded_rect_filled(offset_x + 180, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
    
    // Max Cols
    section_y += 30;
    draw_string(offset_x, section_y + 3, "Columns:", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(offset_x + 130, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    char num_c[4]; num_c[0] = '0' + (desktop_max_cols / 10); num_c[1] = '0' + (desktop_max_cols % 10); num_c[2] = 0;
    if (num_c[0] == '0') { num_c[0] = num_c[1]; num_c[1] = 0; }
    draw_string(offset_x + 160, section_y + 5, num_c, COLOR_DARK_TEXT);
    draw_rounded_rect_filled(offset_x + 180, section_y, 20, 20, 4, COLOR_DARK_PANEL);
    draw_string(offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
}


static void control_panel_paint_mouse(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button (rounded) - padded lower to avoid title bar
    draw_rounded_rect_filled(offset_x, offset_y + 5, 80, 25, 6, COLOR_DARK_PANEL);
    draw_string(offset_x + 10, offset_y + 13, "< Back", COLOR_DARK_TEXT);
    draw_string(offset_x, offset_y + 40, "Mouse Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    draw_string(offset_x, section_y, "Speed:", COLOR_DARK_TEXT);
    
    // Slider track (rounded background)
    draw_rounded_rect_filled(offset_x + 60, section_y + 8, 200, 8, 4, COLOR_DARK_PANEL);
    
    // Slider knob (range 1-50, default 10) - rounded with blue color
    int knob_x = offset_x + 60 + (mouse_speed - 1) * 190 / 49;
    draw_rounded_rect_filled(knob_x, section_y + 2, 10, 14, 3, 0xFF4A90E2);
    
    draw_string(offset_x + 270, section_y + 4, "x", COLOR_DARK_TEXT);
    char speed_str[4];
    cli_itoa(mouse_speed, speed_str);
    draw_string(offset_x + 280, section_y + 4, speed_str, COLOR_DARK_TEXT);
}

static void control_panel_paint(Window *win) {
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

static void control_panel_handle_click(Window *win, int x, int y) {
    (void)win;
    
    if (current_view == VIEW_MAIN) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Settings items layout: each item is 60px tall with 10px spacing
        int item_h = 60;
        int item_spacing = 10;
        
        // Check wallpaper button click (entire card area)
        int item_y = offset_y + 15;
        if (x >= offset_x && x < win->w - 8 &&
            y >= item_y && y < item_y + item_h) {
            current_view = VIEW_WALLPAPER;
            focused_field = -1;
        }
        
        // Check network button click
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win->w - 8 &&
            y >= item_y && y < item_y + item_h) {
            current_view = VIEW_NETWORK;
            focused_field = -1;
        }
        
        // Check desktop button
        item_y += item_h + item_spacing;
        if (x >= offset_x && x < win->w - 8 &&
            y >= item_y && y < item_y + item_h) {
            current_view = VIEW_DESKTOP;
        }

        // Check mouse button
        item_y += item_h + item_spacing;
        if (offset_y + item_y + item_h < win->y + win->h &&
            x >= offset_x && x < win->w - 8 &&
            y >= item_y && y < item_y + item_h) {
            current_view = VIEW_MOUSE;
        }
    } else if (current_view == VIEW_WALLPAPER) {
        int offset_x = 8;
        int offset_y = 30;
        int button_y = offset_y + 65;
        int button_x = offset_x;
        
        // Back button
        if (x >= offset_x && x < offset_x + 80 &&
            y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        // Check Coffee button (91px wide)
        if (x >= button_x && x < button_x + 91 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_COFFEE);
            wm_refresh();
            return;
        }
        
        // Check Teal button
        if (x >= button_x + 100 && x < button_x + 191 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_TEAL);
            wm_refresh();
            return;
        }
        
        // Check Green button
        if (x >= button_x + 200 && x < button_x + 291 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_GREEN);
            wm_refresh();
            return;
        }
        
        // Check Blue button
        button_y += 35;
        if (x >= button_x && x < button_x + 91 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_BLUE_BG);
            wm_refresh();
            return;
        }
        
        // Check Purple button
        if (x >= button_x + 100 && x < button_x + 191 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_PURPLE);
            wm_refresh();
            return;
        }
        
        // Check Grey button
        if (x >= button_x + 200 && x < button_x + 291 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_color(COLOR_GREY);
            wm_refresh();
            return;
        }
        
        // Pattern section
        button_y += 40;
        button_y += 20;
        
        // Check Lumberjack pattern button (132px wide)
        if (x >= button_x && x < button_x + 132 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_pattern(pattern_lumberjack);
            wm_refresh();
            return;
        }
        
        // Check Blue Diamond pattern button (132px wide)
        if (x >= button_x + 145 && x < button_x + 277 && y >= button_y && y < button_y + 25) {
            graphics_set_bg_pattern(pattern_blue_diamond);
            wm_refresh();
            return;
        }
        
        // Custom RGB section
        button_y += 40;
        button_y += 20;
        
        // Check R input box click
        if (x >= button_x + 25 && x < button_x + 75 && y >= button_y && y < button_y + 18) {
            if (focused_field != 0) {
                rgb_r[0] = '\0';  // Clear when first focused
            }
            focused_field = 0;
            input_cursor = 0;
            return;
        }
        
        // Check G input box click
        if (x >= button_x + 115 && x < button_x + 165 && y >= button_y && y < button_y + 18) {
            if (focused_field != 1) {
                rgb_g[0] = '\0';  // Clear when first focused
            }
            focused_field = 1;
            input_cursor = 0;
            return;
        }
        
        // Check B input box click
        if (x >= button_x + 205 && x < button_x + 255 && y >= button_y && y < button_y + 18) {
            if (focused_field != 2) {
                rgb_b[0] = '\0';  // Clear when first focused
            }
            focused_field = 2;
            input_cursor = 0;
            return;
        }
        
        // Check Apply button
        if (x >= button_x && x < button_x + 70 && y >= button_y + 25 && y < button_y + 50) {
            graphics_set_bg_color(parse_rgb_separate(rgb_r, rgb_g, rgb_b));
            wm_refresh();
            return;
        }
        
        // Wallpaper image thumbnails section
        button_y += 60;
        button_y += 20;
        
        // Check Moon thumbnail click
        if (x >= button_x && x < button_x + WALLPAPER_THUMB_W + 8 && y >= button_y && y < button_y + WALLPAPER_THUMB_H + 24) {
            wallpaper_request_set(0);
            return;
        }
        
        // Check Mountain thumbnail click
        int thumb2_x = button_x + WALLPAPER_THUMB_W + 20;
        if (x >= thumb2_x && x < thumb2_x + WALLPAPER_THUMB_W + 8 && y >= button_y && y < button_y + WALLPAPER_THUMB_H + 24) {
            wallpaper_request_set(1);
            return;
        }
    } else if (current_view == VIEW_NETWORK) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Back button
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            focused_field = -1;
            return;
        }
        
        // Init Network button (140px wide now)
        if (x >= offset_x && x < offset_x + 140 && y >= offset_y + 55 && y < offset_y + 80) {
            int result = network_init();
            if (result == 0) {
                net_status[0] = 'I'; net_status[1] = 'n'; net_status[2] = 'i'; 
                net_status[3] = 't'; net_status[4] = 'e'; net_status[5] = 'd';
                net_status[6] = '\0';
            } else {
                net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i'; 
                net_status[3] = 'l'; net_status[4] = 'e'; net_status[5] = 'd';
                net_status[6] = '\0';
            }
            return;
        }
        
        int section_y = offset_y + 85 + 20;
        
        // IP octet 1
        if (x >= offset_x && x < offset_x + 35 && y >= section_y && y < section_y + 20) {
            focused_field = 0;
            input_cursor = 0;
            return;
        }
        // IP octet 2
        if (x >= offset_x + 50 && x < offset_x + 85 && y >= section_y && y < section_y + 20) {
            focused_field = 1;
            input_cursor = 0;
            return;
        }
        // IP octet 3
        if (x >= offset_x + 100 && x < offset_x + 135 && y >= section_y && y < section_y + 20) {
            focused_field = 2;
            input_cursor = 0;
            return;
        }
        // IP octet 4
        if (x >= offset_x + 150 && x < offset_x + 185 && y >= section_y && y < section_y + 20) {
            focused_field = 3;
            input_cursor = 0;
            return;
        }
        
        // Apply IP button
        if (x >= offset_x + 200 && x < offset_x + 270 && y >= section_y && y < section_y + 20) {
            ipv4_address_t ip;
            ip.bytes[0] = 0; ip.bytes[1] = 0; ip.bytes[2] = 0; ip.bytes[3] = 0;
            
            // Parse IP octets
            for (int i = 0; ip_1[i] && i < 3; i++) {
                if (ip_1[i] >= '0' && ip_1[i] <= '9') {
                    ip.bytes[0] = ip.bytes[0] * 10 + (ip_1[i] - '0');
                }
            }
            for (int i = 0; ip_2[i] && i < 3; i++) {
                if (ip_2[i] >= '0' && ip_2[i] <= '9') {
                    ip.bytes[1] = ip.bytes[1] * 10 + (ip_2[i] - '0');
                }
            }
            for (int i = 0; ip_3[i] && i < 3; i++) {
                if (ip_3[i] >= '0' && ip_3[i] <= '9') {
                    ip.bytes[2] = ip.bytes[2] * 10 + (ip_3[i] - '0');
                }
            }
            for (int i = 0; ip_4[i] && i < 3; i++) {
                if (ip_4[i] >= '0' && ip_4[i] <= '9') {
                    ip.bytes[3] = ip.bytes[3] * 10 + (ip_4[i] - '0');
                }
            }
            
            network_set_ipv4_address(&ip);
            net_status[0] = 'I'; net_status[1] = 'P'; net_status[2] = ' ';
            net_status[3] = 's'; net_status[4] = 'e'; net_status[5] = 't';
            net_status[6] = '\0';
            return;
        }
        
        section_y += 30;
        
        // Dest IP octets
        if (x >= offset_x + 25 && x < offset_x + 60 && y >= section_y && y < section_y + 20) {
            focused_field = 4;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 70 && x < offset_x + 105 && y >= section_y && y < section_y + 20) {
            focused_field = 5;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 115 && x < offset_x + 150 && y >= section_y && y < section_y + 20) {
            focused_field = 6;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 160 && x < offset_x + 195 && y >= section_y && y < section_y + 20) {
            focused_field = 7;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Port field
        if (x >= offset_x + 40 && x < offset_x + 100 && y >= section_y && y < section_y + 20) {
            focused_field = 8;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Message field
        if (x >= offset_x + 40 && x < offset_x + 220 && y >= section_y && y < section_y + 20) {
            focused_field = 9;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Send button
        if (x >= offset_x && x < offset_x + 80 && y >= section_y && y < section_y + 25) {
            ipv4_address_t dest_ip;
            dest_ip.bytes[0] = 0; dest_ip.bytes[1] = 0; dest_ip.bytes[2] = 0; dest_ip.bytes[3] = 0;
            
            // Parse dest IP
            for (int i = 0; dest_ip_1[i] && i < 3; i++) {
                if (dest_ip_1[i] >= '0' && dest_ip_1[i] <= '9') {
                    dest_ip.bytes[0] = dest_ip.bytes[0] * 10 + (dest_ip_1[i] - '0');
                }
            }
            for (int i = 0; dest_ip_2[i] && i < 3; i++) {
                if (dest_ip_2[i] >= '0' && dest_ip_2[i] <= '9') {
                    dest_ip.bytes[1] = dest_ip.bytes[1] * 10 + (dest_ip_2[i] - '0');
                }
            }
            for (int i = 0; dest_ip_3[i] && i < 3; i++) {
                if (dest_ip_3[i] >= '0' && dest_ip_3[i] <= '9') {
                    dest_ip.bytes[2] = dest_ip.bytes[2] * 10 + (dest_ip_3[i] - '0');
                }
            }
            for (int i = 0; dest_ip_4[i] && i < 3; i++) {
                if (dest_ip_4[i] >= '0' && dest_ip_4[i] <= '9') {
                    dest_ip.bytes[3] = dest_ip.bytes[3] * 10 + (dest_ip_4[i] - '0');
                }
            }
            
            // Parse port
            int port = 0;
            for (int i = 0; udp_port[i] && i < 5; i++) {
                if (udp_port[i] >= '0' && udp_port[i] <= '9') {
                    port = port * 10 + (udp_port[i] - '0');
                }
            }
            
            // Get message length
            int msg_len = 0;
            while (udp_message[msg_len] && msg_len < 127) msg_len++;
            
            if (msg_len > 0 && port > 0) {
                int result = udp_send_packet(&dest_ip, (uint16_t)port, 54321, udp_message, (size_t)msg_len);
                if (result == 0) {
                    net_status[0] = 'S'; net_status[1] = 'e'; net_status[2] = 'n';
                    net_status[3] = 't'; net_status[4] = '\0';
                } else {
                    net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i';
                    net_status[3] = 'l'; net_status[4] = '\0';
                }
            }
            return;
        }
    } else if (current_view == VIEW_DESKTOP) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Back button
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        int section_y = offset_y + 65;
        // Snap toggle - click on checkbox only
        if (x >= offset_x && x < offset_x + 16 && y >= section_y && y < section_y + 16) {
            desktop_snap_to_grid = !desktop_snap_to_grid;
            // If Snap is turned OFF, Auto Align must be OFF
            if (!desktop_snap_to_grid) {
                desktop_auto_align = false;
            }
            wm_refresh_desktop();
            return;
        }
        
        // Auto Align toggle - click on checkbox only
        section_y += 25;
        if (x >= offset_x && x < offset_x + 16 && y >= section_y && y < section_y + 16) {
            desktop_auto_align = !desktop_auto_align;
            // If Auto Align is turned ON, Snap must be ON
            if (desktop_auto_align) {
                desktop_snap_to_grid = true;
            }
            wm_refresh_desktop();
            return;
        }
        
        // Rows adjust
        section_y += 25;
        if (x >= offset_x + 130 && x < offset_x + 150 && y >= section_y && y < section_y + 20) {
            if (desktop_max_rows_per_col > 1) {
                if (desktop_max_cols * (desktop_max_rows_per_col - 1) < wm_get_desktop_icon_count()) {
                    wm_show_message("Error", "Cannot reduce rows: too many files!");
                } else {
                    desktop_max_rows_per_col--;
                    wm_refresh_desktop();
                }
            }
        }
        if (x >= offset_x + 180 && x < offset_x + 200 && y >= section_y && y < section_y + 20) {
            if (desktop_max_rows_per_col < 15) desktop_max_rows_per_col++;
            wm_refresh_desktop();
        }
        
        // Cols adjust
        section_y += 25;
        if (x >= offset_x + 130 && x < offset_x + 150 && y >= section_y && y < section_y + 20) {
            if (desktop_max_cols > 1) {
                if ((desktop_max_cols - 1) * desktop_max_rows_per_col < wm_get_desktop_icon_count()) {
                    wm_show_message("Error", "Cannot reduce cols: too many files!");
                } else {
                    desktop_max_cols--;
                    wm_refresh_desktop();
                }
            }
        }
        if (x >= offset_x + 180 && x < offset_x + 200 && y >= section_y && y < section_y + 20) {
            desktop_max_cols++;
            wm_refresh_desktop();
        }
    } else if (current_view == VIEW_MOUSE) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Back button
        if (x >= offset_x && x < offset_x + 80 && y >= offset_y + 5 && y < offset_y + 30) {
            current_view = VIEW_MAIN;
            return;
        }
        
        int section_y = offset_y + 65;
        // Slider interaction
        if (x >= offset_x + 60 && x <= offset_x + 260 && y >= section_y && y <= section_y + 20) {
            int new_speed = 1 + (x - (offset_x + 60)) * 49 / 200;
            if (new_speed < 1) new_speed = 1;
            if (new_speed > 50) new_speed = 50;
            mouse_speed = new_speed;
            return;
        }
    }
}

static void control_panel_handle_key(Window *win, char c) {
    (void)win;
    
    if (focused_field < 0) return;
    
    if (current_view == VIEW_WALLPAPER) {
        // Get the currently focused field buffer
        char *focused_buffer = NULL;
        int max_len = 3;  // RGB values are 0-255, max 3 digits
        
        if (focused_field == 0) {
            focused_buffer = rgb_r;
        } else if (focused_field == 1) {
            focused_buffer = rgb_g;
        } else if (focused_field == 2) {
            focused_buffer = rgb_b;
        } else {
            return;
        }
        
        if (c == '\b') {  // Backspace
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c >= '0' && c <= '9') {  // Digits only
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {  // Tab - switch to next field
            focused_field = (focused_field + 1) % 3;
            input_cursor = 0;
        }
    } else if (current_view == VIEW_NETWORK) {
        char *focused_buffer = NULL;
        int max_len = 3;  // Default for IP octets
        
        // Select the focused field
        if (focused_field == 0) {
            focused_buffer = ip_1;
        } else if (focused_field == 1) {
            focused_buffer = ip_2;
        } else if (focused_field == 2) {
            focused_buffer = ip_3;
        } else if (focused_field == 3) {
            focused_buffer = ip_4;
        } else if (focused_field == 4) {
            focused_buffer = dest_ip_1;
        } else if (focused_field == 5) {
            focused_buffer = dest_ip_2;
        } else if (focused_field == 6) {
            focused_buffer = dest_ip_3;
        } else if (focused_field == 7) {
            focused_buffer = dest_ip_4;
        } else if (focused_field == 8) {
            focused_buffer = udp_port;
            max_len = 5;  // Ports up to 65535
        } else if (focused_field == 9) {
            focused_buffer = udp_message;
            max_len = 127;  // Message max length
        } else {
            return;
        }
        
        if (c == '\b') {  // Backspace
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (focused_field <= 8 && c >= '0' && c <= '9') {  // Digits only for IP/port
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (focused_field == 9 && c >= 32 && c <= 126) {  // All printable chars for message
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {  // Tab - switch to next field
            focused_field = (focused_field + 1) % 10;
            input_cursor = 0;
        }
    }
}

void control_panel_init(void) {
    win_control_panel.title = "Settings";
    win_control_panel.x = 200;
    win_control_panel.y = 150;
    win_control_panel.w = 350;
    win_control_panel.h = 500;
    win_control_panel.visible = false;
    win_control_panel.focused = false;
    win_control_panel.z_index = 0;
    win_control_panel.paint = control_panel_paint;
    win_control_panel.handle_key = control_panel_handle_key;
    win_control_panel.handle_click = control_panel_handle_click;
    win_control_panel.handle_right_click = NULL;
    win_control_panel.buf_len = 0;
    win_control_panel.cursor_pos = 0;
    
    // Generate patterns
    generate_lumberjack_pattern();
    generate_blue_diamond_pattern();
}

void control_panel_reset(void) {
    win_control_panel.focused = false;
    current_view = VIEW_MAIN;
    focused_field = -1;
    input_cursor = 0;
}
