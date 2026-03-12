// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stddef.h>
#include "graphics.h"
#include "font.h"
#include "io.h"
#include "font_manager.h"

static struct limine_framebuffer *g_fb = NULL;
static uint32_t g_bg_color = 0xFF696969;

extern void serial_write(const char *str);

#define PATTERN_SIZE 128
static uint32_t g_bg_pattern[PATTERN_SIZE * PATTERN_SIZE];
static bool g_use_pattern = false;

static uint32_t *g_bg_image = NULL;
static int g_bg_image_w = 0;
static int g_bg_image_h = 0;
static bool g_use_image = false;

static DirtyRect g_dirty = {0, 0, 0, 0, false};


#define MAX_FB_WIDTH 2048
#define MAX_FB_HEIGHT 2048
static uint32_t g_back_buffer[MAX_FB_WIDTH * MAX_FB_HEIGHT] __attribute__((aligned(4096)));

static int g_clip_x = 0, g_clip_y = 0, g_clip_w = 0, g_clip_h = 0;
static bool g_clip_enabled = false;

static uint32_t *g_render_target = NULL;
static int g_rt_width = 0;
static int g_rt_height = 0;

static ttf_font_t *g_current_ttf = NULL;

void graphics_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    g_dirty.active = false;
    // Initialize back buffer to black
    for (int i = 0; i < MAX_FB_WIDTH * MAX_FB_HEIGHT; i++) {
        g_back_buffer[i] = 0;
    }
}

void graphics_init_fonts(void) {
    font_manager_init();
    g_current_ttf = font_manager_load("/Library/Fonts/firamono.ttf", 15.0f);
    if (!g_current_ttf) {
        serial_write("[FONT] Falling back to bitmap font\n");
    }
}

void graphics_set_font(const char *path) {
    ttf_font_t *new_font = font_manager_load(path, 15.0f);
    if (new_font) {
        // TODO: free old font data if needed
        g_current_ttf = new_font;
        serial_write("[FONT] Switched to: ");
        serial_write(path);
        serial_write("\n");
    }
}

int get_screen_width(void) {
    return g_fb ? g_fb->width : 0;
}

int get_screen_height(void) {
    return g_fb ? g_fb->height : 0;
}

// Merge new dirty rect with existing one
static void merge_dirty_rect(int x, int y, int w, int h) {
    if (!g_dirty.active) {
        g_dirty.x = x;
        g_dirty.y = y;
        g_dirty.w = w;
        g_dirty.h = h;
        g_dirty.active = true;
    } else {
        // Calculate union of two rectangles
        int x1 = g_dirty.x;
        int y1 = g_dirty.y;
        int x2 = g_dirty.x + g_dirty.w;
        int y2 = g_dirty.y + g_dirty.h;
        
        int new_x1 = x;
        int new_y1 = y;
        int new_x2 = x + w;
        int new_y2 = y + h;
        
        g_dirty.x = new_x1 < x1 ? new_x1 : x1;
        g_dirty.y = new_y1 < y1 ? new_y1 : y1;
        g_dirty.w = (new_x2 > x2 ? new_x2 : x2) - g_dirty.x;
        g_dirty.h = (new_y2 > y2 ? new_y2 : y2) - g_dirty.y;
    }
}

void graphics_mark_dirty(int x, int y, int w, int h) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    // Clamp to screen bounds
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > get_screen_width()) {
        w = get_screen_width() - x;
    }
    if (y + h > get_screen_height()) {
        h = get_screen_height() - y;
    }
    
    if (w <= 0 || h <= 0) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return;
    }
    
    merge_dirty_rect(x, y, w, h);
    asm volatile("push %0; popfq" : : "r"(rflags));
}

void graphics_mark_screen_dirty(void) {
    g_dirty.x = 0;
    g_dirty.y = 0;
    g_dirty.w = get_screen_width();
    g_dirty.h = get_screen_height();
    g_dirty.active = true;
}

DirtyRect graphics_get_dirty_rect(void) {
    return g_dirty;
}

void graphics_clear_dirty(void) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    g_dirty.active = false;
    asm volatile("push %0; popfq" : : "r"(rflags));
}

void graphics_set_render_target(uint32_t *buffer, int w, int h) {
    g_render_target = buffer;
    g_rt_width = w;
    g_rt_height = h;
}

void put_pixel(int x, int y, uint32_t color) {
    if (g_render_target) {
        if (x >= 0 && x < g_rt_width && y >= 0 && y < g_rt_height) {
            g_render_target[y * g_rt_width + x] = color;
        }
        return;
    }

    if (!g_fb) return;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return;
    
    if (g_clip_enabled) {
        if (x < g_clip_x || x >= g_clip_x + g_clip_w ||
            y < g_clip_y || y >= g_clip_y + g_clip_h) {
            return;
        }
    }
    
    uint32_t pixel_offset = y * g_fb->width + x;
    g_back_buffer[pixel_offset] = color;
}

uint32_t graphics_get_pixel(int x, int y) {
    if (g_render_target) {
        if (x >= 0 && x < g_rt_width && y >= 0 && y < g_rt_height) {
            return g_render_target[y * g_rt_width + x];
        }
        return 0;
    }

    if (!g_fb) return 0;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return 0;
    
    return g_back_buffer[y * g_fb->width + x];
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;

    if (g_render_target) {
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > g_rt_width) x2 = g_rt_width;
        if (y2 > g_rt_height) y2 = g_rt_height;
        if (x1 >= x2 || y1 >= y2) return;
        
        for (int i = y1; i < y2; i++) {
            uint32_t *row = &g_render_target[i * g_rt_width + x1];
            int len = x2 - x1;
            for (int j = 0; j < len; j++) {
                row[j] = color;
            }
        }
        return;
    }

    if (!g_fb) return;
    
    if (g_clip_enabled) {
        if (x1 < g_clip_x) x1 = g_clip_x;
        if (y1 < g_clip_y) y1 = g_clip_y;
        if (x2 > g_clip_x + g_clip_w) x2 = g_clip_x + g_clip_w;
        if (y2 > g_clip_y + g_clip_h) y2 = g_clip_y + g_clip_h;
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)g_fb->width) x2 = g_fb->width;
    if (y2 > (int)g_fb->height) y2 = g_fb->height;

    if (x1 >= x2 || y1 >= y2) return;

    for (int i = y1; i < y2; i++) {
        uint32_t *row = &g_back_buffer[i * g_fb->width + x1];
        int len = x2 - x1;
        for (int j = 0; j < len; j++) {
            row[j] = color;
        }
    }
}

// Simple integer-based square root approximation
static int isqrt(int n) {
    if (n < 0) return 0;
    if (n == 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Draw rounded rectangle outline
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 1) radius = 1;
    
    // Draw top and bottom edges
    draw_rect(x + radius, y, w - 2*radius, 1, color);
    draw_rect(x + radius, y + h - 1, w - 2*radius, 1, color);
    
    // Draw left and right edges
    draw_rect(x, y + radius, 1, h - 2*radius, color);
    draw_rect(x + w - 1, y + radius, 1, h - 2*radius, color);
    
    // Draw corner circles using integer approximation
    for (int i = 0; i < radius; i++) {
        int j = isqrt(radius*radius - i*i);
        
        // Top-left corner
        put_pixel(x + radius - i - 1, y + radius - j, color);
        // Top-right corner
        put_pixel(x + w - radius + i, y + radius - j, color);
        // Bottom-left corner
        put_pixel(x + radius - i - 1, y + h - radius + j - 1, color);
        // Bottom-right corner
        put_pixel(x + w - radius + i, y + h - radius + j - 1, color);
    }
}

// Draw filled rounded rectangle
void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 1) radius = 1;
    
    // Draw main rectangle body (center part without corners)
    draw_rect(x + radius, y, w - 2*radius, h, color);
    draw_rect(x, y + radius, radius, h - 2*radius, color);
    draw_rect(x + w - radius, y + radius, radius, h - 2*radius, color);
    
    for (int dy = 0; dy < radius; dy++) {
        int dx_top = isqrt(radius*radius - (radius - dy) * (radius - dy));
        
        int dx_bottom = isqrt(radius*radius - dy*dy);
        
        draw_rect(x + radius - dx_top, y + dy, dx_top, 1, color);
        
        draw_rect(x + w - radius, y + dy, dx_top, 1, color);
        
        draw_rect(x + radius - dx_bottom, y + h - radius + dy, dx_bottom, 1, color);
        
        draw_rect(x + w - radius, y + h - radius + dy, dx_bottom, 1, color);
    }
}

void draw_char(int x, int y, char c, uint32_t color) {
    if (g_current_ttf) {
        font_manager_render_char(g_current_ttf, x, y, c, color, put_pixel);
        return;
    }

    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;

    if (g_clip_enabled && !g_render_target) {
        if (x + 8 <= g_clip_x || x >= g_clip_x + g_clip_w ||
            y + 8 <= g_clip_y || y >= g_clip_y + g_clip_h) {
            return;
        }
    }

    const uint8_t *glyph = font8x8_basic[uc];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

// Bitmap-only version for terminal — always uses 8x8 bitmap font regardless of TTF
void draw_char_bitmap(int x, int y, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;

    if (g_clip_enabled && !g_render_target) {
        if (x + 8 <= g_clip_x || x >= g_clip_x + g_clip_w ||
            y + 8 <= g_clip_y || y >= g_clip_y + g_clip_h) {
            return;
        }
    }

    const uint8_t *glyph = font8x8_basic[uc];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

ttf_font_t *graphics_get_current_ttf(void) {
    return g_current_ttf;
}

void draw_string_bitmap(int x, int y, const char *str, uint32_t color) {
    const char *s = str;
    int cur_x = x;
    int cur_y = y;
    while (*s) {
        if (*s == '\n') {
            cur_x = x;
            cur_y += 10;
        } else {
            draw_char_bitmap(cur_x, cur_y, *s, color);
            cur_x += 8;
        }
        s++;
    }
}

int graphics_get_font_height(void) {
    if (g_current_ttf) {
        return (int)((g_current_ttf->ascent - g_current_ttf->descent) * g_current_ttf->scale);
    }
    return 10; // Fallback bitmap height
}

int graphics_get_font_height_scaled(float scale) {
    if (g_current_ttf) {
        return font_manager_get_font_height_scaled(g_current_ttf, scale);
    }
    return 10; // Fallback bitmap height
}

int graphics_get_string_width_scaled(const char *s, float scale) {
    if (g_current_ttf) {
        return font_manager_get_string_width_scaled(g_current_ttf, s, scale);
    }
    int len = 0;
    while (s && s[len]) len++;
    return len * 8; // Fallback bitmap width
}

void draw_string(int x, int y, const char *s, uint32_t color) {
    if (g_current_ttf) draw_string_scaled(x, y, s, color, g_current_ttf->pixel_height);
    else draw_string_scaled(x, y, s, color, 15.0f);
}

void draw_string_scaled(int x, int y, const char *s, uint32_t color, float scale) {
    if (!s) return;
    int cur_x = x;
    
    if (g_current_ttf) {
        // We let the font manager handle the stbtt scale internally to avoid bringing stb_truetype into graphics.c
        int baseline = y + font_manager_get_font_ascent_scaled(g_current_ttf, scale) - 2;
        int line_height = font_manager_get_font_line_height_scaled(g_current_ttf, scale);
        
        while (*s) {
            if (*s == '\n') {
                cur_x = x;
                baseline += line_height;
            } else {
                font_manager_render_char_scaled(g_current_ttf, cur_x, baseline, *s, color, scale, put_pixel);
                // Advance by same rounded width that font_manager_get_string_width uses
                char buf[2] = {*s, 0};
                cur_x += font_manager_get_string_width_scaled(g_current_ttf, buf, scale);
            }
            s++;
        }
        return;
    }

    int cur_y = y;
    while (*s) {
        if (*s == '\n') {
            cur_x = x;
            cur_y += 10;
        } else {
            draw_char(cur_x, cur_y, *s, color);
            cur_x += 8;
        }
        s++;
    }
}

void draw_desktop_background(void) {
    if (!g_fb) return;
    
    if (g_use_image && g_bg_image) {
        // Draw wallpaper image (stretch/scale to screen)
        int x1 = 0, y1 = 0, x2 = g_fb->width, y2 = g_fb->height;
        if (g_clip_enabled) {
            x1 = g_clip_x; y1 = g_clip_y;
            x2 = g_clip_x + g_clip_w; y2 = g_clip_y + g_clip_h;
        }
        for (int y = y1; y < y2; y++) {
            int src_y = y * g_bg_image_h / (int)g_fb->height;
            if (src_y >= g_bg_image_h) src_y = g_bg_image_h - 1;
            uint32_t *row = &g_back_buffer[y * g_fb->width + x1];
            for (int x = x1; x < x2; x++) {
                int src_x = x * g_bg_image_w / (int)g_fb->width;
                if (src_x >= g_bg_image_w) src_x = g_bg_image_w - 1;
                *row++ = g_bg_image[src_y * g_bg_image_w + src_x];
            }
        }
    } else if (g_use_pattern) {
        // Optimized tiled pattern: only draw within the clipping/dirty rect
        int x1 = 0, y1 = 0, x2 = g_fb->width, y2 = g_fb->height;
        if (g_clip_enabled) {
            x1 = g_clip_x; y1 = g_clip_y;
            x2 = g_clip_x + g_clip_w; y2 = g_clip_y + g_clip_h;
        }

        for (int y = y1; y < y2; y++) {
            uint32_t *row = &g_back_buffer[y * g_fb->width + x1];
            int py = y % PATTERN_SIZE;
            for (int x = x1; x < x2; x++) {
                *row++ = g_bg_pattern[py * PATTERN_SIZE + (x % PATTERN_SIZE)];
            }
        }
    } else {
        // Draw solid color
        draw_rect(0, 0, g_fb->width, g_fb->height, g_bg_color);
    }
}

void graphics_set_bg_color(uint32_t color) {
    g_bg_color = color;
    g_use_pattern = false;
    g_use_image = false;
}

void graphics_set_bg_pattern(const uint32_t *pattern) {
    if (!pattern) return;
    
    // Copy pattern to internal buffer
    for (int i = 0; i < PATTERN_SIZE * PATTERN_SIZE; i++) {
        g_bg_pattern[i] = pattern[i];
    }
    g_use_pattern = true;
    g_use_image = false;
}

void graphics_set_bg_image(uint32_t *pixels, int w, int h) {
    g_bg_image = pixels;
    g_bg_image_w = w;
    g_bg_image_h = h;
    g_use_image = true;
    g_use_pattern = false;
}

void draw_boredos_logo(int x, int y, int scale) {
    // Width: 60, Height: 16
    // 1: Magenta, 2: Blue, 3: Cyan, 4: White, 0: Deadspace
    static const uint8_t boredos_bmp[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    const int bmp_w = 60;
    const int bmp_h = 16;

    for (int r = 0; r < bmp_h; r++) {
        for (int c = 0; c < bmp_w; c++) {
            uint8_t p = boredos_bmp[r * bmp_w + c];
            if (p == 0) continue;

            uint32_t color = 0;
            switch(p) {
                case 1: color = 0xFFB589D6; break; // Magenta
                case 2: color = 0xFF569CD6; break; // Blue
                case 3: color = 0xFF4EC9B0; break; // Cyan
                case 4: color = 0xFFFFFFFF; break; // White
            }
            
            draw_rect(x + (c * scale), y + (r * scale), scale, scale, color);
        }
    }
}

// Double buffering functions
void graphics_clear_back_buffer(uint32_t color) {
    if (!g_fb) return;
    uint32_t *buf = g_back_buffer;
    for (int i = 0; i < (int)g_fb->width * (int)g_fb->height; i++) {
        *buf++ = color;
    }
}

void graphics_flip_buffer(void) {
    if (!g_fb || !g_dirty.active) return;

    int x = g_dirty.x;
    int y = g_dirty.y;
    int w = g_dirty.w;
    int h = g_dirty.h;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g_fb->width) w = g_fb->width - x;
    if (y + h > (int)g_fb->height) h = g_fb->height - y;

    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        int curr_y = y + i;
        uint32_t *src_row = &g_back_buffer[curr_y * g_fb->width + x];
        uint32_t *dst_row = (uint32_t *)((uint8_t *)g_fb->address + curr_y * g_fb->pitch) + x;
        for (int j = 0; j < w; j++) {
            dst_row[j] = src_row[j];
        }
    }
}

void graphics_copy_screenbuffer(uint32_t *dest) {
    if (!g_fb || !dest) return;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    int sw = g_fb->width;
    int sh = g_fb->height;
    
    // Copy the internal back object to the dest directly
    for (int y = 0; y < sh; y++) {
        uint32_t *src_row = &g_back_buffer[y * sw];
        for (int x = 0; x < sw; x++) {
            dest[y * sw + x] = src_row[x];
        }
    }
    asm volatile("push %0; popfq" : : "r"(rflags));
}

void graphics_set_clipping(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    int sw = get_screen_width();
    int sh = get_screen_height();
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    g_clip_x = x;
    g_clip_y = y;
    g_clip_w = w;
    g_clip_h = h;
    g_clip_enabled = true;
}

void graphics_clear_clipping(void) {
    g_clip_enabled = false;
}
void graphics_blit_buffer(uint32_t *src, int dst_x, int dst_y, int w, int h) {
    if (!g_fb || !src) return;
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    for (int y = 0; y < h; y++) {
        int vy = dst_y + y;
        if (vy < 0 || vy >= sh) continue;
        
        for (int x = 0; x < w; x++) {
            int vx = dst_x + x;
            if (vx < 0 || vx >= sw) continue;
            
            uint32_t pcol = src[y * w + x];

            if ((pcol & 0xFF000000) != 0 || (pcol & 0xFFFFFF) != 0) {
                g_back_buffer[vy * sw + vx] = pcol;
            }
        }
    }
}
