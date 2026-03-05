// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall.h"
#include "gdt.h"
#include "memory_manager.h"
#include "gui_ipc.h"
#include "process.h"
#include "wm.h"
#include "fat32.h"
#include "paging.h"
#include "platform.h"
#include "io.h"
#include "pci.h"
#include "kutils.h"
#include "network.h"
#include "icmp.h"
#include "cmd.h"
#include "font_manager.h"
#include "graphics.h"

// Read MSR
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// Implemented in assembly
extern void syscall_entry(void);

extern uint64_t kernel_syscall_stack;

void syscall_init(void) {
    void* stack = kmalloc(16384);
    kernel_syscall_stack = (uint64_t)stack + 16384;
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1; // SCE bit is bit 0
    wrmsr(MSR_EFER, efer);

    // STAR MSR setup:
    // Bits 32-47: Syscall CS and SS. CS = STAR[47:32], SS = STAR[47:32] + 8 (Kernel CS = 0x08)
    // Bits 48-63: Sysret CS and SS. CS = STAR[63:48] + 16, SS = STAR[63:48] + 8.
    // User Data must be Base+8, User Code must be Base+16.
    // Our GDT: User Data = 0x1B, User Code = 0x23. 
    // Therefore Base = 0x13.
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // Mask Interrupts on SYSCALL (Clear IF bit in RFLAGS during syscall execution)
    wrmsr(MSR_FMASK, 0x200);
}

static void user_window_close(Window *win) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_CLOSE };
    process_push_gui_event(proc, &ev);
}

static void user_window_paint(Window *win) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_PAINT };
    process_push_gui_event(proc, &ev);
}

static void user_window_click(Window *win, int x, int y) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_CLICK, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_right_click(Window *win, int x, int y) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_RIGHT_CLICK, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_down(Window *win, int x, int y) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_DOWN, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_up(Window *win, int x, int y) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_UP, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_move(Window *win, int x, int y, uint8_t buttons) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_MOVE, .arg1 = x, .arg2 = y, .arg3 = buttons };
    process_push_gui_event(proc, &ev);
}

// Helper function for WM to send mouse events
void syscall_send_mouse_move_event(Window *win, int x, int y, uint8_t buttons) {
    if (!win || !win->data) return;
    user_window_mouse_move(win, x, y, buttons);
}

void syscall_send_mouse_down_event(Window *win, int x, int y) {
    if (!win || !win->data) return;
    user_window_mouse_down(win, x, y);
}

void syscall_send_mouse_up_event(Window *win, int x, int y) {
    if (!win || !win->data) return;
    user_window_mouse_up(win, x, y);
}

static void user_window_key(Window *win, char c, bool pressed) {
    process_t *proc = (process_t *)win->data;
    if (!proc) return;
    gui_event_t ev = { .type = pressed ? GUI_EVENT_KEY : GUI_EVENT_KEYUP, .arg1 = (int)c };
    process_push_gui_event(proc, &ev);
}

static void user_window_resize(Window *win, int w, int h) {
    if (!win) return;
    if (w <= 0 || h <= 0) return;
    
    extern void* kmalloc(size_t size);
    extern void kfree(void* ptr);
    extern void serial_write(const char *str);
    
    
    if (win->pixels) kfree(win->pixels);
    if (win->comp_pixels) kfree(win->comp_pixels);
    
    win->pixels = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    win->comp_pixels = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    
    if (win->pixels) {
        for (int i = 0; i < w * h; i++) win->pixels[i] = 0;
    }
}


static uint64_t syscall_handler_inner(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    extern void cmd_write(const char *str);
    extern void serial_write(const char *str);
    
    if (syscall_num == 1) { // SYS_WRITE
        extern void cmd_write_len(const char *str, size_t len);
        cmd_write_len((const char*)arg2, (size_t)arg3);
    } else if (syscall_num == 3) { // SYS_GUI
        int cmd = (int)arg1;
        process_t *proc = process_get_current();
        
        if (cmd == GUI_CMD_WINDOW_CREATE) {
            extern void serial_write(const char *str);
            serial_write("Kernel: GUI_CMD_WINDOW_CREATE\n");
            
            const char *title = (const char *)arg2;
            uint64_t *u_params = (uint64_t *)arg3;
            if (!u_params) {
                serial_write("Kernel: Error - params is NULL\n");
                return 0;
            }

            // Copy params from user space to kernel space for safety
            uint64_t params[4];
            for (int i = 0; i < 4; i++) params[i] = u_params[i];
            
            serial_write("Kernel: Window params copied.\n");
            
            Window *win = kmalloc(sizeof(Window));
            if (!win) {
                serial_write("Kernel: Error - kmalloc failed for Window\n");
                return 0;
            }
            serial_write("Kernel: Window allocated.\n");
            
            extern void mem_memset(void *dest, int val, size_t len);
            mem_memset(win, 0, sizeof(Window));

            // Copy title from user space to kernel space so wm.c can access it safely
            int title_len = 0;
            if (title) {
                while (title[title_len] && title_len < 255) title_len++;
            }
            
            char *kernel_title = kmalloc(title_len + 1);
            if (kernel_title) {
                for (int i = 0; i < title_len; i++) {
                    kernel_title[i] = title[i];
                }
                kernel_title[title_len] = '\0';
                serial_write("Kernel: Title copied: ");
                serial_write(kernel_title);
                serial_write("\n");
            } else {
                serial_write("Kernel: Warning - kernel_title kmalloc failed\n");
            }
            
            // Basic initialization
            win->title = kernel_title ? kernel_title : "Unknown";
            win->x = (int)params[0];
            win->y = (int)params[1];
            win->w = (int)params[2];
            win->h = (int)params[3];
            
            serial_write("Kernel: Init win dims.\n");
            
            // Sanity checks for dimensions
            if (win->w <= 0 || win->w > 4096) win->w = 400;
            if (win->h <= 0 || win->h > 4096) win->h = 400;

            win->visible = true;
            win->focused = true;
            win->z_index = 0;
            win->buf_len = 0;
            win->buffer[0] = 0;
            win->data = proc;
            
            serial_write("Kernel: Dims initialized.\n");
            
            size_t pixel_size = 0;
            // Safe allocation
            size_t client_h = win->h - 20;
            if (win->w <= 0 || win->h <= 20) {
                // Invalid dimensions, but prevent underflow/bad alloc
                win->pixels = NULL;
                win->comp_pixels = NULL;
            } else {
                pixel_size = (size_t)win->w * client_h * 4;
                win->pixels = kmalloc(pixel_size);
                win->comp_pixels = kmalloc(pixel_size);
            }
            
            serial_write("Kernel: Buffers allocated.\n");
            
            if (win->pixels) {
                extern void mem_memset(void *dest, int val, size_t len);
                mem_memset(win->pixels, 0, pixel_size);
            }
            if (win->comp_pixels) {
                extern void mem_memset(void *dest, int val, size_t len);
                mem_memset(win->comp_pixels, 0, pixel_size);
            }
            
            serial_write("Kernel: Buffers cleared.\n");

            // Set callbacks
            win->paint = user_window_paint;
            win->handle_click = user_window_click;
            win->handle_right_click = user_window_right_click;
            win->handle_close = user_window_close;
            win->handle_key = user_window_key;
            win->handle_resize = user_window_resize;
            win->resizable = false; // Default to false, can be enabled via syscall
            
            proc->ui_window = win;
            wm_add_window(win);
            
            return (uint64_t)win;
        } else if (cmd == GUI_CMD_DRAW_RECT) {
            Window *win = (Window *)arg2;
            uint64_t *u_params = (uint64_t *)arg3;
            uint32_t color = (uint32_t)arg4;
            if (win && u_params) {
                uint64_t params[4];
                for (int i = 0; i < 4; i++) params[i] = u_params[i];

                extern void draw_rect(int x, int y, int w, int h, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    // Strict user-to-window relative clamping
                    int rx = (int)params[0]; int ry = (int)params[1];
                    int rw = (int)params[2]; int rh = (int)params[3];
                    if (rx < 0) { rw += rx; rx = 0; }
                    if (ry < 0) { rh += ry; ry = 0; }
                    if (rx + rw > win->w) rw = win->w - rx;
                    if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;
                    
                    if (rw > 0 && rh > 0) {
                        graphics_set_render_target(win->pixels, win->w, win->h - 20);
                        draw_rect(rx, ry, rw, rh, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_rect(win->x + params[0], win->y + params[1], params[2], params[3], color);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_DRAW_ROUNDED_RECT_FILLED) {
            Window *win = (Window *)arg2;
            uint64_t *u_params = (uint64_t *)arg3;
            uint32_t color = (uint32_t)arg4;
            if (win && u_params) {
                uint64_t params[5];
                for (int i = 0; i < 5; i++) params[i] = u_params[i];

                extern void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    int rx = (int)params[0]; int ry = (int)params[1];
                    int rw = (int)params[2]; int rh = (int)params[3];
                    int rr = (int)params[4];
                    if (rx < 0) { rw += rx; rx = 0; }
                    if (ry < 0) { rh += ry; ry = 0; }
                    if (rx + rw > win->w) rw = win->w - rx;
                    if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;

                    if (rw > 0 && rh > 0) {
                        graphics_set_render_target(win->pixels, win->w, win->h - 20);
                        draw_rounded_rect_filled(rx, ry, rw, rh, rr, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_DRAW_STRING) {
            Window *win = (Window *)arg2;
            uint64_t coords = arg3;
            int ux = coords & 0xFFFFFFFF;
            int uy = coords >> 32;
            const char *user_str = (const char *)arg4;
            uint32_t color = (uint32_t)arg5;
            if (win && user_str) {
                extern void draw_string(int x, int y, const char *str, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                // Copy string safely to kernel stack buffer
                char kernel_str[256];
                int i = 0;
                while (i < 255 && user_str[i]) {
                    kernel_str[i] = user_str[i];
                    i++;
                }
                kernel_str[i] = 0;

                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                        graphics_set_render_target(win->pixels, win->w, win->h - 20);
                        draw_string(ux, uy, kernel_str, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_string(win->x + ux, win->y + uy, kernel_str, color);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == 10) { // GUI_CMD_DRAW_STRING_BITMAP
            Window *win = (Window *)arg2;
            uint64_t coords = arg3;
            int ux = coords & 0xFFFFFFFF;
            int uy = coords >> 32;
            const char *user_str = (const char *)arg4;
            uint32_t color = (uint32_t)arg5;
            if (win && user_str) {
                extern void draw_string_bitmap(int x, int y, const char *str, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                // Copy string safely to kernel stack buffer
                char kernel_str[256];
                int i = 0;
                while (i < 255 && user_str[i]) {
                    kernel_str[i] = user_str[i];
                    i++;
                }
                kernel_str[i] = 0;

                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                        graphics_set_render_target(win->pixels, win->w, win->h - 20);
                        draw_string_bitmap(ux, uy, kernel_str, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_string_bitmap(win->x + ux, win->y + uy, kernel_str, color);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == 11) { // GUI_CMD_DRAW_STRING_SCALED
            Window *win = (Window *)arg2;
            uint64_t coords = arg3;
            int ux = coords & 0xFFFFFFFF;
            int uy = coords >> 32;
            const char *user_str = (const char *)arg4;
            uint64_t packed = arg5;
            uint32_t color = packed & 0xFFFFFFFF;
            uint32_t scale_bits = packed >> 32;
            float scale = *(float*)&scale_bits;

            if (win && user_str) {
                extern void draw_string_scaled(int x, int y, const char *str, uint32_t color, float scale);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                // Copy string safely to kernel stack buffer
                char kernel_str[256];
                int i = 0;
                while (i < 255 && user_str[i]) {
                    kernel_str[i] = user_str[i];
                    i++;
                }
                kernel_str[i] = 0;

                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                        graphics_set_render_target(win->pixels, win->w, win->h - 20);
                        draw_string_scaled(ux, uy, kernel_str, color, scale);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_string_scaled(win->x + ux, win->y + uy, kernel_str, color, scale);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_DRAW_IMAGE) {
            Window *win = (Window *)arg2;
            uint64_t *u_params = (uint64_t *)arg3;
            uint32_t *image_data = (uint32_t *)arg4;
            if (win && u_params && image_data) {
                uint64_t params[4];
                for (int i = 0; i < 4; i++) params[i] = u_params[i];
                
                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    int rx = (int)params[0]; int ry = (int)params[1];
                    int rw = (int)params[2]; int rh = (int)params[3];
                    
                    int src_x_offset = 0;
                    int src_y_offset = 0;
                    if (rx < 0) { src_x_offset = -rx; rw += rx; rx = 0; }
                    if (ry < 0) { src_y_offset = -ry; rh += ry; ry = 0; }
                    if (rx + rw > win->w) rw = win->w - rx;
                    if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;
                    
                    if (rw > 0 && rh > 0) {
                        for (int y = 0; y < rh; y++) {
                            uint32_t *dest = &win->pixels[(ry + y) * win->w + rx];
                            uint32_t *src = &image_data[(src_y_offset + y) * (int)params[2] + src_x_offset];
                            for (int x = 0; x < rw; x++) {
                                dest[x] = src[x];
                            }
                        }
                    }
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_MARK_DIRTY) {
            Window *win = (Window *)arg2;
            uint64_t *u_params = (uint64_t *)arg3;
            if (win && u_params) {
                uint64_t params[4];
                for (int i = 0; i < 4; i++) params[i] = u_params[i];

                // Dual-buffer commit: copy pixels to comp_pixels
                if (win->pixels && win->comp_pixels) {
                    extern void mem_memcpy(void *dest, const void *src, size_t len);
                    mem_memcpy(win->comp_pixels, win->pixels, (size_t)win->w * (win->h - 20) * 4);
                }
                wm_mark_dirty(win->x + (int)params[0], win->y + (int)params[1], (int)params[2], (int)params[3]);
            }
        } else if (cmd == GUI_CMD_GET_EVENT) {
            Window *win = (Window *)arg2;
            gui_event_t *ev_out = (gui_event_t *)arg3;
            if (!win || !ev_out) return 0;
            if (proc->gui_event_head != proc->gui_event_tail) {
                *ev_out = proc->gui_events[proc->gui_event_head];
                proc->gui_event_head = (proc->gui_event_head + 1) % MAX_GUI_EVENTS;
                return 1;
            }
            return 0;
        } else if (cmd == GUI_CMD_GET_STRING_WIDTH) {
            const char *user_str = (const char *)arg2;
            if (!user_str) return 0;
            
            char kernel_str[256];
            int i = 0;
            while (i < 255 && user_str[i]) {
                kernel_str[i] = user_str[i];
                i++;
            }
            kernel_str[i] = 0;
            
            ttf_font_t *font = graphics_get_current_ttf();
            if (font) {
                return (uint64_t)font_manager_get_string_width(font, kernel_str);
            } else {
                return (uint64_t)i * 8; // Fallback bitmap width
            }
        } else if (cmd == 12) { // GUI_CMD_GET_STRING_WIDTH_SCALED
            const char *user_str = (const char *)arg2;
            uint32_t scale_bits = (uint32_t)arg3;
            float scale = *(float*)&scale_bits;

            if (!user_str) return 0;
            
            char kernel_str[256];
            int i = 0;
            while (i < 255 && user_str[i]) {
                kernel_str[i] = user_str[i];
                i++;
            }
            kernel_str[i] = 0;
            
            extern int graphics_get_string_width_scaled(const char *s, float scale);
            return (uint64_t)graphics_get_string_width_scaled(kernel_str, scale);
        } else if (cmd == GUI_CMD_GET_FONT_HEIGHT) {
            extern int graphics_get_font_height(void);
            return (uint64_t)graphics_get_font_height();

        } else if (cmd == 14) { // GUI_CMD_WINDOW_SET_RESIZABLE
            Window *win = (Window *)arg2;
            if (win) {
                extern void serial_write(const char *str);
                serial_write("Kernel: Setting window resizable to ");
                serial_write(arg3 ? "true\n" : "false\n");
                win->resizable = (arg3 != 0);
            }
            return 0;
        } else if (cmd == 13) { // GUI_CMD_GET_FONT_HEIGHT_SCALED
            uint32_t scale_bits = (uint32_t)arg2;
            float scale = *(float*)&scale_bits;
            extern int graphics_get_font_height_scaled(float scale);
            return (uint64_t)graphics_get_font_height_scaled(scale);
        } else if (cmd == 15) { // GUI_CMD_WINDOW_SET_TITLE
            Window *win = (Window *)arg2;
            const char *user_title = (const char *)arg3;
            if (win && user_title) {
                int title_len = 0;
                while (user_title[title_len] && title_len < 255) title_len++;
                
                char *kernel_title = kmalloc(title_len + 1);
                if (kernel_title) {
                    for (int i = 0; i < title_len; i++) {
                        kernel_title[i] = user_title[i];
                    }
                    kernel_title[title_len] = '\0';
                    
                    if (win->title && win->title != (char*)"Unknown") {
                        kfree(win->title);
                    }
                    win->title = kernel_title;
                    wm_mark_dirty(win->x, win->y - 20, win->w, 20); // Mark title bar dirty
                    wm_refresh();
                }
            }
            return 0;
        }
    } else if (syscall_num == SYS_FS) {
        int cmd = (int)arg1;
        process_t *proc = process_get_current();
        
        if (cmd == FS_CMD_OPEN) {
            const char *path = (const char *)arg2;
            const char *mode = (const char *)arg3;
            if (!path || !mode) return -1;
            
            FAT32_FileHandle *fh = fat32_open(path, mode);
            if (!fh) return -1;
            
            for (int i = 0; i < MAX_PROCESS_FDS; i++) {
                if (proc->fds[i] == NULL) {
                    proc->fds[i] = fh;
                    return (uint64_t)i;
                }
            }
            fat32_close(fh);
            return -1;
        } else if (cmd == FS_CMD_READ) {
            int fd = (int)arg2;
            void *buf = (void *)arg3;
            uint32_t len = (uint32_t)arg4;
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            return (uint64_t)fat32_read((FAT32_FileHandle*)proc->fds[fd], buf, (int)len);
        } else if (cmd == FS_CMD_WRITE) {
            int fd = (int)arg2;
            const void *buf = (const void *)arg3;
            uint32_t len = (uint32_t)arg4;
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            return (uint64_t)fat32_write((FAT32_FileHandle*)proc->fds[fd], buf, (int)len);
        } else if (cmd == FS_CMD_CLOSE) {
            int fd = (int)arg2;
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            fat32_close((FAT32_FileHandle*)proc->fds[fd]);
            proc->fds[fd] = NULL;
            return 0;
        } else if (cmd == FS_CMD_SEEK) {
            int fd = (int)arg2;
            int offset = (int)arg3;
            int whence = (int)arg4; // 0=SET, 1=CUR, 2=END
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            return (uint64_t)fat32_seek((FAT32_FileHandle*)proc->fds[fd], offset, whence);
        } else if (cmd == FS_CMD_TELL) {
            int fd = (int)arg2;
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            return (uint64_t)((FAT32_FileHandle*)proc->fds[fd])->position;
        } else if (cmd == FS_CMD_SIZE) {
            int fd = (int)arg2;
            if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
            return (uint64_t)((FAT32_FileHandle*)proc->fds[fd])->size;
        }
 else if (cmd == FS_CMD_LIST) {
            const char *path = (const char *)arg2;
            FAT32_FileInfo *entries = (FAT32_FileInfo *)arg3;
            int max_entries = (int)arg4;
            if (!path || !entries) return -1;
            return (uint64_t)fat32_list_directory(path, entries, max_entries);
        } else if (cmd == FS_CMD_DELETE) {
            const char *path = (const char *)arg2;
            if (!path) return -1;
            return fat32_delete(path) ? 0 : -1;
        } else if (cmd == FS_CMD_GET_INFO) {
            const char *path = (const char *)arg2;
            FAT32_FileInfo *info = (FAT32_FileInfo *)arg3;
            if (!path || !info) return -1;
            extern int fat32_get_info(const char *path, FAT32_FileInfo *info);
            return (uint64_t)fat32_get_info(path, info);
        } else if (cmd == FS_CMD_MKDIR) {
            const char *path = (const char *)arg2;
            if (!path) return -1;
            return fat32_mkdir(path) ? 0 : -1;
        } else if (cmd == FS_CMD_EXISTS) {
            const char *path = (const char *)arg2;
            if (!path) return 0;
            return fat32_exists(path) ? 1 : 0;
        } else if (cmd == FS_CMD_GETCWD) {
            char *buf = (char *)arg2;
            int size = (int)arg3;
            if (!buf) return -1;
            fat32_get_current_dir(buf, size);
            return 0;
        } else if (cmd == FS_CMD_CHDIR) {
            const char *path = (const char *)arg2;
            if (!path) return -1;
            return fat32_chdir(path) ? 0 : -1;
        }
        return 0;
    } else if (syscall_num == 8) { // DEBUG_SERIAL_WRITE
        extern void serial_write(const char *str);
        serial_write((const char *)arg2);
        return 0;
    } else if (syscall_num == 10) { // SYS_KILL
        return 0; // Handled in outer
    } else if (syscall_num == 9) { // SYS_SBRK
        int incr = (int)arg1;
        process_t *proc = process_get_current();
        if (!proc || !proc->is_user) return (uint64_t)-1;
        
        uint64_t old_end = proc->heap_end;
        if (incr == 0) return old_end;
        
        uint64_t new_end = old_end + incr;
        
        // If expanding, we might need to map new pages
        if (incr > 0) {
            uint64_t start_page = (old_end + 0xFFF) & ~0xFFF;
            uint64_t end_page = (new_end + 0xFFF) & ~0xFFF;
            
            if (end_page > start_page) {
                uint64_t total_size = end_page - start_page;
                void *phys_block = kmalloc_aligned(total_size, 4096);
                if (!phys_block) return (uint64_t)-1; // Out of memory
                
                extern void mem_memset(void *dest, int val, size_t len);
                mem_memset(phys_block, 0, total_size);
                
                uint64_t phys_addr = (uint64_t)phys_block;
                for (uint64_t page = start_page; page < end_page; page += 4096) {
                    paging_map_page(proc->pml4_phys, page, v2p(phys_addr), 0x07); // PT_PRESENT | PT_RW | PT_USER
                    phys_addr += 4096;
                }
            }
        }
        
        proc->heap_end = new_end;
        return old_end;
    } else if (syscall_num == 5) { // SYS_SYSTEM
        int cmd = (int)arg1;
        if (cmd == 1) { // SYSTEM_CMD_SET_BG_COLOR
            uint32_t color = (uint32_t)arg2;
            extern void graphics_set_bg_color(uint32_t color);
            graphics_set_bg_color(color);
            return 0;
        } else if (cmd == 2) { // SYSTEM_CMD_SET_BG_PATTERN
            uint32_t *user_pat = (uint32_t *)arg2;
            if (!user_pat) {
                graphics_set_bg_pattern(NULL);
            } else {
                static uint32_t global_bg_pattern[128*128];
                for (int i=0; i<128*128; i++) {
                    global_bg_pattern[i] = user_pat[i];
                }
                graphics_set_bg_pattern(global_bg_pattern);
            }
            extern void wm_refresh(void);
            wm_refresh();
            return 0;
        } else if (cmd == 3) { // SYSTEM_CMD_SET_WALLPAPER (Obsolete)
            return -1;
        } else if (cmd == 4) { // SYSTEM_CMD_SET_DESKTOP_PROP
            int prop = (int)arg2;
            int val = (int)arg3;
            extern _Bool desktop_snap_to_grid;
            extern _Bool desktop_auto_align;
            extern int desktop_max_rows_per_col;
            extern int desktop_max_cols;
            if (prop == 1) desktop_snap_to_grid = val;
            if (prop == 2) desktop_auto_align = val;
            if (prop == 3) desktop_max_rows_per_col = val;
            if (prop == 4) desktop_max_cols = val;
            extern void wm_refresh_desktop(void);
            wm_refresh_desktop();
            return 0;
        } else if (cmd == 5) { // SYSTEM_CMD_SET_MOUSE_SPEED
            extern int mouse_speed;
            mouse_speed = (int)arg2;
            return 0;
        } else if (cmd == 6) { // SYSTEM_CMD_NETWORK_INIT
            extern int network_init(void);
            return network_init();
        } else if (cmd == 7) { // SYSTEM_CMD_GET_DESKTOP_PROP
            int prop = (int)arg2;
            extern _Bool desktop_snap_to_grid;
            extern _Bool desktop_auto_align;
            extern int desktop_max_rows_per_col;
            extern int desktop_max_cols;
            if (prop == 1) return desktop_snap_to_grid;
            if (prop == 2) return desktop_auto_align;
            if (prop == 3) return desktop_max_rows_per_col;
            if (prop == 4) return desktop_max_cols;
            return 0;
        } else if (cmd == 8) { // SYSTEM_CMD_GET_MOUSE_SPEED
            extern int mouse_speed;
            return mouse_speed;
        } else if (cmd == 9) { // SYSTEM_CMD_GET_WALLPAPER_THUMB (Obsolete)
            return -1;
        } else if (cmd == 10) { // SYSTEM_CMD_CLEAR_SCREEN
            extern void cmd_screen_clear(void);
            cmd_screen_clear();
            return 0;
        } else if (cmd == 11) { // SYSTEM_CMD_RTC_GET
            int *dt = (int *)arg2;
            if (!dt) return -1;
            extern void rtc_get_datetime(int *y, int *m, int *d, int *h, int *min, int *s);
            rtc_get_datetime(&dt[0], &dt[1], &dt[2], &dt[3], &dt[4], &dt[5]);
            return 0;
        } else if (cmd == 12) { // SYSTEM_CMD_REBOOT
            k_reboot();
            return 0;
        } else if (cmd == 13) { // SYSTEM_CMD_SHUTDOWN
            k_shutdown();
            return 0;
        } else if (cmd == 14) { // SYSTEM_CMD_BEEP
            int freq = (int)arg2;
            int ms = (int)arg3;
            if (freq > 0) {
                int div = 1193180 / freq;
                outb(0x43, 0xB6);
                outb(0x42, div & 0xFF);
                outb(0x42, (div >> 8) & 0xFF);
                outb(0x61, inb(0x61) | 0x03);
            }
            // Sleep - kernel side
            k_sleep(ms);
            outb(0x61, inb(0x61) & 0xFC);
            return 0;
        } else if (cmd == 15) { // SYSTEM_CMD_MEMINFO
            uint64_t *out = (uint64_t *)arg2;
            if (!out) return -1;
            MemStats stats = memory_get_stats();
            out[0] = stats.total_memory;
            out[1] = stats.used_memory;
            return 0;
        } else if (cmd == 16) { // SYSTEM_CMD_UPTIME
            return wm_get_ticks();
        } else if (cmd == 17) { // SYSTEM_CMD_PCI_LIST
            typedef struct {
                uint16_t vendor;
                uint16_t device;
                uint8_t class_code;
                uint8_t subclass;
            } pci_info_t;
            pci_info_t *info = (pci_info_t *)arg2;
            int idx = (int)arg3;
            if (!info) {
                pci_device_t pci_devs[128];
                return pci_enumerate_devices(pci_devs, 128);
            }
            pci_device_t pci_devs[128];
            int count = pci_enumerate_devices(pci_devs, 128);
            if (idx >= 0 && idx < count) {
                info->vendor = pci_devs[idx].vendor_id;
                info->device = pci_devs[idx].device_id;
                info->class_code = pci_devs[idx].class_code;
                info->subclass = pci_devs[idx].subclass;
                return 0;
            }
            return -1;
        } else if (cmd == 18) { // SYSTEM_CMD_NETWORK_DHCP
            return network_dhcp_acquire();
        } else if (cmd == 19) { // SYSTEM_CMD_NETWORK_GET_MAC
            mac_address_t *mac = (mac_address_t *)arg2;
            if (!mac) return -1;
            return network_get_mac_address(mac);
        } else if (cmd == 20) { // SYSTEM_CMD_NETWORK_GET_IP
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            if (!ip) return -1;
            return network_get_ipv4_address(ip);
        } else if (cmd == 21) { // SYSTEM_CMD_NETWORK_SET_IP
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            if (!ip) return -1;
            return network_set_ipv4_address(ip);
        } else if (cmd == 22) { // SYSTEM_CMD_UDP_SEND
            ipv4_address_t *dest_ip = (ipv4_address_t *)arg2;
            uint32_t ports = (uint32_t)arg3;  // dest_port in lower 16, src_port in upper 16
            uint16_t dest_port = ports & 0xFFFF;
            uint16_t src_port = (ports >> 16) & 0xFFFF;
            const void *data = (const void *)arg4;
            size_t data_len = (size_t)arg5;
            if (!dest_ip || !data) return -1;
            return udp_send_packet(dest_ip, dest_port, src_port, data, data_len);
        } else if (cmd == 23) { // SYSTEM_CMD_NETWORK_GET_STATS
            int stat_type = (int)arg2;
            switch (stat_type) {
                case 0: return network_get_frames_received();
                case 1: return network_get_udp_packets_received();
                case 2: return network_get_frames_sent();
                case 3: return network_get_e1000_receive_calls();
                case 4: return network_get_e1000_receive_empty();
                case 5: return network_get_process_calls();
                default: return -1;
            }
        } else if (cmd == 24) { // SYSTEM_CMD_NETWORK_GET_GATEWAY
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            if (!ip) return -1;
            return network_get_gateway_ip(ip);
        } else if (cmd == 25) { // SYSTEM_CMD_NETWORK_GET_DNS
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            if (!ip) return -1;
            return network_get_dns_ip(ip);
        } else if (cmd == 26) { // SYSTEM_CMD_ICMP_PING
            ipv4_address_t *dest_ip = (ipv4_address_t *)arg2;
            if (!dest_ip) return -1;
            extern int network_icmp_single_ping(ipv4_address_t *dest);
            return (uint64_t)network_icmp_single_ping(dest_ip);
        } else if (cmd == 27) { // SYSTEM_CMD_NETWORK_IS_INIT
            return network_is_initialized() ? 1 : 0;
        } else if (cmd == 30) { // SYSTEM_CMD_NETWORK_HAS_IP
            return network_has_ip() ? 1 : 0;
        } else if (cmd == 28) { // SYSTEM_CMD_GET_SHELL_CONFIG
            const char *key = (const char *)arg2;
            if (!key) return -1;
            return cmd_get_config_value(key);
        } else if (cmd == 29) { // SYSTEM_CMD_SET_TEXT_COLOR
            uint32_t color = (uint32_t)arg2;
            cmd_set_current_color(color);
            return 0;
        } else if (cmd == 31) { // SYSTEM_CMD_SET_WALLPAPER_PATH
            const char *user_path = (const char *)arg2;
            if (!user_path) return -1;
            
            // Copy path safely to kernel buffer
            char kernel_path[256];
            int i = 0;
            while (i < 255 && user_path[i]) {
                kernel_path[i] = user_path[i];
                i++;
            }
            kernel_path[i] = 0;
            
            extern void wallpaper_request_set_from_file(const char *path);
            wallpaper_request_set_from_file(kernel_path);
            return 0;
        } else if (cmd == 32) { // SYSTEM_CMD_RTC_SET
            int *dt = (int *)arg2;
            if (!dt) return -1;
            extern void rtc_set_datetime(int y, int m, int d, int h, int min, int s);
            rtc_set_datetime(dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
            return 0;
        } else if (cmd == 33) { // SYSTEM_CMD_TCP_CONNECT
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            uint16_t port = (uint16_t)arg3;
            extern int network_tcp_connect(const ipv4_address_t *ip, uint16_t port);
            return (uint64_t)network_tcp_connect(ip, port);
        } else if (cmd == 34) { // SYSTEM_CMD_TCP_SEND
            const void *data = (const void *)arg2;
            size_t len = (size_t)arg3;
            extern int network_tcp_send(const void *data, size_t len);
            return (uint64_t)network_tcp_send(data, len);
        } else if (cmd == 35) { // SYSTEM_CMD_TCP_RECV
            void *buf = (void *)arg2;
            size_t max_len = (size_t)arg3;
            extern int network_tcp_recv(void *buf, size_t max_len);
            return (uint64_t)network_tcp_recv(buf, max_len);
        } else if (cmd == 36) { // SYSTEM_CMD_TCP_CLOSE
            extern int network_tcp_close(void);
            return (uint64_t)network_tcp_close();
        } else if (cmd == 37) { // SYSTEM_CMD_DNS_LOOKUP
            const char *user_name = (const char *)arg2;
            ipv4_address_t *out_ip = (ipv4_address_t *)arg3;
            char name_buf[256];
            int i = 0;
            while (i < 255 && user_name[i]) { name_buf[i] = user_name[i]; i++; }
            name_buf[i] = 0;
            extern int network_dns_lookup(const char *name, ipv4_address_t *out_ip);
            return (uint64_t)network_dns_lookup(name_buf, out_ip);
        } else if (cmd == 38) { // SYSTEM_CMD_SET_DNS
            ipv4_address_t *ip = (ipv4_address_t *)arg2;
            extern int network_set_dns_server(const ipv4_address_t *ip);
            return (uint64_t)network_set_dns_server(ip);
        } else if (cmd == 39) { // SYSTEM_CMD_NET_UNLOCK
            extern void network_force_unlock(void);
            network_force_unlock();
            return 0;
        } else if (cmd == 40) { // SYSTEM_CMD_SET_FONT
            const char *user_path = (const char *)arg2;
            if (!user_path) return -1;
            // Copy font path from userland
            char path[128];
            int i;
            for (i = 0; i < 127 && user_path[i]; i++) {
                path[i] = user_path[i];
            }
            path[i] = 0;
            graphics_set_font(path);
            return 0;
        } else if (cmd == 41) { // SYSTEM_CMD_SET_RAW_MODE
            extern void cmd_set_raw_mode(bool enabled);
            cmd_set_raw_mode((bool)arg2);
            return 0;
        }
        return -1;
    }
    
    return 0;
}

uint64_t syscall_handler_c(registers_t *regs) {
    uint64_t syscall_num = regs->rax;
    
    // Check for context-switching syscalls
    if (syscall_num == 0 || syscall_num == 60 || syscall_num == 10) { // EXIT or KILL
        return process_terminate_current();
    }
    
    // Normal syscalls
    regs->rax = syscall_handler_inner(regs->rax, regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8);
    
    // Return current RSP to assembly wrapper
    return (uint64_t)regs;
}
