// wallpaper.h - Wallpaper management for BoredOS
#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <stdint.h>
#include <stdbool.h>

// Initialize wallpaper subsystem (creates /Wallpapers dir, writes JPEGs, pre-generates thumbnails)
void wallpaper_init(void);

// Request wallpaper change by embedded index (safe from interrupt context)
void wallpaper_request_set(int index);

// Request wallpaper change by file path (safe from interrupt context)
void wallpaper_request_set_from_file(const char *path);

// Process pending wallpaper actions (call from main loop only!)
void wallpaper_process_pending(void);

// Get pre-generated thumbnail pixel buffer (index 0=moon, 1=mountain)
#define WALLPAPER_THUMB_W 100
#define WALLPAPER_THUMB_H 60
uint32_t* wallpaper_get_thumb(int index);
bool wallpaper_thumb_valid(int index);

// Wallpaper info
#define WALLPAPER_COUNT 2
extern const char *wallpaper_names[WALLPAPER_COUNT];

// Get decoded wallpaper pixel buffer
uint32_t* wallpaper_get_pixels(void);
int wallpaper_get_width(void);
int wallpaper_get_height(void);

#endif // WALLPAPER_H
