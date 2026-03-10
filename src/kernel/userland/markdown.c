// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MD_MAX_CONTENT 16384
#define MD_MAX_LINES 256
#define MD_CHAR_WIDTH 8
#define MD_LINE_HEIGHT 16

#define COLOR_DARK_PANEL    0xFF202020
#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_TEXT     0xFFE0E0E0
#define COLOR_DARK_TITLEBAR 0xFF303030
#define COLOR_BLACK         0xFF000000

typedef enum {
    MD_LINE_NORMAL,
    MD_LINE_HEADING1,
    MD_LINE_HEADING2,
    MD_LINE_HEADING3,
    MD_LINE_BOLD,
    MD_LINE_ITALIC,
    MD_LINE_LIST,
    MD_LINE_BLOCKQUOTE,
    MD_LINE_CODE
} MDLineType;

typedef struct {
    char content[256];
    int length;
    MDLineType type;
    int indent_level;
} MDLine;

static MDLine lines[MD_MAX_LINES];
static int line_count = 0;
static int scroll_top = 0;
static char open_filename[256] = "";

static int win_w = 600;
static int win_h = 400;

static size_t md_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void md_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int md_strncpy(char *dest, const char *src, int n) {
    int i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

static void md_parse_line(const char *raw_line, char *output, MDLineType *type, int *indent) {
    int i = 0;
    int out_idx = 0;
    *indent = 0;
    *type = MD_LINE_NORMAL;
    
    while (raw_line[i] == ' ' || raw_line[i] == '\t') {
        if (raw_line[i] == '\t') *indent += 2;
        else *indent += 1;
        i++;
    }
    
    if (raw_line[i] == '#') {
        int hash_count = 0;
        while (raw_line[i] == '#') {
            hash_count++;
            i++;
        }
        if (raw_line[i] == ' ') i++;
        
        if (hash_count == 1) *type = MD_LINE_HEADING1;
        else if (hash_count == 2) *type = MD_LINE_HEADING2;
        else if (hash_count <= 6) *type = MD_LINE_HEADING3;
    } else if (raw_line[i] == '-' || raw_line[i] == '*') {
        if ((raw_line[i] == '-' || raw_line[i] == '*') && (raw_line[i+1] == ' ' || raw_line[i+1] == '\t')) {
            *type = MD_LINE_LIST;
            i += 2;
            while (raw_line[i] == ' ' || raw_line[i] == '\t') i++;
        }
    } else if (raw_line[i] == '>') {
        *type = MD_LINE_BLOCKQUOTE;
        i++;
        if (raw_line[i] == ' ') i++;
    }
    
    while (raw_line[i] && out_idx < 255) {
        if (raw_line[i] == '*' && raw_line[i+1] == '*') {
            i += 2;
            while (raw_line[i] && !(raw_line[i] == '*' && raw_line[i+1] == '*') && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == '*' && raw_line[i+1] == '*') i += 2;
            continue;
        }
        if ((raw_line[i] == '*' || raw_line[i] == '_') && out_idx > 0 && raw_line[i-1] != '\\') {
            char delim = raw_line[i];
            i++;
            while (raw_line[i] && raw_line[i] != delim && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == delim) i++;
            continue;
        }
        if (raw_line[i] == '`') {
            i++;
            while (raw_line[i] && raw_line[i] != '`' && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == '`') i++;
            continue;
        }
        if (raw_line[i] == '[') {
            i++;
            while (raw_line[i] && raw_line[i] != ']' && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == ']') i++;
            if (raw_line[i] == '(') {
                while (raw_line[i] && raw_line[i] != ')') i++;
                if (raw_line[i] == ')') i++;
            }
            continue;
        }
        output[out_idx++] = raw_line[i++];
    }
    output[out_idx] = 0;
}

static void md_clear_all(void) {
    for (int i = 0; i < MD_MAX_LINES; i++) {
        lines[i].content[0] = 0;
        lines[i].length = 0;
        lines[i].type = MD_LINE_NORMAL;
        lines[i].indent_level = 0;
    }
    line_count = 0;
    scroll_top = 0;
    open_filename[0] = 0;
}

void markdown_open_file(const char *filename) {
    md_clear_all();
    md_strcpy(open_filename, filename);
    
    int fd = sys_open(filename, "r");
    if (fd < 0) return;
    
    static char buffer[MD_MAX_CONTENT];
    int bytes_read = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    
    if (bytes_read <= 0) return;
    buffer[bytes_read] = 0;
    
    int line = 0;
    int col = 0;
    char raw_line[256] = "";
    bool in_code_block = false;
    
    for (int i = 0; i < bytes_read && line < MD_MAX_LINES; i++) {
        char ch = buffer[i];
        if (ch == '\n') {
            raw_line[col] = 0;
            if (raw_line[0] == '`' && raw_line[1] == '`' && raw_line[2] == '`') {
                in_code_block = !in_code_block;
            } else {
                if (in_code_block) {
                    md_strcpy(lines[line].content, raw_line);
                    lines[line].length = md_strlen(raw_line);
                    lines[line].type = MD_LINE_CODE;
                    lines[line].indent_level = 0;
                    line++;
                } else {
                    char parsed_content[256];
                    MDLineType type;
                    int indent;
                    md_parse_line(raw_line, parsed_content, &type, &indent);
                    md_strcpy(lines[line].content, parsed_content);
                    lines[line].length = md_strlen(parsed_content);
                    lines[line].type = type;
                    lines[line].indent_level = indent;
                    line++;
                }
            }
            col = 0;
            raw_line[0] = 0;
        } else if (col < 255) {
            raw_line[col++] = ch;
        }
    }
    
    if (col > 0 && line < MD_MAX_LINES) {
        raw_line[col] = 0;
        if (raw_line[0] == '`' && raw_line[1] == '`' && raw_line[2] == '`') {
        } else if (in_code_block) {
            md_strcpy(lines[line].content, raw_line);
            lines[line].length = md_strlen(raw_line);
            lines[line].type = MD_LINE_CODE;
            lines[line].indent_level = 0;
            line++;
        } else {
            char parsed_content[256];
            MDLineType type;
            int indent;
            md_parse_line(raw_line, parsed_content, &type, &indent);
            md_strcpy(lines[line].content, parsed_content);
            lines[line].length = md_strlen(parsed_content);
            lines[line].type = type;
            lines[line].indent_level = indent;
            line++;
        }
    }
    line_count = line;
}

static void md_draw_text_bold(ui_window_t win, int x, int y, const char *text, uint32_t color) {
    ui_draw_string(win, x, y, text, color);
    ui_draw_string(win, x + 1, y, text, color);
}

static void md_paint(ui_window_t win) {
    int offset_x = 4;
    int offset_y = 0;
    int content_width = win_w - 8;
    int content_height = win_h - 28;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y, content_width, 20, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 4, offset_y + 4, "File", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 50, offset_y + 4, open_filename, COLOR_DARK_TEXT);
    
    int btn_x_up = offset_x + content_width - 50;
    int btn_y = offset_y + 2;
    ui_draw_rounded_rect_filled(win, btn_x_up, btn_y, 20, 16, 4, COLOR_DARK_TITLEBAR);
    ui_draw_string(win, btn_x_up + 6, btn_y, "^", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, btn_x_up + 24, btn_y, 20, 16, 4, COLOR_DARK_TITLEBAR);
    ui_draw_string(win, btn_x_up + 30, btn_y, "v", COLOR_DARK_TEXT);
    
    int content_start_y = offset_y + 24;
    int content_start_x = offset_x + 4;
    int usable_content_width = content_width - 8 - 20;
    int usable_content_height = content_height - 28;
    int max_display_lines = usable_content_height / MD_LINE_HEIGHT;
    
    ui_draw_rounded_rect_filled(win, 4, content_start_y, win_w - 24, usable_content_height, 6, COLOR_DARK_BG);
    
    int display_line = 0;
    int i = scroll_top;
    
    while (i < line_count && display_line < max_display_lines) {
        MDLine *line = &lines[i];
        
        int line_height = MD_LINE_HEIGHT;
        int extra_spacing = 0;
        uint32_t text_color = COLOR_DARK_TEXT;
        bool use_bold = false;
        
        switch (line->type) {
            case MD_LINE_HEADING1:
                line_height = MD_LINE_HEIGHT * 2;
                text_color = 0xFF87CEEB;
                use_bold = true;
                extra_spacing = 4;
                break;
            case MD_LINE_HEADING2:
                line_height = MD_LINE_HEIGHT + 6;
                text_color = 0xFF4A90E2;
                use_bold = true;
                extra_spacing = 2;
                break;
            case MD_LINE_HEADING3:
                line_height = MD_LINE_HEIGHT + 2;
                text_color = 0xFF87CEEB;
                use_bold = false;
                break;
            case MD_LINE_BLOCKQUOTE:
                text_color = 0xFFA0A0A0;
                break;
            case MD_LINE_CODE:
                text_color = 0xFF90EE90;
                break;
            default:
                text_color = COLOR_DARK_TEXT;
                break;
        }
        
        if (display_line + (line_height / MD_LINE_HEIGHT) > max_display_lines) break;
        
        int x_offset = content_start_x + (line->indent_level * 4);
        int available_width = usable_content_width - (line->indent_level * 4);
        int max_chars_per_line = available_width / MD_CHAR_WIDTH;
        if (max_chars_per_line < 1) max_chars_per_line = 1;
        
        const char *text = line->content;
        int text_len = line->length;
        int char_idx = 0;
        int local_display_line = 0;
        int wrapped_line_count = 0;
        
        while (char_idx < text_len || (text_len == 0 && local_display_line == 0)) {
            int line_y = content_start_y + display_line * MD_LINE_HEIGHT + (local_display_line * MD_LINE_HEIGHT);
            
            char line_segment[256];
            int segment_len = 0;
            int segment_start = char_idx;
            
            while (char_idx < text_len && segment_len < max_chars_per_line) {
                line_segment[segment_len++] = text[char_idx++];
            }
            line_segment[segment_len] = 0;
            
            if (char_idx < text_len && segment_len > 0) {
                int last_space = -1;
                for (int j = segment_len - 1; j >= 0; j--) {
                    if (line_segment[j] == ' ') {
                        last_space = j; break;
                    }
                }
                if (last_space > 0) {
                    segment_len = last_space;
                    line_segment[segment_len] = 0;
                    char_idx = segment_start + last_space + 1;
                    while (char_idx < text_len && text[char_idx] == ' ') char_idx++;
                }
            }
            
            if (line->type == MD_LINE_CODE && segment_len > 0) {
                ui_draw_rect(win, x_offset - 2, line_y - 2, (segment_len * MD_CHAR_WIDTH) + 4, 12, COLOR_BLACK);
            }

            if (local_display_line == 0) {
                switch (line->type) {
                    case MD_LINE_LIST:
                        ui_draw_rect(win, x_offset, line_y + MD_LINE_HEIGHT/2 - 1, 2, 2, COLOR_BLACK);
                        x_offset += 12;
                        if (segment_len > 0 && line_segment[0] == ' ') {
                            for (int j = 0; j < segment_len - 1; j++) line_segment[j] = line_segment[j + 1];
                            segment_len--;
                        }
                        break;
                    case MD_LINE_BLOCKQUOTE:
                        ui_draw_rect(win, x_offset - 4, line_y, 2, line_height, 0xFF404080);
                        break;
                    default: break;
                }
            }
            
            if (segment_len > 0) {
                if (use_bold) {
                    md_draw_text_bold(win, x_offset, line_y + extra_spacing, line_segment, text_color);
                } else {
                    ui_draw_string(win, x_offset, line_y, line_segment, text_color);
                }
            }
            
            local_display_line++;
            wrapped_line_count++;
            if (char_idx >= text_len) break;
        }
        
        display_line += (wrapped_line_count > 0 ? wrapped_line_count : 1);
        i++;
    }
}

static void md_handle_key(char c, bool pressed) {
    if (!pressed) return;
    if (c == 'w' || c == 'W' || c == 17) {
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
    } else if (c == 's' || c == 'S' || c == 18) {
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
    }
}

static void md_handle_click(int x, int y) {
    int content_width = win_w - 8;
    int btn_x_up = 4 + content_width - 50;
    int btn_y = 2;
    if (x >= btn_x_up && x < btn_x_up + 20 && y >= btn_y && y < btn_y + 16) {
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
    
    int btn_x_down_top = 4 + content_width - 50 + 24;
    if (x >= btn_x_down_top && x < btn_x_down_top + 20 && y >= btn_y && y < btn_y + 16) {
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
}

int main(int argc, char **argv) {
    ui_window_t win = ui_window_create("Markdown Viewer", 150, 180, win_w, win_h);
    if (!win) return 1;

    md_clear_all();
    if (argc > 1) {
        markdown_open_file(argv[1]);
    }

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                md_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
            } else if (ev.type == GUI_EVENT_CLICK) {
                md_handle_click(ev.arg1, ev.arg2);
                md_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
            } else if (ev.type == GUI_EVENT_KEY) {
                md_handle_key((char)ev.arg1, true);
                md_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
            } else if (ev.type == GUI_EVENT_KEYUP) {
                md_handle_key((char)ev.arg1, false);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    return 0;
}
