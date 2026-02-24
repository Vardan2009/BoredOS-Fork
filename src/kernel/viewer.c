// viewer.c - Image Viewer app for BoredOS
// Opens .jpg files and displays the decoded image in a window

#include "viewer.h"
#include "nanojpeg.h"
#include "graphics.h"
#include "fat32.h"
#include "memory_manager.h"
#include "wallpaper.h"
#include "io.h"
#include <stddef.h>

Window win_viewer;

// Viewer state
#define VIEWER_MAX_W 800
#define VIEWER_MAX_H 600
static uint32_t viewer_pixels[VIEWER_MAX_W * VIEWER_MAX_H];
static int viewer_img_w = 0;
static int viewer_img_h = 0;
static char viewer_title[64] = "Viewer";
static bool viewer_has_image = false;

// Deferred open: click handler stores path, main loop decodes
static char viewer_pending_path[256];
static volatile bool viewer_open_pending = false;

// Store the file path for "Set as Wallpaper"
static char viewer_file_path[256];

// String helpers
static int viewer_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void viewer_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

// Simple nearest-neighbor scale from decoded RGB to ARGB pixel buffer
static void viewer_scale_rgb_to_argb(const unsigned char *rgb, int src_w, int src_h,
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

static void viewer_paint(Window *win) {
    int cx = win->x + 4;
    int cy = win->y + 24;
    int cw = win->w - 8;
    int ch = win->h - 28;

    draw_rect(cx, cy, cw, ch, 0xFF1A1A1A);

    if (!viewer_has_image) {
        draw_string(cx + 20, cy + ch / 2, "No image loaded", 0xFF888888);
        return;
    }

    // Calculate display size (fit within window, keep aspect ratio)
    int disp_w = viewer_img_w;
    int disp_h = viewer_img_h;

    if (disp_w > cw - 8) {
        disp_h = disp_h * (cw - 8) / disp_w;
        disp_w = cw - 8;
    }
    if (disp_h > ch - 40) {
        disp_w = disp_w * (ch - 40) / disp_h;
        disp_h = ch - 40;
    }

    // Center in window
    int ox = cx + (cw - disp_w) / 2;
    int oy = cy + (ch - disp_h - 30) / 2;

    // Draw the image pixel by pixel (nearest-neighbor from stored buffer)
    for (int y = 0; y < disp_h; y++) {
        int src_y = y * viewer_img_h / disp_h;
        if (src_y >= viewer_img_h) src_y = viewer_img_h - 1;
        for (int x = 0; x < disp_w; x++) {
            int src_x = x * viewer_img_w / disp_w;
            if (src_x >= viewer_img_w) src_x = viewer_img_w - 1;
            uint32_t pixel = viewer_pixels[src_y * viewer_img_w + src_x];
            put_pixel(ox + x, oy + y, pixel);
        }
    }

    // Draw "Set as Wallpaper" button at the bottom
    int btn_w = 160;
    int btn_h = 22;
    int btn_x = cx + (cw - btn_w) / 2;
    int btn_y = win->y + win->h - 30;
    draw_rounded_rect_filled(btn_x, btn_y, btn_w, btn_h, 6, 0xFF2D2D2D);
    draw_string(btn_x + 10, btn_y + 6, "Set as Wallpaper", 0xFFF0F0F0);
}

static void viewer_handle_click(Window *win, int x, int y) {
    if (!viewer_has_image) return;

    int cx = 4;
    int cw = win->w - 8;

    // Check "Set as Wallpaper" button
    int btn_w = 160;
    int btn_x = cx + (cw - btn_w) / 2;
    int btn_y = win->h - 30;
    if (x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + 22) {
        // Queue wallpaper change from file (deferred to main loop)
        wallpaper_request_set_from_file(viewer_file_path);
    }
}

static void viewer_handle_key(Window *win, char c) {
    (void)win;
    (void)c;
}

// Simple serial output for debugging
static void v_serial_char(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));
    outb(0x3F8, c);
}
static void v_serial_str(const char *s) { while (*s) v_serial_char(*s++); }
static void v_serial_num(int n) {
    if (n < 0) { v_serial_char('-'); n = -n; }
    if (n >= 10) v_serial_num(n / 10);
    v_serial_char('0' + (n % 10));
}

// Called from interrupt context - just queue the path for later processing
void viewer_open_file(const char *path) {
    v_serial_str("[VIEWER] open_file queued: ");
    v_serial_str(path);
    v_serial_str("\n");
    viewer_strcpy(viewer_pending_path, path);
    viewer_open_pending = true;
}

// Process deferred viewer open (called from main loop, NOT interrupt context)
void viewer_process_pending(void) {
    if (!viewer_open_pending) return;
    viewer_open_pending = false;

    const char *path = viewer_pending_path;
    v_serial_str("[VIEWER] process_pending: ");
    v_serial_str(path);
    v_serial_str("\n");

    FAT32_FileHandle *fh = fat32_open(path, "r");
    if (!fh) {
        v_serial_str("[VIEWER] fat32_open FAILED\n");
        return;
    }

    uint32_t file_size = fh->size;
    v_serial_str("[VIEWER] file_size=");
    v_serial_num((int)file_size);
    v_serial_str("\n");

    if (file_size == 0 || file_size > 2 * 1024 * 1024) {
        v_serial_str("[VIEWER] file too big or empty\n");
        fat32_close(fh);
        return;
    }

    unsigned char *buf = (unsigned char*)kmalloc(file_size);
    if (!buf) {
        v_serial_str("[VIEWER] kmalloc FAILED\n");
        fat32_close(fh);
        return;
    }

    int total_read = 0;
    while (total_read < (int)file_size) {
        int chunk = fat32_read(fh, buf + total_read, (int)file_size - total_read);
        if (chunk <= 0) break;
        total_read += chunk;
    }
    fat32_close(fh);

    v_serial_str("[VIEWER] read ");
    v_serial_num(total_read);
    v_serial_str(" bytes\n");

    if (total_read <= 0) {
        kfree(buf);
        return;
    }

    // Decode JPEG (now running in main loop, safe context)
    njInit();
    nj_result_t result = njDecode(buf, total_read);
    v_serial_str("[VIEWER] njDecode returned: ");
    v_serial_num((int)result);
    v_serial_str("\n");

    if (result != NJ_OK) {
        njDone();
        kfree(buf);
        return;
    }

    int img_w = njGetWidth();
    int img_h = njGetHeight();
    unsigned char *rgb = njGetImage();

    v_serial_str("[VIEWER] decoded ");
    v_serial_num(img_w);
    v_serial_str("x");
    v_serial_num(img_h);
    v_serial_str("\n");

    if (!rgb || img_w <= 0 || img_h <= 0) {
        njDone();
        kfree(buf);
        return;
    }

    // Scale to fit viewer buffer
    int fit_w = img_w;
    int fit_h = img_h;
    if (fit_w > VIEWER_MAX_W) {
        fit_h = fit_h * VIEWER_MAX_W / fit_w;
        fit_w = VIEWER_MAX_W;
    }
    if (fit_h > VIEWER_MAX_H) {
        fit_w = fit_w * VIEWER_MAX_H / fit_h;
        fit_h = VIEWER_MAX_H;
    }

    viewer_scale_rgb_to_argb(rgb, img_w, img_h, viewer_pixels, fit_w, fit_h);
    viewer_img_w = fit_w;
    viewer_img_h = fit_h;
    viewer_has_image = true;

    njDone();
    kfree(buf);

    // Store the file path for "Set as Wallpaper"
    viewer_strcpy(viewer_file_path, path);

    // Update title - extract filename from path
    const char *fname = path;
    int plen = viewer_strlen(path);
    for (int i = plen - 1; i >= 0; i--) {
        if (path[i] == '/') {
            fname = &path[i + 1];
            break;
        }
    }
    viewer_title[0] = 'V'; viewer_title[1] = 'i'; viewer_title[2] = 'e';
    viewer_title[3] = 'w'; viewer_title[4] = 'e'; viewer_title[5] = 'r';
    viewer_title[6] = ' '; viewer_title[7] = '-'; viewer_title[8] = ' ';
    int ti = 9;
    for (int i = 0; fname[i] && ti < 60; i++) {
        viewer_title[ti++] = fname[i];
    }
    viewer_title[ti] = 0;

    // Resize window to fit image
    win_viewer.w = fit_w + 16;
    if (win_viewer.w < 200) win_viewer.w = 200;
    win_viewer.h = fit_h + 64;
    if (win_viewer.h < 100) win_viewer.h = 100;

    // Reset position to ensure visibility
    win_viewer.x = 100;
    win_viewer.y = 50;

    v_serial_str("[VIEWER] window: x=");
    v_serial_num(win_viewer.x);
    v_serial_str(" y=");
    v_serial_num(win_viewer.y);
    v_serial_str(" w=");
    v_serial_num(win_viewer.w);
    v_serial_str(" h=");
    v_serial_num(win_viewer.h);
    v_serial_str(" fit=");
    v_serial_num(fit_w);
    v_serial_str("x");
    v_serial_num(fit_h);
    v_serial_str("\n");

    // Show and bring to front
    win_viewer.visible = true;
    wm_bring_to_front(&win_viewer);

    v_serial_str("[VIEWER] z_index=");
    v_serial_num(win_viewer.z_index);
    v_serial_str(" visible=");
    v_serial_num(win_viewer.visible);
    v_serial_str(" focused=");
    v_serial_num(win_viewer.focused);
    v_serial_str("\n");
    v_serial_str("[VIEWER] window shown!\n");
}

void viewer_init(void) {
    win_viewer.title = viewer_title;
    viewer_title[0] = 'V'; viewer_title[1] = 'i'; viewer_title[2] = 'e';
    viewer_title[3] = 'w'; viewer_title[4] = 'e'; viewer_title[5] = 'r';
    viewer_title[6] = 0;
    win_viewer.x = 100;
    win_viewer.y = 50;
    win_viewer.w = 500;
    win_viewer.h = 400;
    win_viewer.visible = false;
    win_viewer.paint = viewer_paint;
    win_viewer.handle_click = viewer_handle_click;
    win_viewer.handle_key = viewer_handle_key;
    win_viewer.handle_right_click = NULL;
    win_viewer.data = NULL;
    // Window is registered directly in wm_init's all_windows array
    v_serial_str("[VIEWER] init done, win_viewer paint=");
    v_serial_num(win_viewer.paint != NULL);
    v_serial_str("\n");
}
