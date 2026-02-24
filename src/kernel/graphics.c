#include <stddef.h>
#include "graphics.h"
#include "font.h"
#include "io.h"

static struct limine_framebuffer *g_fb = NULL;
static uint32_t g_bg_color = 0xFF696969;  // Dark gray background

// Pattern support
#define PATTERN_SIZE 128
static uint32_t g_bg_pattern[PATTERN_SIZE * PATTERN_SIZE];
static bool g_use_pattern = false;

// Dirty rectangle tracking
static DirtyRect g_dirty = {0, 0, 0, 0, false};

// Double buffering - allocate a back buffer
// Max screen size: 2048x2048 @ 32bpp = 16MB, but allocate for common sizes
// Using a simple approach: allocate max size buffer
#define MAX_FB_WIDTH 2048
#define MAX_FB_HEIGHT 2048
static uint32_t g_back_buffer[MAX_FB_WIDTH * MAX_FB_HEIGHT] __attribute__((aligned(4096)));

// Clipping state
static int g_clip_x = 0, g_clip_y = 0, g_clip_w = 0, g_clip_h = 0;
static bool g_clip_enabled = false;

void graphics_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    g_dirty.active = false;
    // Initialize back buffer to black
    for (int i = 0; i < MAX_FB_WIDTH * MAX_FB_HEIGHT; i++) {
        g_back_buffer[i] = 0;
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

void put_pixel(int x, int y, uint32_t color) {
    if (!g_fb) return;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return;
    
    if (g_clip_enabled) {
        if (x < g_clip_x || x >= g_clip_x + g_clip_w ||
            y < g_clip_y || y >= g_clip_y + g_clip_h) {
            return;
        }
    }
    
    // Draw to back buffer
    uint32_t pixel_offset = y * g_fb->width + x;
    g_back_buffer[pixel_offset] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_fb) return;

    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
    
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
    
    // Draw rounded corners using scanline approach (fills gaps properly)
    for (int dy = 0; dy < radius; dy++) {
        // For top corners: distance formula inverted (narrow at top, wide at junction)
        int dx_top = isqrt(radius*radius - (radius - dy) * (radius - dy));
        
        // For bottom corners: distance formula normal (wide at junction, narrow at bottom)
        int dx_bottom = isqrt(radius*radius - dy*dy);
        
        // Top-left corner - horizontal scanline
        draw_rect(x + radius - dx_top, y + dy, dx_top, 1, color);
        
        // Top-right corner - horizontal scanline
        draw_rect(x + w - radius, y + dy, dx_top, 1, color);
        
        // Bottom-left corner - horizontal scanline
        draw_rect(x + radius - dx_bottom, y + h - radius + dy, dx_bottom, 1, color);
        
        // Bottom-right corner - horizontal scanline
        draw_rect(x + w - radius, y + h - radius + dy, dx_bottom, 1, color);
    }
}

void draw_char(int x, int y, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;

    // Fast rejection: if the character is entirely outside the clipping/dirty rect, skip it
    if (g_clip_enabled) {
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

void draw_string(int x, int y, const char *s, uint32_t color) {
    int cur_x = x;
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
    
    if (g_use_pattern) {
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
}

void graphics_set_bg_pattern(const uint32_t *pattern) {
    if (!pattern) return;
    
    // Copy pattern to internal buffer
    for (int i = 0; i < PATTERN_SIZE * PATTERN_SIZE; i++) {
        g_bg_pattern[i] = pattern[i];
    }
    g_use_pattern = true;
}

void draw_boredos_logo(int x, int y, int scale) {

    static const uint8_t brewos_bmp[] = {
        0,0,1,1,1,0,0,0,0,0,0,1,1,1,0,0, // 0: Ears
        0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0, // 1: Ears
        1,1,1,1,1,0,0,0,0,0,0,1,1,1,1,1, // 2: Ears (Separated)
        1,1,1,1,2,2,2,2,2,2,2,2,1,1,1,1, // 3: Forehead / Ears
        1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,1, // 4: Face
        1,1,2,2,2,1,1,2,2,1,1,2,2,2,1,1, // 5: Eyes start
        1,1,2,2,1,1,1,1,1,1,1,1,2,2,1,1, // 6: Eyes
        1,1,2,2,1,1,1,1,1,1,1,1,2,2,1,1, // 7: Eyes
        1,1,2,2,1,1,1,1,1,1,1,1,2,2,1,1, // 8: Eyes
        1,1,2,2,2,1,1,2,2,1,1,2,2,2,1,1, // 9: Under eyes
        1,1,2,2,2,2,2,1,1,2,2,2,2,2,1,1, // 10: Nose
        1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1, // 11: Cheeks
        1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,1, // 12: Jaw
        0,1,1,1,2,2,2,2,2,2,2,2,1,1,1,0, // 13: Chin
        0,0,1,1,1,2,2,2,2,2,2,1,1,1,0,0, // 14: Chin outline
        0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0  // 15: Bottom
    };

    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            uint8_t p = brewos_bmp[r * 16 + c];
            if (p == 1) {
                draw_rect(x + c * scale, y + r * scale, scale, scale, 0xFF1A1A1A); // rgb(26,26,26)
            } else if (p == 2) {
                draw_rect(x + c * scale, y + r * scale, scale, scale, 0xFFFEFEFE); // rgb(254,254,254)
            }
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

void graphics_set_clipping(int x, int y, int w, int h) {
    g_clip_x = x;
    g_clip_y = y;
    g_clip_w = w;
    g_clip_h = h;
    g_clip_enabled = true;
}

void graphics_clear_clipping(void) {
    g_clip_enabled = false;
}
