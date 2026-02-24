// wallpaper.c - Wallpaper management for BoredOS
#include "wallpaper.h"
#include "nanojpeg.h"
#include "graphics.h"
#include "fat32.h"
#include "memory_manager.h"
#include "wallpaper_data.h"
#include "wm.h"
#include "io.h"
#include <stddef.h>

// Static buffer for the current wallpaper (max 1920x1080)
#define MAX_WP_WIDTH 1920
#define MAX_WP_HEIGHT 1080
static uint32_t wp_pixels[MAX_WP_WIDTH * MAX_WP_HEIGHT];
static int wp_width = 0;
static int wp_height = 0;

// Pre-generated thumbnails
static uint32_t thumb_moon[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
static uint32_t thumb_mountain[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
static bool thumbs_valid[WALLPAPER_COUNT] = {false, false};

// Deferred wallpaper action (set from interrupt context, processed in main loop)
static volatile int pending_wallpaper_index = -1;
static volatile const char *pending_wallpaper_path = NULL;
static char pending_path_buf[256];

const char *wallpaper_names[WALLPAPER_COUNT] = {
    "Moon",
    "Mountain"
};

// Simple nearest-neighbor scale from decoded RGB to ARGB pixel buffer
static void scale_rgb_to_argb(const unsigned char *rgb, int src_w, int src_h,
                              uint32_t *dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
        int src_y = y * src_h / dst_h;
        if (src_y >= src_h) src_y = src_h - 1;
        for (int x = 0; x < dst_w; x++) {
            int src_x = x * src_w / dst_w;
            if (src_x >= src_w) src_x = src_w - 1;
            int idx = (src_y * src_w + src_x) * 3;
            unsigned char r = rgb[idx];
            unsigned char g = rgb[idx + 1];
            unsigned char b = rgb[idx + 2];
            dst[y * dst_w + x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

// Decode JPEG data from memory and set as wallpaper (MUST be called from non-interrupt context)
static int decode_and_set_wallpaper(const unsigned char *jpg_data, unsigned int jpg_size) {
    njInit();
    nj_result_t result = njDecode(jpg_data, (int)jpg_size);
    if (result != NJ_OK) {
        njDone();
        return 0;
    }

    int img_w = njGetWidth();
    int img_h = njGetHeight();
    unsigned char *rgb = njGetImage();

    if (!rgb || img_w <= 0 || img_h <= 0) {
        njDone();
        return 0;
    }

    // Scale to screen size
    int screen_w = get_screen_width();
    int screen_h = get_screen_height();
    if (screen_w > MAX_WP_WIDTH) screen_w = MAX_WP_WIDTH;
    if (screen_h > MAX_WP_HEIGHT) screen_h = MAX_WP_HEIGHT;

    scale_rgb_to_argb(rgb, img_w, img_h, wp_pixels, screen_w, screen_h);
    wp_width = screen_w;
    wp_height = screen_h;

    njDone();

    graphics_set_bg_image(wp_pixels, wp_width, wp_height);
    return 1;
}

// Simple serial output for debugging (COM1 = 0x3F8)
static void serial_char(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));  // Wait for transmit ready
    outb(0x3F8, c);
}

static void serial_str(const char *s) {
    while (*s) serial_char(*s++);
}

static void serial_num(int n) {
    if (n < 0) { serial_char('-'); n = -n; }
    if (n >= 10) serial_num(n / 10);
    serial_char('0' + (n % 10));
}

static void serial_hex(unsigned int n) {
    const char hex[] = "0123456789ABCDEF";
    serial_str("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_char(hex[(n >> i) & 0xF]);
    }
}

// Decode JPEG and generate a thumbnail (MUST be called from non-interrupt context)
static int decode_thumbnail(const unsigned char *jpg_data, unsigned int jpg_size,
                            uint32_t *out_pixels, int thumb_w, int thumb_h) {
    serial_str("[WP] decode_thumbnail: data=");
    serial_hex((unsigned int)(unsigned long)jpg_data);
    serial_str(" size=");
    serial_num((int)jpg_size);
    serial_str(" first bytes: ");
    for (int i = 0; i < 4 && i < (int)jpg_size; i++) {
        serial_hex(jpg_data[i]);
        serial_char(' ');
    }
    serial_str("\n");

    njInit();
    serial_str("[WP] njInit done, calling njDecode...\n");

    nj_result_t result = njDecode(jpg_data, (int)jpg_size);

    serial_str("[WP] njDecode returned: ");
    serial_num((int)result);
    serial_str("\n");

    if (result != NJ_OK) {
        njDone();
        // Fill with error indicator color based on error code
        uint32_t err_color;
        switch (result) {
            case NJ_NO_JPEG:      err_color = 0xFF880000; break; // dark red
            case NJ_UNSUPPORTED:  err_color = 0xFF888800; break; // dark yellow
            case NJ_OUT_OF_MEM:   err_color = 0xFF008800; break; // dark green
            case NJ_INTERNAL_ERR: err_color = 0xFF000088; break; // dark blue
            case NJ_SYNTAX_ERROR: err_color = 0xFF880088; break; // dark magenta
            default:              err_color = 0xFF444444; break; // grey
        }
        for (int i = 0; i < thumb_w * thumb_h; i++) {
            out_pixels[i] = err_color;
        }
        return 1;  // Return 1 (valid) so the colored diagnostic is visible!
    }

    int img_w = njGetWidth();
    int img_h = njGetHeight();
    unsigned char *rgb = njGetImage();

    serial_str("[WP] decoded: ");
    serial_num(img_w);
    serial_str("x");
    serial_num(img_h);
    serial_str("\n");

    if (!rgb || img_w <= 0 || img_h <= 0) {
        njDone();
        for (int i = 0; i < thumb_w * thumb_h; i++) {
            out_pixels[i] = 0xFF444444;
        }
        return 1;  // visible
    }

    scale_rgb_to_argb(rgb, img_w, img_h, out_pixels, thumb_w, thumb_h);
    njDone();
    return 1;
}

// Request wallpaper change by index (safe to call from interrupt context)
void wallpaper_request_set(int index) {
    pending_wallpaper_index = index;
}

// Request wallpaper change by file path (safe to call from interrupt context)
void wallpaper_request_set_from_file(const char *path) {
    // Copy path to buffer
    int i = 0;
    while (path[i] && i < 255) {
        pending_path_buf[i] = path[i];
        i++;
    }
    pending_path_buf[i] = 0;
    pending_wallpaper_path = pending_path_buf;
}

// Process deferred wallpaper actions (called from main loop, NOT interrupt context)
void wallpaper_process_pending(void) {
    if (pending_wallpaper_index >= 0) {
        int idx = pending_wallpaper_index;
        pending_wallpaper_index = -1;

        const unsigned char *data = NULL;
        unsigned int size = 0;
        if (idx == 0) {
            data = wallpaper_moon_jpg;
            size = wallpaper_moon_jpg_len;
        } else if (idx == 1) {
            data = wallpaper_mountain_jpg;
            size = wallpaper_mountain_jpg_len;
        }
        if (data) {
            decode_and_set_wallpaper(data, size);
            wm_refresh();
        }
    }

    if (pending_wallpaper_path) {
        const char *path = (const char *)pending_wallpaper_path;
        pending_wallpaper_path = NULL;

        // Read file from filesystem
        FAT32_FileHandle *fh = fat32_open(path, "r");
        if (fh) {
            uint32_t file_size = fh->size;
            if (file_size > 0 && file_size <= 2 * 1024 * 1024) {
                unsigned char *buf = (unsigned char*)kmalloc(file_size);
                if (buf) {
                    int total_read = 0;
                    while (total_read < (int)file_size) {
                        int chunk = fat32_read(fh, buf + total_read, (int)file_size - total_read);
                        if (chunk <= 0) break;
                        total_read += chunk;
                    }
                    fat32_close(fh);

                    if (total_read > 0) {
                        decode_and_set_wallpaper(buf, (unsigned int)total_read);
                        wm_refresh();
                    }
                    kfree(buf);
                } else {
                    fat32_close(fh);
                }
            } else {
                fat32_close(fh);
            }
        }
    }
}

// Get pre-generated thumbnail data
uint32_t* wallpaper_get_thumb(int index) {
    if (index == 0) return thumb_moon;
    if (index == 1) return thumb_mountain;
    return NULL;
}

bool wallpaper_thumb_valid(int index) {
    if (index < 0 || index >= WALLPAPER_COUNT) return false;
    return thumbs_valid[index];
}

uint32_t* wallpaper_get_pixels(void) { return wp_pixels; }
int wallpaper_get_width(void) { return wp_width; }
int wallpaper_get_height(void) { return wp_height; }

void wallpaper_init(void) {
    // Create /Wallpapers directory
    fat32_mkdir("/Wallpapers");

    // Write moon.jpg to /Wallpapers/moon.jpg
    if (!fat32_exists("/Wallpapers/moon.jpg")) {
        FAT32_FileHandle *fh = fat32_open("/Wallpapers/moon.jpg", "w");
        if (fh) {
            fat32_write(fh, wallpaper_moon_jpg, wallpaper_moon_jpg_len);
            fat32_close(fh);
        }
    }

    // Write mountain.jpg to /Wallpapers/mountain.jpg
    if (!fat32_exists("/Wallpapers/mountain.jpg")) {
        FAT32_FileHandle *fh = fat32_open("/Wallpapers/mountain.jpg", "w");
        if (fh) {
            fat32_write(fh, wallpaper_mountain_jpg, wallpaper_mountain_jpg_len);
            fat32_close(fh);
        }
    }

    // Pre-generate thumbnails at boot time (non-interrupt context!)
    thumbs_valid[0] = decode_thumbnail(wallpaper_moon_jpg, wallpaper_moon_jpg_len,
                                       thumb_moon, WALLPAPER_THUMB_W, WALLPAPER_THUMB_H);
    thumbs_valid[1] = decode_thumbnail(wallpaper_mountain_jpg, wallpaper_mountain_jpg_len,
                                       thumb_mountain, WALLPAPER_THUMB_W, WALLPAPER_THUMB_H);

    // Set mountain.jpg as the default wallpaper
    decode_and_set_wallpaper(wallpaper_mountain_jpg, wallpaper_mountain_jpg_len);
}
