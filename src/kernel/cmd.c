// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "cmd.h"
#include "graphics.h"
#include "wm.h"
#include "io.h"
#include "rtc.h"

#include "fat32.h"
#include "disk.h"
#include "kutils.h"
#include <stddef.h>
#include "memory_manager.h"
#include "process.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "network.h"
#include "vm.h"
#include "net_defs.h"
#include "man_entries.h"

#define DEFAULT_CMD_COLS 116
#define DEFAULT_CMD_ROWS 41
#define MAX_CMD_COLS 256
// Terminal cell dimensions — fixed to match the 8x8 bitmap font
#define LINE_HEIGHT 10
#define CHAR_WIDTH  8

#define COLOR_RED 0xFFFF0000

#define TXT_BUFFER_SIZE 4096

// --- Structs ---
typedef struct {
    char c;
    uint32_t color;
    uint8_t attrs; // bit 0: bold, bit 1: reverse, bit 2: blink
} CharCell;

typedef enum {
    MODE_SHELL,
    MODE_PAGER
} CmdMode;

// Shell Configuration
typedef struct {
    uint32_t prompt_drive_color;
    uint32_t prompt_colon_color;
    uint32_t prompt_dir_color;
    uint32_t prompt_op_color;
    char prompt_op_char;
    uint32_t default_text_color;
    uint32_t bg_color;
    uint32_t cursor_color;
    bool show_drive;
    bool show_dir;
    uint32_t dir_color;
    uint32_t file_color;
    uint32_t size_color;
    uint32_t error_color;
    uint32_t success_color;
    uint32_t help_color;
    uint32_t command_color;
    char title_text[64];
    char custom_welcome_message[128];
    char startup_cmd[128];
    char prompt_suffix[32];
    bool welcome_msg;
    bool history_save_prompt;
} ShellConfig;

static ShellConfig shell_config;

// CMD Window State (per-window context)
typedef struct {
    char current_drive;
    char current_dir[256];
} CmdState;

// --- State ---
Window win_cmd;

// Shell State
static int terminal_cols = DEFAULT_CMD_COLS;
static int terminal_rows = DEFAULT_CMD_ROWS;
static CharCell *screen_buffer = NULL;
static int cursor_row = 0;
static int cursor_col = 0;
static uint32_t current_color = 0xFFFFFFFF; // Default light text
static uint32_t current_bg_color = 0;        // 0 = translucent/default
static CmdState *cmd_state = NULL;  // Will be set in cmd_init
static int current_prompt_len = 0;

static size_t cmd_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int cmd_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// ANSI State Machine
static int ansi_state = 0;
#define ANSI_MAX_PARAMS 8
static int ansi_params[ANSI_MAX_PARAMS];
static int ansi_param_count = 0;
static bool ansi_private_mode = false;
static int saved_cursor_row = 0;
static int saved_cursor_col = 0;
static uint8_t current_attrs = 0;
static bool cursor_visible = true;
static bool terminal_raw_mode = false;

// Pager State
static CmdMode current_mode = MODE_SHELL;
static char pager_wrapped_lines[2000][MAX_CMD_COLS + 1]; 
static int pager_total_lines = 0;
static int pager_top_line = 0;

// Process Execution State
bool cmd_is_waiting_for_process = false;

// Boot time for uptime
int boot_time_init = 0;

// Output redirection state
static FAT32_FileHandle *redirect_file = NULL;
static char redirect_mode = 0;  // '>' for write, 'a' for append, 0 for normal output
static char redirect_filename[256];
static bool pipe_capture_mode = false;
static char pipe_buffer[8192];
static int pipe_buffer_pos = 0;
static bool pipe_waiting_for_first = false;
static char pipe_next_command[512];
int boot_year, boot_month, boot_day, boot_hour, boot_min, boot_sec;

// Message notification state
static int msg_count = 0;

void cmd_increment_msg_count(void) {
    msg_count++;
}

void cmd_reset_msg_count(void) {
    msg_count = 0;
}

static uint32_t ansi_get_256_color(int n) {
    if (n < 16) {
        static uint32_t base[16] = {
            0xFF000000, 0xFFAA0000, 0xFF00AA00, 0xFFAA5500, 0xFF0000AA, 0xFFAA00AA, 0xFF00AAAA, 0xFFAAAAAA,
            0xFF555555, 0xFFFF5555, 0xFF55FF55, 0xFFFFFF55, 0xFF5555FF, 0xFFFF55FF, 0xFF55FFFF, 0xFFFFFFFF
        };
        return base[n];
    }
    if (n >= 232) {
        uint8_t v = (n - 232) * 10 + 8;
        return 0xFF000000 | (v << 16) | (v << 8) | v;
    }
    n -= 16;
    static uint8_t levels[] = {0, 95, 135, 175, 215, 255};
    int r = levels[n / 36];
    int g = levels[(n / 6) % 6];
    int b = levels[n % 6];
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void ansi_handle_sgr() {
    if (ansi_param_count == 0) {
        current_color = 0xFFFFFFFF;
        current_bg_color = 0;
        current_attrs = 0;
        return;
    }
    for (int i = 0; i < ansi_param_count; i++) {
        int p = ansi_params[i];
        if (p == 0) { 
            current_color = 0xFFFFFFFF; 
            current_bg_color = 0; 
            current_attrs = 0; 
        }
        else if (p == 1) current_attrs |= 1; // Bold
        else if (p == 5) current_attrs |= 4; // Blink
        else if (p == 7) current_attrs |= 2; // Reverse
        else if (p == 22) current_attrs &= ~1; // Normal intensity
        else if (p == 25) current_attrs &= ~4; // Stop blink
        else if (p == 27) current_attrs &= ~2; // Normal (not reverse)
        else if (p >= 30 && p <= 37) current_color = ansi_get_256_color(p - 30);
        else if (p >= 40 && p <= 47) current_bg_color = ansi_get_256_color(p - 40);
        else if (p >= 90 && p <= 97) current_color = ansi_get_256_color(p - 90 + 8);
        else if (p >= 100 && p <= 107) current_bg_color = ansi_get_256_color(p - 100 + 8);
        else if (p == 38 || p == 48) {
            bool is_fg = (p == 38);
            if (i + 2 < ansi_param_count && ansi_params[i+1] == 5) {
                uint32_t c = ansi_get_256_color(ansi_params[i+2]);
                if (is_fg) current_color = c; else current_bg_color = c;
                i += 2;
            } else if (i + 4 < ansi_param_count && ansi_params[i+1] == 2) {
                uint32_t c = 0xFF000000 | ((ansi_params[i+2] & 0xFF) << 16) | ((ansi_params[i+3] & 0xFF) << 8) | (ansi_params[i+4] & 0xFF);
                if (is_fg) current_color = c; else current_bg_color = c;
                i += 4;
            }
        }
    }
}

static void cmd_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static void itoa(int n, char *buf);
static void handle_pipe_chain(const char* right_cmd, bool print_prompt);

static void itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
    buf[i] = 0;
}

// --- Configuration Parsing ---

static bool parse_bool(const char *s) {
    if (!s) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (cmd_strcmp(s, "true") == 0 || cmd_strcmp(s, "1") == 0 || cmd_strcmp(s, "yes") == 0) return true;
    return false;
}

static int parse_int(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int res = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

static uint32_t parse_color(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;

    // 1. HEX Format: #RRGGBB or 0xRRGGBB
    if (s[0] == '#' || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
        const char *hex = (s[0] == '#') ? s + 1 : s + 2;
        uint32_t res = 0;
        int count = 0;
        while (*hex && count < 8) {
            char c = *hex++;
            if (c >= '0' && c <= '9') res = (res << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f') res = (res << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') res = (res << 4) | (c - 'A' + 10);
            else break;
            count++;
        }
        if (count == 6) res |= 0xFF000000;
        return res;
    }

    // 2. RGB Format: rgb(r, g, b)
    if (s[0] == 'r' && s[1] == 'g' && s[2] == 'b') {
        const char *p = s + 3;
        while (*p && *p != '(') p++;
        if (*p == '(') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            int r = parse_int(p);
            while (*p && *p != ',' && *p != ')') p++;
            if (*p == ',') {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                int g = parse_int(p);
                while (*p && *p != ',' && *p != ')') p++;
                if (*p == ',') {
                    p++;
                    while (*p == ' ' || *p == '\t') p++;
                    int b = parse_int(p);
                    return 0xFF000000 | ((uint32_t)(r & 0xFF) << 16) | ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(b & 0xFF);
                }
            }
        }
    }

    // 3. Named Colors (assuming s is trimmed and lowercased)
    if (cmd_strcmp(s, "black") == 0) return 0xFF000000;
    if (cmd_strcmp(s, "white") == 0) return 0xFFFFFFFF;
    if (cmd_strcmp(s, "gray") == 0 || cmd_strcmp(s, "grey") == 0) return 0xFF808080;
    if (cmd_strcmp(s, "lightgray") == 0 || cmd_strcmp(s, "light gray") == 0 || cmd_strcmp(s, "lightgrey") == 0 || cmd_strcmp(s, "light grey") == 0) return 0xFFD3D3D3;
    if (cmd_strcmp(s, "darkgray") == 0 || cmd_strcmp(s, "dark gray") == 0 || cmd_strcmp(s, "darkgrey") == 0 || cmd_strcmp(s, "dark grey") == 0) return 0xFFA9A9A9;
    
    if (cmd_strcmp(s, "red") == 0) return 0xFFFF0000;
    if (cmd_strcmp(s, "lightred") == 0 || cmd_strcmp(s, "light red") == 0) return 0xFFFF7F7F;
    if (cmd_strcmp(s, "darkred") == 0 || cmd_strcmp(s, "dark red") == 0) return 0xFF8B0000;
    
    if (cmd_strcmp(s, "green") == 0) return 0xFF00FF00;
    if (cmd_strcmp(s, "lightgreen") == 0 || cmd_strcmp(s, "light green") == 0) return 0xFF90EE90;
    if (cmd_strcmp(s, "darkgreen") == 0 || cmd_strcmp(s, "dark green") == 0) return 0xFF006400;
    
    if (cmd_strcmp(s, "yellow") == 0) return 0xFFFFFF00;
    if (cmd_strcmp(s, "lightyellow") == 0 || cmd_strcmp(s, "light yellow") == 0) return 0xFFFFFFE0;
    
    if (cmd_strcmp(s, "blue") == 0) return 0xFF0000FF;
    if (cmd_strcmp(s, "lightblue") == 0 || cmd_strcmp(s, "light blue") == 0) return 0xFFADD8E6;
    if (cmd_strcmp(s, "darkblue") == 0 || cmd_strcmp(s, "dark blue") == 0) return 0xFF00008B;
    
    if (cmd_strcmp(s, "magenta") == 0) return 0xFFFF00FF;
    if (cmd_strcmp(s, "lightmagenta") == 0 || cmd_strcmp(s, "light magenta") == 0) return 0xFFFF77FF;
    
    if (cmd_strcmp(s, "cyan") == 0 || cmd_strcmp(s, "aqua") == 0) return 0xFF00FFFF;
    if (cmd_strcmp(s, "lightcyan") == 0 || cmd_strcmp(s, "light cyan") == 0) return 0xFFE0FFFF;
    
    if (cmd_strcmp(s, "orange") == 0) return 0xFFFFA500;
    if (cmd_strcmp(s, "brown") == 0) return 0xFFA52A2A;
    if (cmd_strcmp(s, "pink") == 0) return 0xFFFFC0CB;
    if (cmd_strcmp(s, "purple") == 0) return 0xFF800080;
    if (cmd_strcmp(s, "teal") == 0) return 0xFF008080;
    if (cmd_strcmp(s, "lime") == 0) return 0xFF00FF00;
    if (cmd_strcmp(s, "maroon") == 0) return 0xFF800000;
    if (cmd_strcmp(s, "navy") == 0) return 0xFF000080;
    if (cmd_strcmp(s, "indigo") == 0) return 0xFF4B0082;
    if (cmd_strcmp(s, "violet") == 0) return 0xFFEE82EE;

    // 4. Fallback: Loose Hex (e.g. "FF00FF")
    const char *hex = s;
    uint32_t res = 0;
    int count = 0;
    while (*hex && count < 8) {
        char c = *hex++;
        if (c >= '0' && c <= '9') res = (res << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') res = (res << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') res = (res << 4) | (c - 'A' + 10);
        else break;
        count++;
    }
    if (count == 6) res |= 0xFF000000;
    return res;
}

static void cmd_init_config_defaults(void) {
    shell_config.prompt_drive_color = 0xFFFFFFFF; // Default light gray
    shell_config.prompt_colon_color = 0xFFCCCCCC;
    shell_config.prompt_dir_color = 0xFF569CD6;   // Blue
    shell_config.prompt_op_color = 0xFFCCCCCC;
    shell_config.prompt_op_char = '>';
    shell_config.default_text_color = 0xFFFFFFFF; // White
    shell_config.bg_color = 0xFF1E1E1E;          // Dark background
    shell_config.cursor_color = 0xFFFFFFFF;
    shell_config.show_drive = true;
    shell_config.show_dir = true;
    shell_config.dir_color = 0xFF569CD6;
    shell_config.file_color = 0xFFFFFFFF;
    shell_config.size_color = 0xFF6A9955;         // Green
    shell_config.error_color = 0xFFFF4444;        // Red
    shell_config.success_color = 0xFF6A9955;
    shell_config.help_color = 0xFFCCCCCC;
    shell_config.command_color = 0xFFFFFFFF;     // White typed text
    cmd_strcpy(shell_config.title_text, "Command Prompt");
    cmd_strcpy(shell_config.custom_welcome_message, "");
    cmd_strcpy(shell_config.startup_cmd, "");
    cmd_strcpy(shell_config.prompt_suffix, " ");
    shell_config.welcome_msg = true;
    shell_config.history_save_prompt = true;
}

void cmd_load_config(void) {
    cmd_init_config_defaults();
    
    FAT32_FileHandle *fh = fat32_open("Library/conf/shell.cfg", "r");
    if (!fh) return;
    
    char buffer[4096];
    int bytes = fat32_read(fh, buffer, sizeof(buffer) - 1);
    fat32_close(fh);
    
    if (bytes <= 0) return;
    buffer[bytes] = 0;
    
    char *line = buffer;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        
        char saved = *end;
        *end = 0;
        
        // Skip comments and empty lines
        if (line[0] == '/' && line[1] == '/') {
            line = end + (saved ? 1 : 0);
            if (saved == '\r' && *line == '\n') line++;
            continue;
        }
        
        char *sep = line;
        while (*sep && *sep != '=') sep++;
        
        if (*sep == '=') {
            *sep = 0;
            char *key = line;
            char *val = sep + 1;
            
            // Trim whitespace from key
            char *k_end = sep - 1;
            while (k_end > key && (*k_end == ' ' || *k_end == '\t')) {
                *k_end-- = 0;
            }
            
            // Trim whitespace from value
            while (*val == ' ' || *val == '\t') val++;
            char *v_end = val;
            while (*v_end) v_end++;
            if (v_end > val) {
                v_end--;
                while (v_end > val && (*v_end == ' ' || *v_end == '\t' || *v_end == '\n' || *v_end == '\r')) {
                    *v_end-- = 0;
                }
            }

            // Lowercase value for easier parsing of named colors, EXCEPT for string fields
            if (cmd_strcmp(key, "title_text") != 0 && 
                cmd_strcmp(key, "custom_welcome_message") != 0 &&
                cmd_strcmp(key, "startup_cmd") != 0 &&
                cmd_strcmp(key, "prompt_suffix") != 0) {
                char *p_low = val;
                while (*p_low) {
                    if (*p_low >= 'A' && *p_low <= 'Z') *p_low += 32;
                    p_low++;
                }
            }
            
            if (cmd_strcmp(key, "prompt_drive_color") == 0) shell_config.prompt_drive_color = parse_color(val);
            else if (cmd_strcmp(key, "prompt_colon_color") == 0) shell_config.prompt_colon_color = parse_color(val);
            else if (cmd_strcmp(key, "prompt_dir_color") == 0) shell_config.prompt_dir_color = parse_color(val);
            else if (cmd_strcmp(key, "prompt_op_color") == 0) shell_config.prompt_op_color = parse_color(val);
            else if (cmd_strcmp(key, "prompt_op_char") == 0) {
                if (*val) shell_config.prompt_op_char = *val;
            }
            else if (cmd_strcmp(key, "default_text_color") == 0) shell_config.default_text_color = parse_color(val);
            else if (cmd_strcmp(key, "bg_color") == 0) shell_config.bg_color = parse_color(val);
            else if (cmd_strcmp(key, "cursor_color") == 0) shell_config.cursor_color = parse_color(val);
            else if (cmd_strcmp(key, "show_drive") == 0) shell_config.show_drive = parse_bool(val);
            else if (cmd_strcmp(key, "show_dir") == 0) shell_config.show_dir = parse_bool(val);
            else if (cmd_strcmp(key, "dir_color") == 0) shell_config.dir_color = parse_color(val);
            else if (cmd_strcmp(key, "file_color") == 0) shell_config.file_color = parse_color(val);
            else if (cmd_strcmp(key, "size_color") == 0) shell_config.size_color = parse_color(val);
            else if (cmd_strcmp(key, "error_color") == 0) shell_config.error_color = parse_color(val);
            else if (cmd_strcmp(key, "success_color") == 0) shell_config.success_color = parse_color(val);
            else if (cmd_strcmp(key, "help_color") == 0) shell_config.help_color = parse_color(val);
            else if (cmd_strcmp(key, "command_color") == 0) shell_config.command_color = parse_color(val);
            else if (cmd_strcmp(key, "title_text") == 0) {
                cmd_strcpy(shell_config.title_text, val);
            }
            else if (cmd_strcmp(key, "custom_welcome_message") == 0) {
                cmd_strcpy(shell_config.custom_welcome_message, val);
            }
            else if (cmd_strcmp(key, "startup_cmd") == 0) {
                cmd_strcpy(shell_config.startup_cmd, val);
            }
            else if (cmd_strcmp(key, "prompt_suffix") == 0) {
                cmd_strcpy(shell_config.prompt_suffix, val);
            }
            else if (cmd_strcmp(key, "welcome_msg") == 0) shell_config.welcome_msg = parse_bool(val);
            else if (cmd_strcmp(key, "history_save_prompt") == 0) shell_config.history_save_prompt = parse_bool(val);
        }
        
        line = end + (saved ? 1 : 0);
        if (saved == '\r' && *line == '\n') line++;
    }
}

// System Call Helper
uint32_t cmd_get_config_value(const char *key) {
    if (cmd_strcmp(key, "prompt_drive_color") == 0) return shell_config.prompt_drive_color;
    if (cmd_strcmp(key, "prompt_colon_color") == 0) return shell_config.prompt_colon_color;
    if (cmd_strcmp(key, "prompt_dir_color") == 0) return shell_config.prompt_dir_color;
    if (cmd_strcmp(key, "prompt_op_color") == 0) return shell_config.prompt_op_color;
    if (cmd_strcmp(key, "default_text_color") == 0) return shell_config.default_text_color;
    if (cmd_strcmp(key, "bg_color") == 0) return shell_config.bg_color;
    if (cmd_strcmp(key, "cursor_color") == 0) return shell_config.cursor_color;
    if (cmd_strcmp(key, "dir_color") == 0) return shell_config.dir_color;
    if (cmd_strcmp(key, "file_color") == 0) return shell_config.file_color;
    if (cmd_strcmp(key, "size_color") == 0) return shell_config.size_color;
    if (cmd_strcmp(key, "error_color") == 0) return shell_config.error_color;
    if (cmd_strcmp(key, "success_color") == 0) return shell_config.success_color;
    if (cmd_strcmp(key, "help_color") == 0) return shell_config.help_color;
    if (cmd_strcmp(key, "command_color") == 0) return shell_config.command_color;

    // Return 1 for boolean true values
    if (cmd_strcmp(key, "show_drive") == 0) return shell_config.show_drive;
    if (cmd_strcmp(key, "show_dir") == 0) return shell_config.show_dir;
    if (cmd_strcmp(key, "welcome_msg") == 0) return shell_config.welcome_msg;
    if (cmd_strcmp(key, "history_save_prompt") == 0) return shell_config.history_save_prompt;
    
    return 0;
}

void cmd_set_current_color(uint32_t color) {
    current_color = color;
}

// --- History ---
#define HISTORY_MAX 64
static char cmd_history[HISTORY_MAX][MAX_CMD_COLS + 1];
static int history_head = 0;
static int history_len = 0;
static int history_pos = -1;
static char history_save_buf[MAX_CMD_COLS + 1];

static void cmd_history_add(const char *cmd) {
    if (!cmd || !*cmd) return;
    
    // Don't add if same as last command
    int last_idx = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (history_len > 0 && cmd_strcmp(cmd, cmd_history[last_idx]) == 0) {
        return;
    }

    cmd_strcpy(cmd_history[history_head], cmd);
    history_head = (history_head + 1) % HISTORY_MAX;
    if (history_len < HISTORY_MAX) history_len++;
}

void cmd_print_prompt(void) {
    if (cmd_state) {
        current_prompt_len = 0;
        cursor_col = 0;
        
        if (shell_config.show_drive) {
            current_color = shell_config.prompt_drive_color;
            cmd_putchar(cmd_state->current_drive);
            current_color = shell_config.prompt_colon_color;
            cmd_putchar(':');
        }
        
        if (shell_config.show_dir) {
            current_color = shell_config.prompt_dir_color;
            cmd_write(cmd_state->current_dir);
        }
        
        current_color = shell_config.prompt_op_color;
        cmd_putchar(shell_config.prompt_op_char);
        
        if (shell_config.prompt_suffix[0]) {
            cmd_write(shell_config.prompt_suffix);
        }
        
        current_prompt_len = cursor_col;
        current_color = shell_config.command_color;
    }
}

void cmd_clear_line_content(void) {
    int prompt_len = current_prompt_len;
    for (int i = prompt_len; i < terminal_cols; i++) {
        screen_buffer[cursor_row * terminal_cols + i].c = ' ';
        screen_buffer[cursor_row * terminal_cols + i].color = current_color;
    }
    cursor_col = prompt_len;
}

static void cmd_set_line_content(const char *str) {
    cmd_clear_line_content();
    while (*str && cursor_col < terminal_cols) {
        screen_buffer[cursor_row * terminal_cols + cursor_col].c = *str;
        screen_buffer[cursor_row * terminal_cols + cursor_col].color = current_color;
        cursor_col++;
        str++;
    }
}


static void cmd_scroll_up() {
    for (int r = 1; r < terminal_rows; r++) {
        for (int c = 0; c < terminal_cols; c++) {
            screen_buffer[(r - 1) * terminal_cols + c] = screen_buffer[r * terminal_cols + c];
        }
    }
    // Clear bottom row
    for (int c = 0; c < terminal_cols; c++) {
        screen_buffer[(terminal_rows - 1) * terminal_cols + c].c = ' ';
        screen_buffer[(terminal_rows - 1) * terminal_cols + c].color = shell_config.default_text_color;
    }
}


// Public for CLI apps to use
void cmd_putchar(char c) {
    // If pipe capture mode is enabled, write to pipe buffer
    if (pipe_capture_mode) {
        if (pipe_buffer_pos < (int)sizeof(pipe_buffer) - 1) {
            pipe_buffer[pipe_buffer_pos++] = c;
        }
        return;
    }
    
    // If output is being redirected to a file, write there instead
    if (redirect_file && redirect_mode) {
        fat32_write(redirect_file, &c, 1);
        return;
    }

    if (ansi_state == 0) {
        if (c == '\x1b') {
            ansi_state = 1;
            return;
        } else if (c == '\x07') {
            // BEL - Beep
            k_beep(750, 100);
            return;
        }
    } else if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_param_count = 0;
            for(int i=0; i<ANSI_MAX_PARAMS; i++) ansi_params[i] = 0;
            ansi_private_mode = false;
            return;
        } else if (c == 'O') {
            ansi_state = 3; // SS3
            return;
        } else if (c == 'M') {
            // Reverse Index - scroll up if at top
            if (cursor_row > 0) {
                cursor_row--;
            } else {
                // Scroll screen down
                for (int r = terminal_rows - 1; r > 0; r--) {
                    for (int col = 0; col < terminal_cols; col++) {
                        screen_buffer[r * terminal_cols + col] = screen_buffer[(r - 1) * terminal_cols + col];
                    }
                }
                for (int col = 0; col < terminal_cols; col++) {
                    screen_buffer[0 * terminal_cols + col].c = ' ';
                    screen_buffer[0 * terminal_cols + col].color = shell_config.default_text_color;
                    screen_buffer[0 * terminal_cols + col].attrs = 0;
                }
            }
            ansi_state = 0;
            return;
        } else if (c == '=' || c == '>') {
            // ALT/NORM Keypad mode - absorb
            ansi_state = 0;
            return;
        } else {
            ansi_state = 0; // Unsupported ESC sequence
        }
    } else if (ansi_state == 2) {
        if (c == '?') {
            ansi_private_mode = true;
            return;
        } else if (c >= '0' && c <= '9') {
            if (ansi_param_count < ANSI_MAX_PARAMS) {
                ansi_params[ansi_param_count] = ansi_params[ansi_param_count] * 10 + (c - '0');
            }
            return;
        } else if (c == ';') {
            ansi_param_count++;
            return;
        } else {
            // End of sequence
            if (ansi_param_count < ANSI_MAX_PARAMS) ansi_param_count++;
            
                        if (c == 'm') {
                ansi_handle_sgr();
            } else if (c == 'H' || c == 'f') {
                int r = ansi_params[0] > 0 ? ansi_params[0] - 1 : 0;
                int col = ansi_params[1] > 0 ? ansi_params[1] - 1 : 0;
                if (r >= terminal_rows) r = terminal_rows - 1;
                if (col >= terminal_cols) col = terminal_cols - 1;
                cursor_row = r;
                cursor_col = col;
            } else if (c == 'A') {
                int n = ansi_params[0] > 0 ? ansi_params[0] : 1;
                cursor_row -= n; if (cursor_row < 0) cursor_row = 0;
            } else if (c == 'B') {
                int n = ansi_params[0] > 0 ? ansi_params[0] : 1;
                cursor_row += n; if (cursor_row >= terminal_rows) cursor_row = terminal_rows - 1;
            } else if (c == 'C') {
                int n = ansi_params[0] > 0 ? ansi_params[0] : 1;
                cursor_col += n; if (cursor_col >= terminal_cols) cursor_col = terminal_cols - 1;
            } else if (c == 'D') {
                int n = ansi_params[0] > 0 ? ansi_params[0] : 1;
                cursor_col -= n; if (cursor_col < 0) cursor_col = 0;
            } else if (c == 's') {
                saved_cursor_row = cursor_row;
                saved_cursor_col = cursor_col;
            } else if (c == 'u') {
                cursor_row = saved_cursor_row;
                cursor_col = saved_cursor_col;
            } else if (c == 'J') {
                if (ansi_params[0] == 2) {
                    cmd_screen_clear();
                } else if (ansi_params[0] == 0) {
                    // Erase from cursor to end of screen
                    for (int i = cursor_row * terminal_cols + cursor_col; i < terminal_rows * terminal_cols; i++) {
                        screen_buffer[i].c = ' ';
                        screen_buffer[i].color = current_color;
                        screen_buffer[i].attrs = 0;
                    }
                } else if (ansi_params[0] == 1) {
                    // Erase from beginning of screen to cursor
                    for (int i = 0; i <= cursor_row * terminal_cols + cursor_col; i++) {
                        screen_buffer[i].c = ' ';
                        screen_buffer[i].color = current_color;
                        screen_buffer[i].attrs = 0;
                    }
                }
            } else if (c == 'K') {
                if (ansi_params[0] == 0) {
                    for (int i = cursor_col; i < terminal_cols; i++) {
                        screen_buffer[cursor_row * terminal_cols + i].c = ' ';
                        screen_buffer[cursor_row * terminal_cols + i].color = current_color;
                        screen_buffer[cursor_row * terminal_cols + i].attrs = current_attrs;
                    }
                } else if (ansi_params[0] == 1) {
                    for (int i = 0; i <= cursor_col; i++) {
                        screen_buffer[cursor_row * terminal_cols + i].c = ' ';
                        screen_buffer[cursor_row * terminal_cols + i].color = current_color;
                        screen_buffer[cursor_row * terminal_cols + i].attrs = current_attrs;
                    }
                } else if (ansi_params[0] == 2) {
                    for (int i = 0; i < terminal_cols; i++) {
                        screen_buffer[cursor_row * terminal_cols + i].c = ' ';
                        screen_buffer[cursor_row * terminal_cols + i].color = current_color;
                        screen_buffer[cursor_row * terminal_cols + i].attrs = current_attrs;
                    }
                }
            } else if (c == 'h' && ansi_private_mode) {
                if (ansi_params[0] == 25) cursor_visible = true;
            } else if (c == 'l' && ansi_private_mode) {
                if (ansi_params[0] == 25) cursor_visible = false;
            }
            
            ansi_state = 0;
            return;
        }
    } else if (ansi_state == 3) {
        // SS3 sequences: ESC O <char>
        if (c == 'A') { cursor_row--; if (cursor_row < 0) cursor_row = 0; }
        else if (c == 'B') { cursor_row++; if (cursor_row >= terminal_rows) cursor_row = terminal_rows - 1; }
        else if (c == 'C') { cursor_col++; if (cursor_col >= terminal_cols) cursor_col = terminal_cols - 1; }
        else if (c == 'D') { cursor_col--; if (cursor_col < 0) cursor_col = 0; }
        ansi_state = 0;
        return;
    }
    
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        int spaces = 4 - (cursor_col % 4);
        for (int i = 0; i < spaces; i++) cmd_putchar(' ');
        return;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            screen_buffer[cursor_row * terminal_cols + cursor_col].c = ' ';
            screen_buffer[cursor_row * terminal_cols + cursor_col].color = shell_config.default_text_color;
        }
    } else {
        if (cursor_col >= terminal_cols) {
            cursor_col = 0;
            cursor_row++;
        }
        
        if (cursor_row >= terminal_rows) {
            cmd_scroll_up();
            cursor_row = terminal_rows - 1;
        }

        screen_buffer[cursor_row * terminal_cols + cursor_col].c = c;
        screen_buffer[cursor_row * terminal_cols + cursor_col].color = current_color;
        screen_buffer[cursor_row * terminal_cols + cursor_col].attrs = current_attrs;
        cursor_col++;
    }

    if (cursor_row >= terminal_rows) {
        cmd_scroll_up();
        cursor_row = terminal_rows - 1;
    }
    
    // Trigger repaint so output from syscalls is immediately visible
    wm_mark_dirty(win_cmd.x, win_cmd.y, win_cmd.w, win_cmd.h);
}

void cmd_write_len(const char *str, size_t len);

// Public for CLI apps to use
void cmd_write(const char *str) {
    if (!str) return;
    cmd_write_len(str, cmd_strlen(str));
}

void cmd_write_len(const char *str, size_t len) {
    if (pipe_capture_mode) {
        for (size_t i = 0; i < len && pipe_buffer_pos < (int)sizeof(pipe_buffer) - 1; i++) {
            pipe_buffer[pipe_buffer_pos++] = str[i];
        }
        return;
    }
    
    if (redirect_file && redirect_mode) {
        fat32_write(redirect_file, (void *)str, len);
    } else {
        for (size_t i = 0; i < len; i++) {
            cmd_putchar(str[i]);
        }
    }
}

// Public for CLI apps to use
void cmd_write_int(int n) {
    char buf[32];
    itoa(n, buf);
    cmd_write(buf);
}

void cmd_write_hex(uint64_t n) {
    const char* digits = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = '\0';
    for (int i = 17; i >= 2; i--) {
        buf[i] = digits[n & 0xF];
        n >>= 4;
    }
    cmd_write(buf);
}

int cmd_get_cursor_col(void) {
    return cursor_col;
}

// --- Pager Logic ---

// Public for CLI apps to use - clear the terminal screen
void cmd_screen_clear() {
    for(int r=0; r<terminal_rows; r++) {
        for(int c=0; c<terminal_cols; c++) {
            screen_buffer[r * terminal_cols + c].c = ' ';
            screen_buffer[r * terminal_cols + c].color = COLOR_DARK_TEXT;
            screen_buffer[r * terminal_cols + c].attrs = 0;
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    wm_mark_dirty(win_cmd.x, win_cmd.y, win_cmd.w, win_cmd.h);
}

// Public for CLI apps to use - exit/close the terminal window
void cmd_window_exit() {
    win_cmd.visible = false;
}

// Public for CLI apps to use
void pager_wrap_content(const char **lines, int count) {
    pager_total_lines = 0;
    pager_top_line = 0;
    
    for (int i = 0; i < count; i++) {
        const char *line = lines[i];
        int len = cmd_strlen(line);
        
        if (len == 0) {
            pager_wrapped_lines[pager_total_lines][0] = 0;
            pager_total_lines++;
            continue;
        }
        
        // Intelligent Word Wrap
        int processed = 0;
        while (processed < len) {
            if (pager_total_lines >= 2000) break;

            int remaining = len - processed;
            int chunk_len = remaining;
            if (chunk_len > terminal_cols) chunk_len = terminal_cols;

            // If cutting a word, backtrack to last space
            if (chunk_len < remaining) { // Only check if actually wrapping
                int split_point = chunk_len;
                while (split_point > 0 && line[processed + split_point] != ' ') {
                    split_point--;
                }
                
                if (split_point > 0) {
                    chunk_len = split_point; // Cut at space
                }
                // If split_point == 0, the word is longer than the line, so forced split is okay.
            }

            // Copy chunk
            for (int k = 0; k < chunk_len; k++) {
                pager_wrapped_lines[pager_total_lines][k] = line[processed + k];
            }
            pager_wrapped_lines[pager_total_lines][chunk_len] = 0;
            
            pager_total_lines++;
            processed += chunk_len;
            
            if (processed < len && line[processed] == ' ') {
                processed++;
            }
        }
    }
}

// Public for CLI apps to use
void pager_set_mode(void) {
    current_mode = MODE_PAGER;
}

// Internal LS command to avoid stack overflow in external module
static void cmd_update_dir(const char *path);  // Forward declaration



static void internal_cmd_cd(char *args) {
    // Handle cd with proper cmd_state context
    if (!args || !args[0]) {
        // No argument - show current directory
        if (cmd_state) {
            char drive_str[3];
            drive_str[0] = cmd_state->current_drive;
            drive_str[1] = ':';
            drive_str[2] = 0;
            cmd_write(drive_str);
            cmd_write(cmd_state->current_dir);
            cmd_write("\n");
        } else {
            char cwd[256];
            fat32_get_current_dir(cwd, sizeof(cwd));
            cmd_write("Current directory: ");
            cmd_write(cwd);
            cmd_write("\n");
        }
        return;
    }
    
    // Parse argument (remove trailing spaces/tabs)
    char path[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        path[i] = args[i];
        i++;
    }
    path[i] = 0;
    
    if (cmd_state) {
        // Build full path for validation
        char full_path[512] = {0};
        if (path[1] == ':') {
            // Has drive letter
            cmd_strcpy(full_path, path);
        } else if (path[0] == '/') {
            // Absolute path
            full_path[0] = cmd_state->current_drive;
            full_path[1] = ':';
            int j = 2;
            int k = 0;
            while (path[k] && j < 509) {
                full_path[j++] = path[k++];
            }
            full_path[j] = 0;
        } else {
            // Relative path - resolve from current directory
            full_path[0] = cmd_state->current_drive;
            full_path[1] = ':';
            int j = 2;
            
            // Copy current directory
            const char *dir = cmd_state->current_dir;
            while (*dir && j < 509) {
                full_path[j++] = *dir++;
            }
            
            // Add separator if needed
            if (j > 2 && full_path[j-1] != '/') {
                full_path[j++] = '/';
            }
            
            // Add path argument
            int k = 0;
            while (path[k] && j < 509) {
                full_path[j++] = path[k++];
            }
            full_path[j] = 0;
        }
        
        // Validate directory exists
        if (fat32_is_directory(full_path)) {
            // Normalize the path to resolve .. and .
            char normalized_path[512];
            fat32_normalize_path(full_path, normalized_path);
            
            cmd_update_dir(normalized_path);
            cmd_write("Changed to: ");
            char drive_str[3];
            drive_str[0] = cmd_state->current_drive;
            drive_str[1] = ':';
            drive_str[2] = 0;
            cmd_write(drive_str);
            cmd_write(cmd_state->current_dir);
            cmd_write("\n");
        } else {
            cmd_write("Error: Cannot change to directory: ");
            cmd_write(path);
            cmd_write("\n");
        }
    } else {
        // Fallback to global state if no cmd_state
        if (fat32_chdir(path)) {
            char cwd[256];
            fat32_get_current_dir(cwd, sizeof(cwd));
            cmd_write("Changed to: ");
            cmd_write(cwd);
            cmd_write("\n");
        } else {
            cmd_write("Error: Cannot change to directory: ");
            cmd_write(path);
            cmd_write("\n");
        }
    }
}

static void internal_cmd_txtedit(char *args) {
    // Parse the file path argument
    char filepath[256];
    int i = 0;
    
    // Skip leading whitespace
    while (args && args[i] && (args[i] == ' ' || args[i] == '\t')) {
        i++;
    }
    
    // Extract filepath
    int j = 0;
    while (args && args[i] && args[i] != ' ' && args[i] != '\t' && j < 255) {
        filepath[j++] = args[i++];
    }
    filepath[j] = 0;
    
    // If no filepath provided, show usage
    if (j == 0) {
        cmd_write("Usage: txtedit <filename>\n");
        return;
    }

    // Normalize the path
    char normalized_path[256];
    fat32_normalize_path(filepath, normalized_path);
    
    cmd_write("Opening: ");
    cmd_write(normalized_path);
    cmd_write("\n");

    cmd_is_waiting_for_process = true;
    process_t *proc = process_create_elf("A:/bin/txtedit.elf", normalized_path);
    if (proc) {
        proc->is_terminal_proc = true;
        proc->ui_window = &win_cmd;
    }
}




void cmd_exec_elf(char *args) {
    if (!args || args[0] == '\0') {
        cmd_write("Usage: exec <filename>\n");
        return;
    }

    char full_exec_path[512] = {0};
    int i = 0;
    
    if (args[0] && args[1] != ':') {
        if (cmd_state) {
            full_exec_path[i++] = cmd_state->current_drive;
            full_exec_path[i++] = ':';
            const char *dir = cmd_state->current_dir;
            while (*dir && i < 509) {
                full_exec_path[i++] = *dir++;
            }
            if (i > 2 && full_exec_path[i-1] != '/') {
                full_exec_path[i++] = '/';
            }
        }
        const char *p = args;
        while (*p && i < 509) {
            full_exec_path[i++] = *p++;
        }
        full_exec_path[i] = 0;
    } else {
        // Just copy what they provided
        const char *p = args;
        while (*p && i < 511) {
            full_exec_path[i++] = *p++;
        }
        full_exec_path[i] = 0;
    }
    
    cmd_is_waiting_for_process = true;
    process_t *proc = process_create_elf(full_exec_path, args);
    if (proc) {
        proc->is_terminal_proc = true;
        proc->ui_window = &win_cmd;
    }
}

// Public API for syscall exit 
void cmd_process_finished(void) {
    if (cmd_is_waiting_for_process) {
        cmd_is_waiting_for_process = false;

        if (redirect_file) {
            fat32_close(redirect_file);
            redirect_file = NULL;
            redirect_mode = 0;
            cmd_write("Output redirected to: ");
            cmd_write(redirect_filename);
            cmd_write("\n");
        }

        if (pipe_waiting_for_first) {
            pipe_waiting_for_first = false;
            pipe_capture_mode = false;
            handle_pipe_chain(pipe_next_command, true);
            return;
        }

        if (cursor_col > 0) {
            cmd_putchar('\n');
        }
        cmd_print_prompt();
    }
}

static void internal_cmd_exit(char *args) {
    (void)args;
    cmd_window_exit();
}

static void internal_cmd_reload(char *args) {
    (void)args;
    cmd_load_config();
    win_cmd.title = shell_config.title_text;
    cmd_write("Configuration reloaded.\n");
    cmd_print_prompt();
}

static void internal_cmd_cls(char *args) {
    (void)args;
    cmd_screen_clear();
    cursor_row = 0;
    cursor_col = 0;
    cmd_print_prompt();
}

// Command dispatch table
typedef struct {
    const char *name;
    void (*func)(char *args);
} CommandEntry;

static const CommandEntry commands[] = {
    {"EXEC", cmd_exec_elf},
    {"exec", cmd_exec_elf},
    {"TXTEDIT", internal_cmd_txtedit},
    {"txtedit", internal_cmd_txtedit},
    {"EXIT", internal_cmd_exit},
    {"exit", internal_cmd_exit},
    {"CD", internal_cmd_cd},
    {"cd", internal_cmd_cd},
    {"RELOAD", internal_cmd_reload},
    {"reload", internal_cmd_reload},
    {"CLS", internal_cmd_cls},
    {"cls", internal_cmd_cls},
    {"CLEAR", internal_cmd_cls},
    {"clear", internal_cmd_cls},
    {NULL, NULL}
};




// Helper to sync cmd window directory after cd
static void cmd_update_dir(const char *path) {
    if (!cmd_state || !path) return;
    
    // Extract drive if provided
    const char *p = path;
    char drive = cmd_state->current_drive;
    if (p[0] && p[1] == ':') {
        drive = p[0];
        if (drive >= 'a' && drive <= 'z') drive -= 32;
        p += 2;
    }
    
    // Update drive
    cmd_state->current_drive = drive;
    
    // Update directory
    if (*p) {
        // Remove trailing slashes and copy
        int len = 0;
        while (p[len]) len++;
        while (len > 0 && p[len-1] == '/') len--;
        
        if (len == 0) {
            cmd_state->current_dir[0] = '/';
            cmd_state->current_dir[1] = 0;
        } else {
            for (int i = 0; i < len && i < 255; i++) {
                cmd_state->current_dir[i] = p[i];
            }
            cmd_state->current_dir[len] = 0;
        }
    } else {
        cmd_state->current_dir[0] = '/';
        cmd_state->current_dir[1] = 0;
    }
}

static const char* find_pipe(const char* cmd) {
    while (*cmd) {
        if (*cmd == '|' && *(cmd + 1) == '|') {
            return cmd;
        }
        cmd++;
    }
    return NULL;
}

static void cmd_exec_single(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    // Check for drive switch (e.g. "A:", "B:")
    if (cmd[0] && cmd[1] == ':' && cmd[2] == 0) {
        char letter = cmd[0];
        if (letter >= 'a' && letter <= 'z') letter -= 32;
        
        // Check if drive exists (don't change global, just check)
        if (disk_get_by_letter(letter)) {
            // Update cmd window's drive, not global
            if (cmd_state) {
                cmd_state->current_drive = letter;
                cmd_state->current_dir[0] = '/';
                cmd_state->current_dir[1] = 0;
            }
        } else {
            cmd_write("Invalid drive.\n");
        }
        return;
    }

    char *args_ptr = cmd;
    while (*args_ptr && *args_ptr != ' ') {
        args_ptr++;
    }
    char old_char = *args_ptr;
    *args_ptr = '\0'; // Temporarily terminate to get just the command

    bool is_path = false;
    char *p = cmd;
    while(*p) {
        if (*p == '/' || *p == ':') {
            is_path = true;
            break;
        }
        p++;
    }

    if (is_path) {
        FAT32_FileHandle *fh = fat32_open(cmd, "r");
        if (fh) {
            fat32_close(fh);
            *args_ptr = old_char; // Restore command string

            // Now properly split command and args
            char *args = cmd;
            while (*args && *args != ' ') args++;
            if (*args) {
                *args = 0; // Null terminate cmd
                args++; // Point to start of args
            }

            cmd_is_waiting_for_process = true;
            process_t *proc = process_create_elf(cmd, args);
            if (proc) {
                proc->is_terminal_proc = true;
                proc->ui_window = &win_cmd;
            }
            return;
        }
    }
    
    *args_ptr = old_char; // Restore command string if not found or not a path

    if (cmd[0] == '.' && cmd[1] == '/') {
        char *filename = cmd + 2;
        
        // Build full path with drive context and current directory
        char full_exec_path[512];
        int i = 0;
        
        // Add drive letter
        if (cmd_state) {
            full_exec_path[i++] = cmd_state->current_drive;
            full_exec_path[i++] = ':';
            
            // Add current directory
            const char *dir = cmd_state->current_dir;
            while (*dir && i < 509) {
                full_exec_path[i++] = *dir++;
            }
            
            // Add separator if current dir doesn't end with /
            if (i > 2 && full_exec_path[i-1] != '/') {
                full_exec_path[i++] = '/';
            }
        }
        
        // Add the relative path argument
        const char *p = filename;
        while (*p && i < 509) {
            full_exec_path[i++] = *p++;
        }
        full_exec_path[i] = 0;
        filename = full_exec_path;
        
        FAT32_FileHandle *fh = fat32_open(filename, "r");
        if (fh) {

            uint8_t *buffer = (uint8_t*)kmalloc(VM_MEMORY_SIZE);
            if (!buffer) {
                cmd_write("Error: Out of memory.\n");
                fat32_close(fh);
                return;
            }

            int size = fat32_read(fh, buffer, VM_MEMORY_SIZE);
            fat32_close(fh);
            
            if (size > 0) {
                int res = vm_exec(buffer, size);
                if (res != 0) {
                     cmd_write("Execution failed.\n");
                }
            } else {
                cmd_write("Error: Empty file.\n");
            }
            kfree(buffer);
        } else {
            cmd_write("Error: Command not found or file does not exist.\n");
        }
        return;
    }

    // Split cmd and args
    char *args = cmd;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args = 0; // Null terminate cmd
        args++; // Point to start of args
    }

    // For file system commands, prepend drive context to path args if on different drive
    // Build full path with drive letter for commands that take paths
    char full_path_arg[512] = {0};
    bool is_cd_command = (cmd_strcmp(cmd, "cd") == 0 || cmd_strcmp(cmd, "CD") == 0);
    bool is_ls_command = (cmd_strcmp(cmd, "ls") == 0 || cmd_strcmp(cmd, "LS") == 0);
    bool is_echo_command = (cmd_strcmp(cmd, "echo") == 0 || cmd_strcmp(cmd, "ECHO") == 0);
    
    if (cmd_state) {
        // Check if this is a command that takes a path argument
        bool needs_path = (is_ls_command ||
                          is_cd_command ||
                          cmd_strcmp(cmd, "mkdir") == 0 || cmd_strcmp(cmd, "MKDIR") == 0 ||
                          cmd_strcmp(cmd, "rm") == 0 || cmd_strcmp(cmd, "RM") == 0 ||
                          cmd_strcmp(cmd, "cat") == 0 || cmd_strcmp(cmd, "CAT") == 0 ||
                          is_echo_command ||
                          cmd_strcmp(cmd, "cc") == 0 || cmd_strcmp(cmd, "CC") == 0 ||
                          cmd_strcmp(cmd, "compc") == 0 || cmd_strcmp(cmd, "COMPC") == 0 ||
                          cmd_strcmp(cmd, "touch") == 0 || cmd_strcmp(cmd, "TOUCH") == 0 ||
                          cmd_strcmp(cmd, "cp") == 0 || cmd_strcmp(cmd, "CP") == 0 ||
                          cmd_strcmp(cmd, "mv") == 0 || cmd_strcmp(cmd, "MV") == 0 ||
                          cmd_strcmp(cmd, "txtedit") == 0 || cmd_strcmp(cmd, "TXTEDIT") == 0 ||
                          cmd_strcmp(cmd, "tx") == 0 || cmd_strcmp(cmd, "TX") == 0);
        
        if (needs_path) {
            if (args && args[0]) {
                if (is_echo_command) {
                    char temp_args[512] = {0};
                    int i = 0;
                    int j = 0;
                    bool in_redirect = false;
                    
                    while (args[i] && j < 509) {
                        if (args[i] == '>' && args[i+1] == '>') {
                            // >> redirection
                            temp_args[j++] = '>';
                            temp_args[j++] = '>';
                            i += 2;
                            while (args[i] == ' ') { temp_args[j++] = ' '; i++; }
                            
                            // Prepend drive and directory to filename if relative
                            if (args[i] && args[i+1] != ':') {
                                temp_args[j++] = cmd_state->current_drive;
                                temp_args[j++] = ':';
                                if (args[i] != '/') {
                                    const char *d = cmd_state->current_dir;
                                    while (*d && j < 509) temp_args[j++] = *d++;
                                    if (j > 2 && temp_args[j-1] != '/') temp_args[j++] = '/';
                                }
                            }
                            in_redirect = true;
                        } else if (args[i] == '>' && args[i+1] != '>') {
                            // > redirection
                            temp_args[j++] = '>';
                            i++;
                            while (args[i] == ' ') { temp_args[j++] = ' '; i++; }
                            
                            // Prepend drive and directory to filename if relative
                            if (args[i] && args[i+1] != ':') {
                                temp_args[j++] = cmd_state->current_drive;
                                temp_args[j++] = ':';
                                if (args[i] != '/') {
                                    const char *d = cmd_state->current_dir;
                                    while (*d && j < 509) temp_args[j++] = *d++;
                                    if (j > 2 && temp_args[j-1] != '/') temp_args[j++] = '/';
                                }
                            }
                            in_redirect = true;
                        } else {
                            temp_args[j++] = args[i++];
                        }
                    }
                    (void)in_redirect;
                    temp_args[j] = 0;
                    cmd_strcpy(full_path_arg, temp_args);
                    args = full_path_arg;
                } else if (cmd_strcmp(cmd, "cat") == 0 || cmd_strcmp(cmd, "CAT") == 0 ||
                           cmd_strcmp(cmd, "cc") == 0 || cmd_strcmp(cmd, "CC") == 0 ||
                           cmd_strcmp(cmd, "compc") == 0 || cmd_strcmp(cmd, "COMPC") == 0 ||
                           cmd_strcmp(cmd, "touch") == 0 || cmd_strcmp(cmd, "TOUCH") == 0 ||
                           cmd_strcmp(cmd, "mkdir") == 0 || cmd_strcmp(cmd, "MKDIR") == 0 ||
                           cmd_strcmp(cmd, "rm") == 0 || cmd_strcmp(cmd, "RM") == 0 ||
                           cmd_strcmp(cmd, "ls") == 0 || cmd_strcmp(cmd, "LS") == 0 ||
                           cmd_strcmp(cmd, "cp") == 0 || cmd_strcmp(cmd, "CP") == 0 ||
                           cmd_strcmp(cmd, "mv") == 0 || cmd_strcmp(cmd, "MV") == 0 ||
                           cmd_strcmp(cmd, "txtedit") == 0 || cmd_strcmp(cmd, "TXTEDIT") == 0 ||
                           cmd_strcmp(cmd, "tx") == 0 || cmd_strcmp(cmd, "TX") == 0) {
                    // For filesystem commands: prepend drive and current directory if relative
                    if (args[1] == ':') {
                        // Already has drive letter
                        cmd_strcpy(full_path_arg, args);
                    } else if (args[0] == '/') {
                        // Absolute path, just prepend drive
                        full_path_arg[0] = cmd_state->current_drive;
                        full_path_arg[1] = ':';
                        int i = 2;
                        int j = 0;
                        while (args[j] && i < 509) {
                            full_path_arg[i++] = args[j++];
                        }
                        full_path_arg[i] = 0;
                    } else {
                        // Relative path - need to build from current directory
                        int i = 0;
                        full_path_arg[i++] = cmd_state->current_drive;
                        full_path_arg[i++] = ':';
                        
                        // Add current directory
                        const char *dir = cmd_state->current_dir;
                        while (*dir && i < 509) {
                            full_path_arg[i++] = *dir++;
                        }
                        
                        // Add separator if current dir doesn't end with /
                        if (i > 2 && full_path_arg[i-1] != '/') {
                            full_path_arg[i++] = '/';
                        }
                        
                        // Add the relative path argument
                        int j = 0;
                        while (args[j] && i < 509) {
                            full_path_arg[i++] = args[j++];
                        }
                        full_path_arg[i] = 0;
                    }
                    args = full_path_arg;
                } else if (is_cd_command) {
                    // For cd: build full path with drive + current directory + relative path
                    // Check if args starts with drive letter (e.g., "B:" or "A:")
                    if (args[1] == ':') {
                        // Has drive letter, use as-is
                        cmd_strcpy(full_path_arg, args);
                    } else if (args[0] == '/') {
                        // Absolute path, just prepend drive
                        full_path_arg[0] = cmd_state->current_drive;
                        full_path_arg[1] = ':';
                        int i = 2;
                        int j = 0;
                        while (args[j] && i < 509) {
                            full_path_arg[i++] = args[j++];
                        }
                        full_path_arg[i] = 0;
                    } else {
                        // Relative path - need to build from current directory
                        int i = 0;
                        full_path_arg[i++] = cmd_state->current_drive;
                        full_path_arg[i++] = ':';
                        
                        // Add current directory
                        const char *dir = cmd_state->current_dir;
                        while (*dir && i < 509) {
                            full_path_arg[i++] = *dir++;
                        }
                        
                        // Add separator if current dir doesn't end with /
                        if (i > 2 && full_path_arg[i-1] != '/') {
                            full_path_arg[i++] = '/';
                        }
                        
                        // Add the relative path argument
                        int j = 0;
                        while (args[j] && i < 509) {
                            full_path_arg[i++] = args[j++];
                        }
                        full_path_arg[i] = 0;
                    }
                    args = full_path_arg;
                } else if (args[1] == ':') {
                    // Already has drive letter
                    cmd_strcpy(full_path_arg, args);
                    args = full_path_arg;
                } else {
                    // Add drive letter
                    full_path_arg[0] = cmd_state->current_drive;
                    full_path_arg[1] = ':';
                    int i = 2;
                    int j = 0;
                    while (args[j] && i < 509) {
                        full_path_arg[i++] = args[j++];
                    }
                    full_path_arg[i] = 0;
                    args = full_path_arg;
                }
            } else if (is_ls_command || is_cd_command) {
                // For ls and cd with no args, pass current directory with drive
                full_path_arg[0] = cmd_state->current_drive;
                full_path_arg[1] = ':';
                int i = 2;
                const char *dir = cmd_state->current_dir;
                while (*dir && i < 509) {
                    full_path_arg[i++] = *dir++;
                }
                full_path_arg[i] = 0;
                args = full_path_arg;
            }
        }
    }

    // Use command dispatch table
    for (int i = 0; commands[i].name != NULL; i++) {
        if (cmd_strcmp(cmd, commands[i].name) == 0) {
            commands[i].func(args);
            return;
        }
    }

    // Check for executable in Current Directory or A:/bin/
    char search_path[512];
    
    // Check if the command already ends in .elf (case insensitive)
    bool has_elf_ext = false;
    int cmd_len = cmd_strlen(cmd);
    if (cmd_len > 4) {
        const char *ext = cmd + cmd_len - 4;
        if ((ext[0] == '.' && (ext[1] == 'e' || ext[1] == 'E') && 
            (ext[2] == 'l' || ext[2] == 'L') && (ext[3] == 'f' || ext[3] == 'F'))) {
            has_elf_ext = true;
        }
    }
    
    // 1. Try Current Directory + .elf
    if (cmd_state) {
        int idx = 0;
        search_path[idx++] = cmd_state->current_drive;
        search_path[idx++] = ':';
        const char *dir = cmd_state->current_dir;
        while (*dir && idx < 500) search_path[idx++] = *dir++;
        if (idx > 2 && search_path[idx-1] != '/') search_path[idx++] = '/';
        const char *c = cmd;
        while (*c && idx < 500) search_path[idx++] = *c++;
        if (!has_elf_ext) {
            search_path[idx++] = '.'; search_path[idx++] = 'e'; search_path[idx++] = 'l'; search_path[idx++] = 'f';
        }
        search_path[idx] = 0;
        
        FAT32_FileHandle *fh = fat32_open(search_path, "r");
        if (fh) {
            fat32_close(fh);
            cmd_is_waiting_for_process = true;
            process_t *proc = process_create_elf(search_path, args);
            if (proc) {
                proc->is_terminal_proc = true;
                proc->ui_window = &win_cmd;
            }
            return;
        }
    }
    
    // 2. Try A:/bin/ + .elf
    {
        int idx = 0;
        const char *bin_prefix = "A:/bin/";
        while (*bin_prefix) search_path[idx++] = *bin_prefix++;
        const char *c = cmd;
        while (*c && idx < 500) search_path[idx++] = *c++;
        if (!has_elf_ext) {
            search_path[idx++] = '.'; search_path[idx++] = 'e'; search_path[idx++] = 'l'; search_path[idx++] = 'f';
        }
        search_path[idx] = 0;
        
        FAT32_FileHandle *fh = fat32_open(search_path, "r");
        if (fh) {
            fat32_close(fh);
            cmd_is_waiting_for_process = true;
            process_t *proc = process_create_elf(search_path, args);
            if (proc) {
                proc->is_terminal_proc = true;
                proc->ui_window = &win_cmd;
            }
            return;
        }
    }



    cmd_write("Unknown command: ");
    cmd_write(cmd);
    cmd_write("\n");
}
static void cmd_exec(char *cmd, bool print_prompt);

static void handle_pipe_chain(const char* right_cmd, bool print_prompt) {
    // 1. Trim trailing whitespace from pipe_buffer
    while (pipe_buffer_pos > 0 && 
           (pipe_buffer[pipe_buffer_pos-1] == ' ' || 
            pipe_buffer[pipe_buffer_pos-1] == '\r' || 
            pipe_buffer[pipe_buffer_pos-1] == '\n')) {
        pipe_buffer[--pipe_buffer_pos] = '\0';
    }
    pipe_buffer[pipe_buffer_pos] = '\0';

    if (pipe_buffer_pos == 0) {
        cmd_write("Error: Command produced no output to pipe.\n");
        return;
    }

    // 2. Wrap in quotes if needed
    bool needs_quotes = false;
    if (pipe_buffer[0] != '"' || pipe_buffer[pipe_buffer_pos-1] != '"') {
        needs_quotes = true;
    }
    
    // 3. Construct the new command
    char* full_cmd = (char*)kmalloc(9000); 
    if (!full_cmd) {
        cmd_write("Error: Out of memory for pipe operation.\n");
        return;
    }
    
    cmd_strcpy(full_cmd, right_cmd);
    int len = cmd_strlen(full_cmd);
    full_cmd[len++] = ' ';
    if (needs_quotes) full_cmd[len++] = '"';
    
    for (int i = 0; i < pipe_buffer_pos && len < 8990; i++) {
        full_cmd[len++] = pipe_buffer[i];
    }
    
    if (needs_quotes) full_cmd[len++] = '"';
    full_cmd[len] = '\0';
    
    // 4. Recursive execution
    cmd_exec(full_cmd, print_prompt);
    
    kfree(full_cmd);
}

// Execute command with redirection and pipe support
static void cmd_exec(char *cmd, bool print_prompt) {
    // Check for pipe operator first
    const char* pipe_pos = find_pipe(cmd);
    if (pipe_pos) {
        // Handle piped commands
        char left_cmd[256] = {0};
        char right_cmd[256] = {0};
        
        // Extract left command (before pipe)
        size_t left_len = pipe_pos - cmd;
        if (left_len >= sizeof(left_cmd)) left_len = sizeof(left_cmd) - 1;
        for (size_t j = 0; j < left_len; j++) {
            left_cmd[j] = cmd[j];
        }
        left_cmd[left_len] = '\0';
        
        // Trim trailing spaces from left command
        while (left_len > 0 && left_cmd[left_len - 1] == ' ') {
            left_cmd[--left_len] = '\0';
        }
        
        // Extract right command (after pipe ||)
        const char* right_start = pipe_pos + 2;  // Skip both '|' characters
        while (*right_start == ' ') right_start++;
        
        size_t right_len = 0;
        while (right_start[right_len] && right_len < sizeof(right_cmd) - 1) {
            right_cmd[right_len] = right_start[right_len];
            right_len++;
        }
        right_cmd[right_len] = '\0';
        
        // Trim trailing spaces from right command
        while (right_len > 0 && right_cmd[right_len - 1] == ' ') {
            right_cmd[--right_len] = '\0';
        }
        
        // Generalized pipe implementation
        pipe_buffer_pos = 0;
        pipe_capture_mode = true;
        
        // Execute the left command and capture its output
        cmd_exec_single(left_cmd);
        
        if (cmd_is_waiting_for_process) {
            // Asynchronous: wait for completion in cmd_process_finished
            pipe_waiting_for_first = true;
            cmd_strcpy(pipe_next_command, right_cmd);
            return;
        } else {
            // Synchronous: process immediately
            pipe_capture_mode = false;
            handle_pipe_chain(right_cmd, print_prompt);
            return;
        }
    }
    
    // Check for redirection operators (> or >>)
    char *redirect_ptr = NULL;
    char redirect_op = 0;  // '>' or 'a' for append
    char output_file[256] = {0};
    int cmd_len = 0;
    
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '>' && cmd[i+1] == '>') {
            redirect_ptr = cmd + i + 2;
            redirect_op = 'a';  // append
            cmd_len = i;
            break;
        } else if (cmd[i] == '>') {
            redirect_ptr = cmd + i + 1;
            redirect_op = '>';  // write
            cmd_len = i;
            break;
        }
    }
    
    // If redirection found, set it up
    if (redirect_ptr) {
        // Null terminate command
        cmd[cmd_len] = 0;
        
        // Parse output filename
        int i = 0;
        while (redirect_ptr[i] && (redirect_ptr[i] == ' ' || redirect_ptr[i] == '\t')) {
            i++;
        }
        
        int j = 0;
        while (redirect_ptr[i] && redirect_ptr[i] != ' ' && redirect_ptr[i] != '\t') {
            output_file[j++] = redirect_ptr[i++];
        }
        output_file[j] = 0;
        
        if (!output_file[0]) {
            cmd_write("Error: No output file specified\n");
            return;
        }
        
        cmd_strcpy(redirect_filename, output_file);

        // Open file for redirection
        const char *mode = (redirect_op == 'a') ? "a" : "w";
        redirect_file = fat32_open(output_file, mode);
        if (!redirect_file) {
            cmd_write("Error: Cannot open file for redirection\n");
            return;
        }
        
        redirect_mode = redirect_op;
    }
    
    // Execute the command
    cmd_exec_single(cmd);
    
    // Close redirected file if it was opened
    // If we started a process, we must NOT close the file yet.
    // The file will be closed in cmd_process_finished().
    if (redirect_file && !cmd_is_waiting_for_process) {
        fat32_close(redirect_file);
        redirect_file = NULL;
        redirect_mode = 0;
        cmd_write("Output redirected to: ");
        cmd_write(output_file);
        cmd_write("\n");
    }


    if (print_prompt && !cmd_is_waiting_for_process) {
        cmd_print_prompt();
    }
}


// --- Window Functions ---

static void cmd_paint(Window *win) {
    // Fill background
    draw_rect(win->x + 4, win->y + 30, win->w - 8, win->h - 34, shell_config.bg_color);
    
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    int start_y = offset_y + 4;
    int start_x = offset_x + 4;

    if (current_mode == MODE_PAGER) {
        // Draw Pager Content (Wrapped)
        for (int i = 0; i < terminal_rows && (pager_top_line + i) < pager_total_lines; i++) {
            draw_string(start_x, start_y + (i * LINE_HEIGHT), pager_wrapped_lines[pager_top_line + i], shell_config.default_text_color);
        }
        
        // Status Bar
        // Status Bar
        draw_string(start_x, start_y + (terminal_rows * LINE_HEIGHT), "-- Press Q to quit --", shell_config.default_text_color);
    } else {
        // Draw Cursor
        if (win->focused && cursor_visible) {
            draw_rect(start_x + (cursor_col * CHAR_WIDTH), start_y + (cursor_row * LINE_HEIGHT), 
                      CHAR_WIDTH, LINE_HEIGHT, shell_config.cursor_color);
        }

        // Draw Shell Buffer
        for (int r = 0; r < terminal_rows; r++) {
            for (int c = 0; c < terminal_cols; c++) {
                CharCell cell = screen_buffer[r * terminal_cols + c];
                char ch = cell.c;
                if (ch != 0 && ch != ' ') {
                    uint32_t fg = cell.color;
                    uint32_t bg = shell_config.bg_color;

                    if (cell.attrs & 2) { // Reverse
                        uint32_t tmp = fg;
                        fg = bg;
                        bg = tmp;
                        // Draw background for reversed text
                        draw_rect(start_x + (c * CHAR_WIDTH), start_y + (r * LINE_HEIGHT), 
                                  CHAR_WIDTH, LINE_HEIGHT, bg);
                    }

                    // If cursor is on this character, and cursor color is bright, use background color for char
                    if (r == cursor_row && c == cursor_col && win->focused && cursor_visible) {
                        fg = bg; // Character takes background color when cursor is over it
                    }
                    
                    draw_char_bitmap(start_x + (c * CHAR_WIDTH), start_y + (r * LINE_HEIGHT), ch, fg);
                }
            }
        }
    }

    // Column/Row Indicator in top-right
    char size_buf[32];
    itoa(terminal_cols, size_buf);
    int p = 0; while(size_buf[p]) p++; size_buf[p++] = 'x'; itoa(terminal_rows, size_buf + p);
    draw_string(win->x + win->w - 80, win->y + 5, size_buf, 0xFFAAAAAA);
}

void cmd_handle_resize(Window *win, int w, int h) {
    (void)win;
    int new_cols = (w - 20) / CHAR_WIDTH;
    int new_rows = (h - 50) / LINE_HEIGHT;
    if (new_cols < 20) new_cols = 20;
    if (new_rows < 5) new_rows = 5;

    if (new_cols == terminal_cols && new_rows == terminal_rows) return;

    CharCell *new_buffer = (CharCell*)kmalloc(new_rows * new_cols * sizeof(CharCell));
    for (int i = 0; i < new_rows * new_cols; i++) {
        new_buffer[i].c = ' ';
        new_buffer[i].color = shell_config.default_text_color;
    }

    // Reflow Logic: Copy characters and track cursor
    int new_r = 0;
    int new_c = 0;
    int target_cursor_r = 0;
    int target_cursor_c = 0;

    for (int r = 0; r < terminal_rows; r++) {
        // Find logical end of this line for copying
        int last_c = terminal_cols - 1;
        while (last_c >= 0 && screen_buffer[r * terminal_cols + last_c].c == ' ') last_c--;
        
        // If this is the cursor row, we MUST copy up to the cursor col at least
        if (r == cursor_row) {
            if (last_c < cursor_col) last_c = cursor_col;
        }

        for (int c = 0; c <= last_c; c++) {
            if (r == cursor_row && c == cursor_col) {
                target_cursor_r = new_r;
                target_cursor_c = new_c;
            }

            if (new_c >= new_cols) {
                new_c = 0;
                new_r++;
            }
            if (new_r < new_rows) {
                new_buffer[new_r * new_cols + new_c] = screen_buffer[r * terminal_cols + c];
                new_c++;
            }
        }

        // If it was the cursor row, we've found our target position
        if (r == cursor_row) break;

        // Force newline after each source line if it wasn't a wrap
        if (last_c < terminal_cols - 1) {
            new_c = 0;
            new_r++;
        }
    }

    kfree(screen_buffer);
    screen_buffer = new_buffer;
    terminal_rows = new_rows;
    terminal_cols = new_cols;
    
    cursor_row = target_cursor_r;
    cursor_col = target_cursor_c;
    if (cursor_row >= terminal_rows) cursor_row = terminal_rows - 1;
    if (cursor_col >= terminal_cols) cursor_col = terminal_cols - 1;
}

void cmd_handle_click(Window *win, int x, int y) {
    (void)win;
    // Basic mouse support: convert pixel to char coords
    int c = (x - 10) / CHAR_WIDTH;
    int r = (y - 25) / LINE_HEIGHT;
    if (r >= 0 && r < terminal_rows && c >= 0 && c < terminal_cols) {
        // If telnet or other app wants mouse reporting, we'd handle it here
    }
}

static void cmd_handle_close(Window *win) {
    if (!win) return;
    
    extern process_t* process_get_by_ui_window(void *win);
    extern void process_terminate(process_t *proc);
    
    // Find any process associated with this terminal window and terminate it
    process_t *proc = process_get_by_ui_window(win);
    if (proc) {
        process_terminate(proc);
    }
    
    win->visible = false;
}

void cmd_set_raw_mode(bool enabled) {
    terminal_raw_mode = enabled;
}

static void cmd_key(Window *target, char c, bool pressed) {
    if (!pressed) return;
    (void)target;
    
    if (terminal_raw_mode) {
        // Find the process associated with this terminal and push a key event
        extern process_t* process_get_by_ui_window(void* win);
        extern void process_push_gui_event(process_t *proc, gui_event_t *ev);
        process_t *proc = process_get_by_ui_window(&win_cmd);
        if (proc && proc->is_terminal_proc) {
            gui_event_t ev;
            ev.type = GUI_EVENT_KEY;
            ev.arg1 = c;
            process_push_gui_event(proc, &ev);
            return;
        }
    }

    if (current_mode == MODE_PAGER) {
        if (c == 'q' || c == 'Q') {
            current_mode = MODE_SHELL;
        } else if (c == 17) { // UP
            if (pager_top_line > 0) pager_top_line--;
        } else if (c == 18) { // DOWN
            if (pager_top_line < pager_total_lines - terminal_rows) pager_top_line++;
        }
        return;
    }

    // Shell Mode
     if (c == '\n') { // Enter
          char cmd_buf[512]; // Use a fixed large enough buffer or dynamic
          int len = 0;
          int prompt_len = current_prompt_len;
          
          for (int i = prompt_len; i < terminal_cols; i++) {
              char ch = screen_buffer[cursor_row * terminal_cols + i].c;
              if (ch == 0 || (ch == ' ' && i > prompt_len && (i + 1 < terminal_cols) && screen_buffer[cursor_row * terminal_cols + i + 1].c == 0)) break;
              if (len < 511) cmd_buf[len++] = ch;
          }
          while (len > 0 && cmd_buf[len-1] == ' ') len--;
          cmd_buf[len] = 0;

         cmd_putchar('\n');
         current_color = shell_config.default_text_color;
         
         if (len > 0) cmd_history_add(cmd_buf);
         history_pos = -1;
         
         cmd_exec(cmd_buf, true);
         
         if (!cmd_is_waiting_for_process) {
             // cmd_print_prompt(); // This is now handled by cmd_exec
         }
    } else if (c == 17) { // UP
        if (history_len > 0) {
            if (history_pos == -1) {
                // Save current line
                int len = 0;
                int prompt_len = current_prompt_len;
                for (int i = prompt_len; i < terminal_cols; i++) {
                    char ch = screen_buffer[cursor_row * terminal_cols + i].c;
                    if (ch == 0 || (ch == ' ' && i > prompt_len && (i + 1 < terminal_cols) && screen_buffer[cursor_row * terminal_cols + i + 1].c == 0)) break;
                    history_save_buf[len++] = ch;
                }
                while (len > 0 && history_save_buf[len-1] == ' ') len--;
                history_save_buf[len] = 0;
                
                history_pos = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
            } else {
                int oldest = (history_head - history_len + HISTORY_MAX) % HISTORY_MAX;
                if (history_pos != oldest) {
                    history_pos = (history_pos - 1 + HISTORY_MAX) % HISTORY_MAX;
                }
            }
            cmd_set_line_content(cmd_history[history_pos]);
        }
    } else if (c == 18) { // DOWN
        if (history_pos != -1) {
            int newest = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
            if (history_pos == newest) {
                history_pos = -1;
                cmd_set_line_content(history_save_buf);
            } else {
                history_pos = (history_pos + 1) % HISTORY_MAX;
                cmd_set_line_content(cmd_history[history_pos]);
            }
        }
    } else if (c == 19) { // LEFT
        if (cursor_col > current_prompt_len) {
            cursor_col--;
        }
    } else if (c == 20) { // RIGHT
        if (cursor_col < terminal_cols - 1) {
            cursor_col++;
        }
    } else if (c == '\b') { // Backspace
         if (cursor_col > current_prompt_len) {
             // Shift characters to the left
             for (int i = cursor_col; i < terminal_cols; i++) {
                 screen_buffer[cursor_row * terminal_cols + i - 1] = screen_buffer[cursor_row * terminal_cols + i];
             }
             screen_buffer[cursor_row * terminal_cols + terminal_cols - 1].c = ' ';
             cursor_col--;
             wm_mark_dirty(win_cmd.x, win_cmd.y, win_cmd.w, win_cmd.h);
         }
    } else {
        if (c >= 32 && c <= 126) {
            // Shift characters to the right
            for (int i = terminal_cols - 1; i > cursor_col; i--) {
                screen_buffer[cursor_row * terminal_cols + i] = screen_buffer[cursor_row * terminal_cols + i - 1];
            }
            screen_buffer[cursor_row * terminal_cols + cursor_col].c = c;
            screen_buffer[cursor_row * terminal_cols + cursor_col].color = current_color;
            cursor_col++;
            wm_mark_dirty(win_cmd.x, win_cmd.y, win_cmd.w, win_cmd.h);
        }
    }
}

static void cmd_exec(char *cmd, bool print_prompt);

void cmd_reset(void) {
    // Reset terminal to fresh state
    cmd_screen_clear();
    cmd_load_config();
    
    cursor_row = 0;
    cursor_col = 0;
    current_color = shell_config.default_text_color;
    win_cmd.title = shell_config.title_text;
    
    if (shell_config.welcome_msg) {
        if (shell_config.custom_welcome_message[0]) {
            cmd_write(shell_config.custom_welcome_message);
            cmd_putchar('\n');
        } else {
            cmd_write("BoredOS Command Prompt\n");
        }
    }
    
    if (shell_config.startup_cmd[0]) {
        cmd_exec(shell_config.startup_cmd, false);
    }

    // If a startup command is running, it will print the prompt when it's done.
    // Otherwise, we print it now.
    if (!cmd_is_waiting_for_process) {
        cmd_print_prompt();
    }
}

static void create_ramfs_files(void) {
    if (!fat32_exists("Documents")) fat32_mkdir("Documents");
    if (!fat32_exists("Projects")) fat32_mkdir("Projects");
    if (!fat32_exists("Documents/Important")) fat32_mkdir("Documents/Important");
    if (!fat32_exists("Apps")) fat32_mkdir("Apps");
    if (!fat32_exists("Desktop")) fat32_mkdir("Desktop");
    if (!fat32_exists("RecycleBin")) fat32_mkdir("RecycleBin");
    if (!fat32_exists("Library/conf")) fat32_mkdir("Library/conf");

    // Create default shell configuration file (commented out)
    if (!fat32_exists("Library/conf/shell.cfg")) {
        FAT32_FileHandle *cfg = fat32_open("Library/conf/shell.cfg", "w");
        if (cfg) {
            const char *config_content = 
                "// BoredOS Shell Configuration\n"
                "// Colors: HEX (#RRGGBB), RGB (rgb(r,g,b)), or Names (red, blue, light blue, etc.)\n"
                "// ---------------------------------------------------------------------------\n"
                "// prompt_drive_color=white\n"
                "// prompt_colon_color=gray\n"
                "// prompt_dir_color=light blue\n"
                "// prompt_op_color=white\n"
                "// prompt_op_char=>\n"
                "// prompt_suffix= \n"
                "// default_text_color=light gray\n"
                "// command_color=white\n"
                "// bg_color=#1E1E1E\n"
                "// cursor_color=white\n"
                "// title_text=Command Prompt\n"
                "// custom_welcome_message=\n"
                "// startup_cmd=\n"
                "// welcome_msg=true\n"
                "// show_drive=true\n"
                "// show_dir=true\n"
                "// dir_color=light blue\n"
                "// file_color=white\n"
                "// size_color=light green\n"
                "// error_color=light red\n"
                "// success_color=light green\n"
                "// help_color=light gray\n"
                "// history_save_prompt=true\n";
            fat32_write(cfg, (void *)config_content, cmd_strlen(config_content)); 
            fat32_close(cfg);
        }
    }

    // Create default sysfetch configuration file
    if (!fat32_exists("Library/conf/sysfetch.cfg")) {
        FAT32_FileHandle *cfg = fat32_open("Library/conf/sysfetch.cfg", "w");
        if (cfg) {
            const char *config_content =
                "// BoredOS System Fetch Configuration\n"
                "// ----------------------------------\n"
                "// To use custom ascii art, uncomment the line below and point it to your file.\n"
                "ascii_art_file=A:/Library/art/boredos.txt\n"
                "user_host_string=root@boredos\n"
                "separator=------------\n"
                "\n"
                "// Labels (leave empty to hide a line)\n"
                "os_label=OS\n"
                "kernel_label=Kernel\n"
                "uptime_label=Uptime\n"
                "shell_label=Shell\n"
                "memory_label=Memory\n";
            fat32_write(cfg, (void *)config_content, cmd_strlen(config_content));
            fat32_close(cfg);
        }
    }

    // Create default art directory and file
    if (!fat32_exists("Library/art")) {
        fat32_mkdir("Library/art");
    }
    if (!fat32_exists("Library/art/boredos.txt")) {
        FAT32_FileHandle *art = fat32_open("Library/art/boredos.txt", "w");
        if (art) {
            const char *art_content =
                "\033[35m==================== \033[97m__    ____  ____ \033[0m\n"
                "\033[35m=================== \033[97m/ /_  / __ \\/ ___\\\033[0m\n"
                "\033[34m================== \033[97m/ __ \\/ / / /\\___ \\\033[0m\n"
                "\033[34m================= \033[97m/ /_/ / /_/ /____/ /\033[0m\n"
                "\033[36m================ \033[97m/_.___/\\____//_____/ \033[0m\n"
                "\033[36m===============                       \033[0m\n";
            fat32_write(art, (void *)art_content, cmd_strlen(art_content));
            fat32_close(art);
        }
    }

    
  
    FAT32_FileHandle *fh = fat32_open("Apps/README.md", "w");
    if (fh) {
        const char *content = 
            "# All compiled C files in this directory are openable from any other directory by typing in the name of the compiled file by typing in the name of the compiled file.\n\n"            
            "The c file 'wordofgod.c' contains a C program similar to one in TempleOS, which Terry A. Davis (RIP) saw as 'words from god' telling him what to do with his kernel.\n"
            "I made this file as a tribute to him, as he also inspired me to create this project in '24. If you want to run it you simply do cc (or compc) wordofgod.c and then run ./wordofgod \n";
        fat32_write(fh, (void *)content, cmd_strlen(content));
        fat32_close(fh);
    }    
    
    fh = fat32_open("Documents/notes.txt", "w");
    if (fh) {
        const char *content = "My Notes\n\n- First note\n- Second note\n";
        fat32_write(fh, (void *)content, 39);
        fat32_close(fh);
    }
    
    fh = fat32_open("Projects/project1.txt", "w");
    if (fh) {
        const char *content = "Project 1\n\nStatus: In Progress\n";
        fat32_write(fh, (void *)content, 32);
        fat32_close(fh);
    }

    fat32_open("Desktop/Recycle Bin.shortcut", "w");

    
    fh = fat32_open("Apps/wordofgod.c", "w");
    if (fh) {
        // Buffer the entire file content to write in one go
        // This prevents issues with multiple small writes causing truncation
        char *buf = (char*)kmalloc(8192);
        if (buf) {
            int p = 0;
            const char *strs[] = {
                "int main(){int l;l=malloc(1200);",
                "poke(l+0,\"In \");poke(l+4,\"the \");poke(l+8,\"beginning \");poke(l+12,\"God \");poke(l+16,\"created \");poke(l+20,\"heaven \");poke(l+24,\"and \");poke(l+28,\"earth \");poke(l+32,\"light \");poke(l+36,\"darkness \");",
                "poke(l+40,\"day \");poke(l+44,\"night \");poke(l+48,\"waters \");poke(l+52,\"firmament \");poke(l+56,\"evening \");poke(l+60,\"morning \");poke(l+64,\"land \");poke(l+68,\"seas \");poke(l+72,\"grass \");poke(l+76,\"herb \");",
                "poke(l+80,\"seed \");poke(l+84,\"fruit \");poke(l+88,\"tree \");poke(l+92,\"sun \");poke(l+96,\"moon \");poke(l+100,\"stars \");poke(l+104,\"signs \");poke(l+108,\"seasons \");poke(l+112,\"days \");poke(l+116,\"years \");",
                "poke(l+120,\"creature \");poke(l+124,\"life \");poke(l+128,\"fowl \");poke(l+132,\"whales \");poke(l+136,\"cattle \");poke(l+140,\"creeping \");poke(l+144,\"beast \");poke(l+148,\"man \");poke(l+152,\"image \");poke(l+156,\"likeness \");",
                "poke(l+160,\"dominion \");poke(l+164,\"fish \");poke(l+168,\"air \");poke(l+172,\"every \");poke(l+176,\"CIA \");poke(l+180,\"Epstein Files \");poke(l+184,\"holy \");poke(l+188,\"rest \");poke(l+192,\"dust \");poke(l+196,\"breath \");",
                "poke(l+200,\"soul \");poke(l+204,\"garden \");poke(l+208,\"east \");poke(l+212,\"Eden \");poke(l+216,\"ground \");poke(l+220,\"sight \");poke(l+224,\"good \");poke(l+228,\"evil \");poke(l+232,\"river \");poke(l+236,\"gold \");",
                "poke(l+240,\"stone \");poke(l+244,\"woman \");poke(l+248,\"wife \");poke(l+252,\"flesh \");poke(l+256,\"bone \");poke(l+260,\"naked \");poke(l+264,\"serpent \");poke(l+268,\"subtle \");poke(l+272,\"eat \");poke(l+276,\"eyes \");",
                "poke(l+280,\"wise \");poke(l+284,\"cool \");poke(l+288,\"voice \");poke(l+292,\"fear \");poke(l+296,\"hid \");poke(l+300,\"cursed \");poke(l+304,\"belly \");poke(l+308,\"enmity \");poke(l+312,\"sorrow \");poke(l+316,\"conception \");",
                "poke(l+320,\"children \");poke(l+324,\"desire \");poke(l+328,\"husband \");poke(l+332,\"lava \");poke(l+336,\"thistles \");poke(l+340,\"sweat \");poke(l+344,\"bread \");poke(l+348,\"mother \");poke(l+352,\"skin \");poke(l+356,\"coats \");",
                "poke(l+360,\"cherubims \");poke(l+364,\"sword \");poke(l+368,\"gate \");poke(l+372,\"offering \");poke(l+376,\"obsidian \");poke(l+380,\"sin \");poke(l+384,\"door \");poke(l+388,\"blood \");poke(l+392,\"brother \");poke(l+396,\"keeper \");",
                "poke(l+400,\"voice \");poke(l+404,\"heard \");poke(l+408,\"walking \");poke(l+412,\"cool \");poke(l+416,\"day \");poke(l+420,\"where \");poke(l+424,\"art \");poke(l+428,\"thou \");poke(l+432,\"told \");poke(l+436,\"thee \");",
                "poke(l+440,\"hast \");poke(l+444,\"eaten \");poke(l+448,\"tree \");poke(l+452,\"minecraft \");poke(l+456,\"commanded \");poke(l+460,\"shouldest \");poke(l+464,\"not \");poke(l+468,\"eat \");poke(l+472,\"gave \");poke(l+476,\"me \");",
                "poke(l+480,\"beguiled \");poke(l+484,\"belly \");poke(l+488,\"go \");poke(l+492,\"dust \");poke(l+496,\"shalt \");poke(l+500,\"eat \");poke(l+504,\"days \");poke(l+508,\"life \");poke(l+512,\"put \");poke(l+516,\"enmity \");",
                "poke(l+520,\"between \");poke(l+524,\"seed \");poke(l+528,\"ICE \");poke(l+532,\"Detainment Facility \");poke(l+536,\"heel \");poke(l+540,\"multiply \");poke(l+544,\"sorrow \");poke(l+548,\"conception \");poke(l+552,\"forth \");poke(l+556,\"children \");",
                "poke(l+560,\"desire \");poke(l+564,\"rule \");poke(l+568,\"over \");poke(l+572,\"sake \");poke(l+576,\"sweat \");poke(l+580,\"face \");poke(l+584,\"till \");poke(l+588,\"return \");poke(l+592,\"ground \");poke(l+596,\"taken \");",
                "poke(l+600,\"mother \");poke(l+604,\"living \");poke(l+608,\"coats \");poke(l+612,\"skins \");poke(l+616,\"clothed \");poke(l+620,\"become \");poke(l+624,\"one \");poke(l+628,\"us \");poke(l+632,\"know \");poke(l+636,\"good \");",
                "poke(l+640,\"evil \");poke(l+644,\"lest \");poke(l+648,\"put \");poke(l+652,\"hand \");poke(l+656,\"take \");poke(l+660,\"live \");poke(l+664,\"ever \");poke(l+668,\"sent \");poke(l+672,\"garden \");poke(l+676,\"eden \");",
                "poke(l+680,\"flaming \");poke(l+684,\"sword \");poke(l+688,\"turned \");poke(l+692,\"way \");poke(l+696,\"knew \");poke(l+700,\"conceived \");poke(l+704,\"bare \");poke(l+708,\"cain \");poke(l+712,\"said \");poke(l+716,\"gotten \");",
                "poke(l+720,\"lord \");poke(l+724,\"again \");poke(l+728,\"abel \");poke(l+732,\"sheep \");poke(l+736,\"tiller \");poke(l+740,\"process \");poke(l+744,\"time \");poke(l+748,\"pass \");poke(l+752,\"brought \");poke(l+756,\"fruit \");",
                "poke(l+760,\"offering \");poke(l+764,\"firstlings \");poke(l+768,\"flock \");poke(l+772,\"fat \");poke(l+776,\"thereof \");poke(l+780,\"respect \");poke(l+784,\"wroth \");poke(l+788,\"countenance \");poke(l+792,\"fallen \");poke(l+796,\"well \");",
                "poke(l+800,\"accepted \");poke(l+804,\"not \");poke(l+808,\"sin \");poke(l+812,\"lieth \");poke(l+816,\"door \");poke(l+820,\"unto \");poke(l+824,\"rule \");poke(l+828,\"talked \");poke(l+832,\"field \");poke(l+836,\"rose \");",
                "poke(l+840,\"slew \");poke(l+844,\"done \");poke(l+848,\"crieth \");poke(l+852,\"mouth \");poke(l+856,\"receive \");poke(l+860,\"strength \");poke(l+864,\"fugitive \");poke(l+868,\"vagabond \");poke(l+872,\"punishment \");poke(l+876,\"greater \");",
                "poke(l+880,\"bear \");poke(l+884,\"driven \");poke(l+888,\"hid \");poke(l+892,\"findeth \");poke(l+896,\"slay \");poke(l+900,\"vengeance \");poke(l+904,\"sevenfold \");poke(l+908,\"mark \");poke(l+912,\"finding \");poke(l+916,\"kill \");",
                "poke(l+920,\"presence \");poke(l+924,\"dwelt \");poke(l+928,\"nod \");poke(l+932,\"enoch \");poke(l+936,\"city \");poke(l+940,\"irad \");poke(l+944,\"mehujael \");poke(l+948,\"methusael \");poke(l+952,\"lamech \");poke(l+956,\"adah \");",
                "poke(l+960,\"zillah \");poke(l+964,\"jabal \");poke(l+968,\"tent \");poke(l+972,\"cattle \");poke(l+976,\"jubal \");poke(l+980,\"harp \");poke(l+984,\"organ \");poke(l+988,\"tubalcain \");poke(l+992,\"brass \");poke(l+996,\"iron \");",
                "poke(l+1000,\"naamah \");poke(l+1004,\"wives \");poke(l+1008,\"hear \");poke(l+1012,\"speech \");poke(l+1016,\"hearken \");poke(l+1020,\"young \");poke(l+1024,\"hurt \");poke(l+1028,\"wounding \");poke(l+1032,\"avenged \");poke(l+1036,\"seventy \");",
                "poke(l+1040,\"seth \");poke(l+1044,\"appointed \");poke(l+1048,\"enos \");poke(l+1052,\"began \");poke(l+1056,\"call \");poke(l+1060,\"name \");poke(l+1064,\"generations \");poke(l+1068,\"adam \");poke(l+1072,\"likeness \");poke(l+1076,\"blessed \");",
                "poke(l+1080,\"begat \");poke(l+1084,\"sons \");poke(l+1088,\"daughters \");poke(l+1092,\"lived \");poke(l+1096,\"died \");poke(l+1100,\"cainan \");poke(l+1104,\"mahalaleel \");poke(l+1108,\"jared \");poke(l+1112,\"walked \");poke(l+1116,\"three \");",
                "poke(l+1120,\"hundred \");poke(l+1124,\"sixty \");poke(l+1128,\"five \");poke(l+1132,\"methuselah \");poke(l+1136,\"lamech \");poke(l+1140,\"noah \");poke(l+1144,\"comfort \");poke(l+1148,\"work \");poke(l+1152,\"toil \");poke(l+1156,\"hands \");",
                "poke(l+1160,\"shem \");poke(l+1164,\"ham \");poke(l+1168,\"japheth \");poke(l+1172,\"men \");poke(l+1176,\"daughters \");poke(l+1180,\"born \");poke(l+1184,\"fair \");poke(l+1188,\"chose \");poke(l+1192,\"spirit \");poke(l+1196,\"strive \");",
                "int c;int r;r=abs(rand());r=r-(r/5)*5;c=14+r;int i;i=0;while(i<c){int x;x=abs(rand());x=x-(x/300)*300;int w;w=peek(l+x*4);print_str(w);i=i+1;}nl();}",
                NULL
            };
            
            for (int i = 0; strs[i]; i++) {
                const char *s = strs[i];
                while (*s && p < 8191) buf[p++] = *s++;
            }
            
            fat32_write(fh, buf, p);
            kfree(buf);
        }
        fat32_close(fh);
    }

    fh = fat32_open("Apps/DOOM.c", "w");
    if (fh) {
        const char *content = 
            "int main(){\n"
            "      puts(\"To DOOM, or not to DOOM.\\n\");\n"
            "      puts(\"-Me\\n\");\n"
            "}\n";
        fat32_write(fh, (void *)content, cmd_strlen(content));
        fat32_close(fh);
    }
}



void cmd_init(void) {
    cmd_init_config_defaults();
    create_ramfs_files();
    create_man_entries();
    
    // Load config after files are created
    cmd_load_config();

    // Default sizes
    terminal_cols = DEFAULT_CMD_COLS;
    terminal_rows = DEFAULT_CMD_ROWS;
    
    // Allocate screen buffer
    screen_buffer = (CharCell*)kmalloc(terminal_cols * terminal_rows * sizeof(CharCell));

    win_cmd.title = shell_config.title_text;
    win_cmd.x = 50;
    win_cmd.y = 50;
    win_cmd.w = (terminal_cols * CHAR_WIDTH) + 20; 
    win_cmd.h = (terminal_rows * LINE_HEIGHT) + 50;
    
    win_cmd.visible = false;
    win_cmd.focused = false;
    win_cmd.z_index = 0;
    win_cmd.paint = cmd_paint;
    win_cmd.handle_key = cmd_key;
    win_cmd.handle_click = cmd_handle_click;
    win_cmd.handle_right_click = NULL;
    win_cmd.handle_close = cmd_handle_close;
    win_cmd.handle_resize = cmd_handle_resize;
    win_cmd.resizable = true;
    
    // Initialize cmd state (per-window context)
    CmdState *state = (CmdState*)kmalloc(sizeof(CmdState));
    if (state) {
        state->current_drive = 'A';
        state->current_dir[0] = '/';
        state->current_dir[1] = 0;
        win_cmd.data = state;
        cmd_state = state;  // Set static pointer
    }
    
    cmd_reset();
    
    if (!boot_time_init) {
        rtc_get_datetime(&boot_year, &boot_month, &boot_day, &boot_hour, &boot_min, &boot_sec);
        boot_time_init = 1;
    }
}
