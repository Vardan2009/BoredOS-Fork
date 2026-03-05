#define STB_TRUETYPE_IMPLEMENTATION
#include "memory_manager.h"
#include "font_manager.h"
#include "stb_truetype.h"
#include "fat32.h"
#include <stddef.h>

// Simple math implementations for stb_truetype
float ksqrtf(float x) {
    float res;
    asm volatile("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float kfabsf(float x) {
    return (x < 0) ? -x : x;
}

float kpowf(float b, float e) {
    // Very simplified pow for stb_truetype's needs
    if (e == 0) return 1.0f;
    if (e == 1) return b;
    if (e == 0.5f) return ksqrtf(b);
    
    // Fallback/log-based would be complex, let's see if this suffices
    float res = 1.0f;
    for (int i = 0; i < (int)e; i++) res *= b;
    return res;
}

float kfmodf(float x, float y) {
    return x - (int)(x / y) * y;
}

float kcosf(float x) {
    // Taylor series for cos(x) around 0
    float x2 = x * x;
    return 1.0f - (x2 / 2.0f) + (x2 * x2 / 24.0f) - (x2 * x2 * x2 / 720.0f);
}

float kacosf(float x) {
    // Very rough approximation for acos(x)
    if (x >= 1.0f) return 0;
    if (x <= -1.0f) return 3.14159f;
    return 1.57079f - x - (x*x*x)/6.0f;
}

extern void serial_write(const char *s);
extern uint32_t graphics_get_pixel(int x, int y);

static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    
    uint32_t rb = (((fg & 0xFF00FF) * alpha) + ((bg & 0xFF00FF) * (255 - alpha))) >> 8;
    uint32_t g = (((fg & 0x00FF00) * alpha) + ((bg & 0x00FF00) * (255 - alpha))) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static ttf_font_t *default_font = NULL;

#define FONT_CACHE_SIZE 2048
typedef struct {
    char c;
    float pixel_height;
    int w, h, xoff, yoff;
    unsigned char *bitmap;
} font_cache_entry_t;

// Cache is disabled for now due to race conditions and collisions
// static font_cache_entry_t g_font_cache[FONT_CACHE_SIZE];

bool font_manager_init(void) {
    // We'll load a default font later if available
    return true;
}

ttf_font_t* font_manager_load(const char *path, float size) {
    FAT32_FileHandle *fh = fat32_open(path, "r");
    if (!fh || !fh->valid) {
        serial_write("[FONT] Failed to open font file: ");
        serial_write(path);
        serial_write("\n");
        return NULL;
    }

    uint32_t fsize = fh->size;
    unsigned char *buffer = kmalloc(fsize);
    if (!buffer) {
        fat32_close(fh);
        return NULL;
    }

    int read = fat32_read(fh, buffer, fsize);
    fat32_close(fh);

    ttf_font_t *font = kmalloc(sizeof(ttf_font_t));
    if (!font) {
        kfree(buffer);
        return NULL;
    }

    stbtt_fontinfo *info = kmalloc(sizeof(stbtt_fontinfo));
    if (!info) {
        kfree(buffer);
        kfree(font);
        return NULL;
    }

    if (!stbtt_InitFont(info, buffer, 0)) {
        serial_write("[FONT] Failed to init font: ");
        serial_write(path);
        serial_write("\n");
        kfree(info);
        kfree(buffer);
        kfree(font);
        return NULL;
    }

    font->data = buffer;
    font->size = fsize;
    font->info = info;
    font->pixel_height = size;
    font->scale = stbtt_ScaleForPixelHeight(info, size);
    
    stbtt_GetFontVMetrics(info, &font->ascent, &font->descent, &font->line_gap);

    if (!default_font) default_font = font;

    return font;
}

void font_manager_render_char(ttf_font_t *font, int x, int y, char c, uint32_t color, void (*put_pixel_fn)(int, int, uint32_t)) {
    if (!font) font = default_font;
    if (!font) return;
    font_manager_render_char_scaled(font, x, y, c, color, font->pixel_height, put_pixel_fn);
}

void font_manager_render_char_scaled(ttf_font_t *font, int x, int y, char c, uint32_t color, float scale, void (*put_pixel_fn)(int, int, uint32_t)) {
    if (!font) font = default_font;
    if (!font) return;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    
    unsigned char *bitmap = NULL;
    int w, h, xoff, yoff;
    
    float real_scale = stbtt_ScaleForPixelHeight(info, scale); // Convert pixel size back to stbtt scale
    
    int codepoint = (unsigned char)c;
    if (codepoint == 128) codepoint = 0x2014; // Unicode emdash
    
    bitmap = stbtt_GetCodepointBitmap(info, 0, real_scale, codepoint, &w, &h, &xoff, &yoff);

    if (bitmap) {
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                unsigned char alpha = bitmap[row * w + col];
                if (alpha > 0) {
                    int px = x + col + xoff;
                    int py = y + row + yoff;
                    uint32_t bg = graphics_get_pixel(px, py);
                    put_pixel_fn(px, py, alpha_blend(bg, color, alpha));
                }
            }
        }
        stbtt_FreeBitmap(bitmap, NULL);
    }
}

int font_manager_get_string_width(ttf_font_t *font, const char *s) {
    if (!font) font = default_font;
    if (!font) return 0;
    return font_manager_get_string_width_scaled(font, s, font->pixel_height);
}

int font_manager_get_font_height_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)((font->ascent - font->descent) * real_scale);
}

int font_manager_get_font_ascent_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)(font->ascent * real_scale);
}

int font_manager_get_font_line_height_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)((font->ascent - font->descent + font->line_gap) * real_scale);
}

int font_manager_get_string_width_scaled(ttf_font_t *font, const char *s, float scale) {
    if (!font) font = default_font;
    if (!font || !s) return 0;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float real_scale = stbtt_ScaleForPixelHeight(info, scale);
    int width = 0;
    while (*s) {
        int advance, lsb;
        int codepoint = (unsigned char)*s;
        if (codepoint == 128) codepoint = 0x2014; // Unicode emdash
        stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
        // Round per-character to match draw_string's accumulation
        width += (int)(advance * real_scale + 0.5f);
        s++;
    }
    return width;
}
