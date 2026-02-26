#include "nanojpeg.h"
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIEWER_MAX_W 800
#define VIEWER_MAX_H 600

static uint32_t *viewer_pixels = NULL;
static int viewer_img_w = 0;
static int viewer_img_h = 0;
static char viewer_title[64] = "Viewer";
static bool viewer_has_image = false;
static char viewer_file_path[256];

static int win_w = 500;
static int win_h = 400;

static void viewer_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static int viewer_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

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

static void viewer_paint(ui_window_t win) {
    int cx = 4;
    int cy = 24;
    int cw = win_w - 8;
    int ch = win_h - 28;

    ui_draw_rect(win, cx, cy, cw, ch, 0xFF1A1A1A);

    if (!viewer_has_image) {
        ui_draw_string(win, cx + 20, cy + ch / 2, "No image loaded", 0xFF888888);
        return;
    }

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

    int ox = cx + (cw - disp_w) / 2;
    int oy = cy + (ch - disp_h - 30) / 2;

    for (int y = 0; y < disp_h; y++) {
        int src_y = y * viewer_img_h / disp_h;
        if (src_y >= viewer_img_h) src_y = viewer_img_h - 1;
        for (int x = 0; x < disp_w; x++) {
            int src_x = x * viewer_img_w / disp_w;
            if (src_x >= viewer_img_w) src_x = viewer_img_w - 1;
            uint32_t pixel = viewer_pixels[src_y * viewer_img_w + src_x];
            ui_draw_rect(win, ox + x, oy + y, 1, 1, pixel);
        }
    }

    int btn_w = 160;
    int btn_h = 22;
    int btn_x = cx + (cw - btn_w) / 2;
    int btn_y = win_h - 30;
    ui_draw_rounded_rect_filled(win, btn_x, btn_y, btn_w, btn_h, 6, 0xFF2D2D2D);
    ui_draw_string(win, btn_x + 10, btn_y + 6, "Set as Wallpaper", 0xFFF0F0F0);
}

static void viewer_handle_click(ui_window_t win, int x, int y) {
    if (!viewer_has_image) return;

    int cx = 4;
    int cw = win_w - 8;
    int btn_w = 160;
    int btn_x = cx + (cw - btn_w) / 2;
    int btn_y = win_h - 30;
    
    if (x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + 22) {
        // SYSTEM_CMD_SET_WALLPAPER is 3 based on syscall.c code
        sys_system(3, (uint64_t)viewer_file_path, 0, 0, 0);
    }
}

void viewer_open_file(const char *path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return;

    // We can't use stat yet, so read chunks until EOF
    // Alternatively, use a large buffer if sys_read handles large chunks.
    int alloc_size = 2 * 1024 * 1024;
    unsigned char *buf = malloc(alloc_size);
    if (!buf) {
        sys_close(fd);
        return;
    }

    int total_read = 0;
    while (total_read < alloc_size) {
        int chunk = sys_read(fd, (char*)buf + total_read, alloc_size - total_read);
        if (chunk <= 0) break;
        total_read += chunk;
    }
    sys_close(fd);

    if (total_read <= 0) {
        free(buf);
        return;
    }

    njInit();
    nj_result_t result = njDecode(buf, total_read);
    if (result != NJ_OK) {
        njDone();
        free(buf);
        return;
    }

    int img_w = njGetWidth();
    int img_h = njGetHeight();
    unsigned char *rgb = njGetImage();

    if (!rgb || img_w <= 0 || img_h <= 0) {
        njDone();
        free(buf);
        return;
    }

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

    viewer_pixels = malloc(fit_w * fit_h * sizeof(uint32_t));
    if (viewer_pixels) {
        viewer_scale_rgb_to_argb(rgb, img_w, img_h, viewer_pixels, fit_w, fit_h);
        viewer_img_w = fit_w;
        viewer_img_h = fit_h;
        viewer_has_image = true;
    }

    njDone();
    free(buf);

    viewer_strcpy(viewer_file_path, path);

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

    win_w = fit_w + 16;
    if (win_w < 200) win_w = 200;
    win_h = fit_h + 64;
    if (win_h < 100) win_h = 100;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        viewer_open_file(argv[1]);
    }

    ui_window_t win = ui_window_create(viewer_title, 100, 50, win_w, win_h);
    if (!win) return 1;

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                viewer_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h);
            } else if (ev.type == GUI_EVENT_CLICK) {
                viewer_handle_click(win, ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }
    }
    return 0;
}
