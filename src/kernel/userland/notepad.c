#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/syscall_user.h"
#include <stddef.h>

#define COLOR_NOTEPAD_BG 0xFFFFFFFF
#define COLOR_BLACK     0xFF000000

#define NOTEPAD_BUF_SIZE (64 * 1024)
static char buffer[NOTEPAD_BUF_SIZE];
static int buf_len = 0;
static int cursor_pos = 0;
static int notepad_scroll_line = 0;

static void notepad_ensure_cursor_visible(int h) {
    int visible_lines = (h - 40) / 10 + 3; 
    if (visible_lines < 1) visible_lines = 1;
    
    int cursor_line = 0;
    for (int i = 0; i < cursor_pos && i < buf_len; i++) {
        if (buffer[i] == '\n') cursor_line++;
    }
    
    if (cursor_line < notepad_scroll_line) {
        notepad_scroll_line = cursor_line;
    }
    
    if (cursor_line >= notepad_scroll_line + visible_lines) {
        notepad_scroll_line = cursor_line - visible_lines + 1;
    }
}

static void notepad_load_state() {
    int fd = sys_open("A:/tmp/notepad_state.txt", "r");
    if (fd >= 0) {
        sys_serial_write("Notepad: Loading state...\n");
        buf_len = sys_read(fd, buffer, NOTEPAD_BUF_SIZE - 1);
        if (buf_len < 0) buf_len = 0;
        buffer[buf_len] = 0;
        sys_close(fd);
    }
    cursor_pos = buf_len;
}

static void notepad_save_state() {
    // Ensure dir exists
    sys_mkdir("A:/tmp");
    int fd = sys_open("A:/tmp/notepad_state.txt", "w");
    if (fd >= 0) {
        sys_write_fs(fd, buffer, buf_len);
        sys_close(fd);
    }
}

static void notepad_paint(ui_window_t win, int w, int h) {
    ui_draw_rect(win, 4, 30, w - 8, h - 34, COLOR_NOTEPAD_BG);

    int visual_line = 0; 
    int current_x = 8;
    int current_y = 36;
    int window_right = w - 16;  
    
    for (int i = 0; i < buf_len; i++) {
        if (visual_line < notepad_scroll_line) {
            if (buffer[i] == '\n') {
                visual_line++;
                current_x = 8;
                current_y = 36;
            } else {
                if (current_x >= window_right) {
                    visual_line++;
                    current_x = 8;
                    current_y += 10;
                }
                current_x += 8;
            }
            continue;
        }
        
        if (visual_line >= notepad_scroll_line + (h - 40) / 10) {
            break;
        }
        
        if (buffer[i] == '\n') {
            current_x = 8;
            current_y += 10;
            visual_line++;
        } else {
            if (current_x >= window_right) {
                current_x = 8;
                current_y += 10;
                visual_line++;
                
                if (visual_line >= notepad_scroll_line + (h - 40) / 10) {
                    break;
                }
            }
            
            char ch[2] = {buffer[i], 0};
            ui_draw_string(win, current_x, current_y, ch, COLOR_BLACK);
            current_x += 8;
        }
    }
    
    // Cursor
    int cx = 8;
    int cy = 36;
    int c_visual_line = 0;
    
    for (int i = 0; i < cursor_pos; i++) {
        if (buffer[i] == '\n') {
            cx = 8;
            cy += 10;
            c_visual_line++;
        } else {
            if (cx >= window_right) {
                cx = 8;
                cy += 10;
                c_visual_line++;
            }
            cx += 8;
        }
    }
    
    if (c_visual_line >= notepad_scroll_line && 
        c_visual_line < notepad_scroll_line + (h - 40) / 10) {
        ui_draw_rect(win, cx, cy, 2, 8, COLOR_BLACK);
    }
    
    ui_mark_dirty(win, 0, 0, w, h);
}

static void notepad_key(ui_window_t win, int h, char c) {
    if (c == 17) { // UP
        if (cursor_pos > 0) {
            int curr = cursor_pos;
            int line_start = curr;
            while (line_start > 0 && buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;

            if (line_start > 0) {
                int prev_line_end = line_start - 1;
                int prev_line_start = prev_line_end;
                while (prev_line_start > 0 && buffer[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int prev_line_len = prev_line_end - prev_line_start;
                if (col > prev_line_len) col = prev_line_len;
                cursor_pos = prev_line_start + col;
            }
        }
    } else if (c == 18) { // DOWN
        if (cursor_pos < buf_len) {
            int curr = cursor_pos;
            int line_start = curr;
            while (line_start > 0 && buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;
            
            int next_line_start = curr;
            while (next_line_start < buf_len && buffer[next_line_start] != '\n') {
                next_line_start++;
            }
            
            if (next_line_start < buf_len) {
                next_line_start++; // Skip newline
                int next_line_end = next_line_start;
                while (next_line_end < buf_len && buffer[next_line_end] != '\n') {
                    next_line_end++;
                }
                int next_line_len = next_line_end - next_line_start;
                if (col > next_line_len) col = next_line_len;
                cursor_pos = next_line_start + col;
            } else {
                cursor_pos = buf_len;
            }
        }
    } else if (c == 19) { // LEFT
        if (cursor_pos > 0) cursor_pos--;
    } else if (c == 20) { // RIGHT
        if (cursor_pos < buf_len) cursor_pos++;
    } else if (c == '\b') { // Backspace
        if (cursor_pos > 0) {
            for (int i = cursor_pos; i < buf_len; i++) {
                buffer[i - 1] = buffer[i];
            }
            buf_len--;
            cursor_pos--;
            buffer[buf_len] = 0;
        }
    } else if (c == '\n') { // Enter
         if (buf_len < 1023) {
             for (int i = buf_len; i > cursor_pos; i--) {
                 buffer[i] = buffer[i - 1];
             }
             buffer[cursor_pos] = c;
             buf_len++;
             cursor_pos++;
             buffer[buf_len] = 0;
         }
    } else {
        if (buf_len < NOTEPAD_BUF_SIZE - 1) {
            for (int i = buf_len; i > cursor_pos; i--) {
                buffer[i] = buffer[i - 1];
            }
            buffer[cursor_pos] = c;
            buf_len++;
            cursor_pos++;
            buffer[buf_len] = 0;
        }
    }
    notepad_ensure_cursor_visible(h);
}

int main(int argc, char **argv) {
    sys_serial_write("Notepad: Starting userspace main...\n");
    ui_window_t win = ui_window_create("Notepad", 100, 100, 400, 300);
    if (win == 0) {
        sys_serial_write("Notepad: Failed to create window!\n");
        return 1;
    }
    sys_serial_write("Notepad: Window created successfully.\n");

    notepad_load_state();

    gui_event_t ev;
    sys_serial_write("Notepad: Entering event loop...\n");
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                notepad_paint(win, 400, 300);
            } else if (ev.type == GUI_EVENT_KEY) {
                notepad_key(win, 300, (char)ev.arg1);
                notepad_paint(win, 400, 300);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_serial_write("Notepad: CLOSE\n");
                notepad_save_state();
                sys_exit(0);
            }
        } else {
            // Optional: sys_yield() or similar to avoid high CPU
            // For now, just keep looping but it's better than nothing
            for(volatile int i=0; i<10000; i++); 
        }
    }

    return 0;
}
