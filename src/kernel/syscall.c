#include "syscall.h"
#include "gdt.h"
#include "memory_manager.h"
#include "gui_ipc.h"
#include "process.h"
#include "wm.h"

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

uint64_t syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    extern void cmd_write(const char *str);
    extern void serial_write(const char *str);
    extern void cmd_process_finished(void);
    
    if (syscall_num == 1) { // SYS_WRITE
        // arg2 is the buffer based on our user_test logic
        cmd_write((const char*)arg2);
        serial_write((const char*)arg2);
    } else if (syscall_num == 0) { // SYS_EXIT
        process_t *proc = process_get_current();
        if (proc && proc->ui_window) {
            wm_remove_window((Window *)proc->ui_window);
            proc->ui_window = NULL;
        }
        cmd_process_finished();
        // The actual process termination and scheduling will be handled later
        // For now this just releases the CMD prompt lock.
        // We will eventually need to mark the process_t as DEAD here.
    } else if (syscall_num == 3) { // SYS_GUI
        int cmd = (int)arg1;
        process_t *proc = process_get_current();
        
        if (cmd == GUI_CMD_WINDOW_CREATE) {
            const char *title = (const char *)arg2;
            uint64_t *params = (uint64_t *)arg3;
            if (!params) return 0;
            
            Window *win = kmalloc(sizeof(Window));
            if (!win) return 0;
            
            // Copy title from user space to kernel space so wm.c can access it safely
            int title_len = 0;
            while (title[title_len] && title_len < 255) title_len++;
            char *kernel_title = kmalloc(title_len + 1);
            if (kernel_title) {
                for (int i = 0; i < title_len; i++) {
                    kernel_title[i] = title[i];
                }
                kernel_title[title_len] = '\0';
            }
            
            // Basic initialization
            win->title = kernel_title ? kernel_title : "Unknown";
            win->x = (int)params[0];
            win->y = (int)params[1];
            win->w = (int)params[2];
            win->h = (int)params[3];
            
            // Sanity checks for dimensions
            if (win->w <= 0 || win->w > 4096) win->w = 400;
            if (win->h <= 0 || win->h > 4096) win->h = 400;

            win->visible = true;
            win->focused = true;
            win->z_index = 0;
            win->buf_len = 0;
            win->buffer[0] = 0;
            win->data = proc;
            
            // Safe allocation
            size_t pixel_size = (size_t)win->w * win->h * 4;
            win->pixels = kmalloc(pixel_size);
            win->comp_pixels = kmalloc(pixel_size);
            if (win->pixels) {
                extern void mem_memset(void *dest, int val, size_t len);
                mem_memset(win->pixels, 0, pixel_size);
            }
            if (win->comp_pixels) {
                extern void mem_memset(void *dest, int val, size_t len);
                mem_memset(win->comp_pixels, 0, pixel_size);
            }
            
            win->paint = user_window_paint;
            win->handle_click = user_window_click;
            win->handle_close = user_window_close;
            win->handle_key = NULL;
            win->handle_right_click = NULL;
            
            proc->ui_window = win;
            wm_add_window(win);
            
            return (uint64_t)win;
        } else if (cmd == GUI_CMD_DRAW_RECT) {
            Window *win = (Window *)arg2;
            uint64_t *p = (uint64_t *)arg3;
            uint32_t color = (uint32_t)arg4;
            if (win && p) {
                extern void draw_rect(int x, int y, int w, int h, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    // Strict user-to-window relative clamping
                    int rx = (int)p[0]; int ry = (int)p[1];
                    int rw = (int)p[2]; int rh = (int)p[3];
                    if (rx < 0) { rw += rx; rx = 0; }
                    if (ry < 0) { rh += ry; ry = 0; }
                    if (rx + rw > win->w) rw = win->w - rx;
                    if (ry + rh > win->h) rh = win->h - ry;
                    
                    if (rw > 0 && rh > 0) {
                        graphics_set_render_target(win->pixels, win->w, win->h);
                        draw_rect(rx, ry, rw, rh, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_rect(win->x + p[0], win->y + p[1], p[2], p[3], color);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_DRAW_ROUNDED_RECT_FILLED) {
            Window *win = (Window *)arg2;
            uint64_t *p = (uint64_t *)arg3;
            uint32_t color = (uint32_t)arg4;
            if (win && p) {
                extern void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color);
                extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
                
                uint64_t rflags;
                asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
                
                if (win->pixels) {
                    int rx = (int)p[0]; int ry = (int)p[1];
                    int rw = (int)p[2]; int rh = (int)p[3];
                    int rr = (int)p[4];
                    if (rx < 0) { rw += rx; rx = 0; }
                    if (ry < 0) { rh += ry; ry = 0; }
                    if (rx + rw > win->w) rw = win->w - rx;
                    if (ry + rh > win->h) rh = win->h - ry;

                    if (rw > 0 && rh > 0) {
                        graphics_set_render_target(win->pixels, win->w, win->h);
                        draw_rounded_rect_filled(rx, ry, rw, rh, rr, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_rounded_rect_filled(win->x + p[0], win->y + p[1], p[2], p[3], p[4], color);
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
                    // String clipping is handled by draw_char -> put_pixel, 
                    // but we ensure coordinate sanity here
                    if (ux >= -100 && ux < win->w && uy >= -100 && uy < win->h) {
                        graphics_set_render_target(win->pixels, win->w, win->h);
                        draw_string(ux, uy, kernel_str, color);
                        graphics_set_render_target(NULL, 0, 0);
                    }
                } else {
                    draw_string(win->x + ux, win->y + uy, kernel_str, color);
                }
                
                asm volatile("push %0; popfq" : : "r"(rflags));
            }
        } else if (cmd == GUI_CMD_MARK_DIRTY) {
            Window *win = (Window *)arg2;
            uint64_t *p = (uint64_t *)arg3;
            if (win && p) {
                // Dual-buffer commit: copy pixels to comp_pixels
                if (win->pixels && win->comp_pixels) {
                    extern void mem_memcpy(void *dest, const void *src, size_t len);
                    mem_memcpy(win->comp_pixels, win->pixels, (size_t)win->w * win->h * 4);
                }
                wm_mark_dirty(win->x + p[0], win->y + p[1], p[2], p[3]);
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
        }
    }
    
    return 0;
}
