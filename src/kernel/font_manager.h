#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// stb_truetype math stubs
extern float ksqrtf(float x);
extern float kpowf(float b, float e);
extern float kfmodf(float x, float y);
extern float kcosf(float x);
extern float kacosf(float x);
extern float kfabsf(float x);

#define STBTT_ifloor(x) ((int)(x))
#define STBTT_iceil(x)  ((int)(x + 0.999999f))
#define STBTT_sqrt(x)   ksqrtf(x)
#define STBTT_pow(x,y)  kpowf(x,y)
#define STBTT_fmod(x,y) kfmodf(x,y)
#define STBTT_cos(x)    kcosf(x)
#define STBTT_acos(x)   kacosf(x)
#define STBTT_fabs(x)   kfabsf(x)

#define STBTT_assert(x) ((void)0)

// Memory management
#define STBTT_malloc(x,u)  kmalloc(x)
#define STBTT_free(x,u)    kfree(x)

// String functions
#define STBTT_memcpy mem_memcpy
#define STBTT_memset mem_memset

// Data types
typedef uint64_t STBTT_ptrsize;

typedef struct {
    void *data;
    size_t size;
    void *info; // stbtt_fontinfo
    float scale;
    float pixel_height;
    int ascent;
    int descent;
    int line_gap;
} ttf_font_t;

bool font_manager_init(void);
ttf_font_t* font_manager_load(const char *path, float size);
void font_manager_render_char(ttf_font_t *font, int x, int y, char c, uint32_t color, void (*put_pixel_fn)(int, int, uint32_t));
void font_manager_render_char_scaled(ttf_font_t *font, int x, int y, char c, uint32_t color, float scale, void (*put_pixel_fn)(int, int, uint32_t));
int font_manager_get_string_width(ttf_font_t *font, const char *s);
int font_manager_get_string_width_scaled(ttf_font_t *font, const char *s, float scale);

int font_manager_get_font_height_scaled(ttf_font_t *font, float scale);
int font_manager_get_font_ascent_scaled(ttf_font_t *font, float scale);
int font_manager_get_font_line_height_scaled(ttf_font_t *font, float scale);

#endif
