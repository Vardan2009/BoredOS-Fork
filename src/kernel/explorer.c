#include "explorer.h"
#include "graphics.h"
#include "fat32.h"
#include "disk.h"
#include "wm.h"
#include "memory_manager.h"
#include "editor.h"
#include "markdown.h"
#include "cmd.h"
#include "notepad.h"
#include "calculator.h"
#include "minesweeper.h"
#include "control_panel.h"
#include "about.h"
#include "paint.h"
#include <stdbool.h>
#include <stddef.h>

// === File Explorer State ===
Window win_explorer;

#define EXPLORER_MAX_FILES 64
#define EXPLORER_ITEM_HEIGHT 80
#define EXPLORER_ITEM_WIDTH 120
#define EXPLORER_COLS 4
#define EXPLORER_ROWS 4
#define EXPLORER_PADDING 15

// Dialog states
#define DIALOG_NONE 0
#define DIALOG_CREATE_FILE 1
#define DIALOG_CREATE_FOLDER 2
#define DIALOG_DELETE_CONFIRM 3
#define DIALOG_REPLACE_CONFIRM 4
#define DIALOG_REPLACE_MOVE_CONFIRM 5
#define DIALOG_CREATE_REPLACE_CONFIRM 6
#define DIALOG_INPUT_MAX 256
#define DIALOG_RENAME 8
#define ACTION_RESTORE 108
#define DIALOG_ERROR 7
#define ACTION_CREATE_SHORTCUT 107

static Window* explorer_wins[10];
static int explorer_win_count = 0;

// Dropdown menu state
static int dropdown_menu_item_height = 25;
#define DROPDOWN_MENU_WIDTH 120
#define DROPDOWN_MENU_ITEMS 3

// File context menu state
#define FILE_CONTEXT_MENU_WIDTH 180
#define FILE_CONTEXT_MENU_HEIGHT 50
#define CONTEXT_MENU_ITEM_HEIGHT 25

// Clipboard state
static char clipboard_path[256] = "";
static int clipboard_action = 0; // 0=None, 1=Copy, 2=Cut
#define FILE_CONTEXT_ITEMS 2  // "Open with Text Editor" and "Open with Markdown Viewer"

typedef struct {
    const char *label;
    int action_id; // 100+ for actions
    bool enabled;
    uint32_t color;
} ExplorerContextItem;

// === Helper Functions ===

size_t explorer_strlen(const char *str);
void explorer_strcpy(char *dest, const char *src);
static int explorer_strcmp(const char *s1, const char *s2);
void explorer_strcat(char *dest, const char *src);
static void explorer_load_directory(Window *win, const char *path);
static void explorer_handle_right_click(Window *win, int x, int y);
static void explorer_handle_file_context_menu_click(Window *win, int x, int y);
static void explorer_perform_paste(Window *win, const char *dest_dir);
static void explorer_perform_move_internal(Window *win, const char *source_path, const char *dest_dir);
static bool explorer_copy_recursive(const char *src_path, const char *dest_path);
Window* explorer_create_window(const char *path);

extern bool is_dragging_file;

size_t explorer_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void explorer_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int explorer_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void explorer_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    explorer_strcpy(dest, src);
}

// Get file extension (e.g., "md" from "file.md")
static const char* explorer_get_extension(const char *filename) {
    const char *dot = filename;
    const char *ext = "";
    
    // Find the last dot
    while (*dot) {
        if (*dot == '.') {
            ext = dot + 1;
        }
        dot++;
    }
    
    return ext;
}

// Check if file is markdown
static bool explorer_is_markdown_file(const char *filename) {
    const char *ext = explorer_get_extension(filename);
    return explorer_strcmp(ext, "md") == 0;
}

// Helper to check if string starts with prefix
static bool explorer_str_starts_with(const char *str, const char *prefix) {
    while(*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

// Helper to check if string ends with suffix
static bool explorer_str_ends_with(const char *str, const char *suffix) {
    int str_len = explorer_strlen(str);
    int suf_len = explorer_strlen(suffix);
    if (suf_len > str_len) return false;
    return explorer_strcmp(str + str_len - suf_len, suffix) == 0;
}

// Helper for label drawing (adapted from wm.c)
static void explorer_draw_icon_label(int x, int y, const char *label, uint32_t color) {
    char line1[11] = {0}; // 10 chars + null
    char line2[11] = {0}; // 10 chars + null
    int len = 0; while(label[len]) len++;
    
    if (len <= 10) {
        for (int i = 0; i < len; i++) line1[i] = label[i];
    } else {
        // Dot-based wrap: keep extension together if prefix fits
        int dot_pos = -1;
        for (int i = len - 1; i >= 0; i--) {
            if (label[i] == '.') { dot_pos = i; break; }
        }

        int split = -1;
        if (dot_pos != -1 && dot_pos > 0 && dot_pos <= 10) {
            split = dot_pos;
        } else {
            // Word-based wrap: look for space in the first 11 characters
            for (int i = 10; i >= 0; i--) {
                if (label[i] == ' ') {
                    split = i;
                    break;
                }
            }
        }

        if (split != -1) {
            for (int i = 0; i < split; i++) line1[i] = label[i];
            int start2 = (label[split] == ' ') ? split + 1 : split;
            int j = 0;
            while (label[start2 + j] && j < 10) {
                line2[j] = label[start2 + j];
                j++;
            }
            if (label[start2 + j] != 0) {
                int t = (j > 8) ? 8 : j;
                line2[t] = '.'; line2[t+1] = '.'; line2[t+2] = 0;
            }
        } else {
            for (int i = 0; i < 10; i++) line1[i] = label[i];
            int j = 0;
            while (label[10 + j] && j < 10) {
                line2[j] = label[10 + j];
                j++;
            }
            if (label[10 + j] != 0) {
                int t = (j > 8) ? 8 : j;
                line2[t] = '.'; line2[t+1] = '.'; line2[t+2] = 0;
            }
        }
    }
    
    // Center in EXPLORER_ITEM_WIDTH
    int l1_len = 0; while(line1[l1_len]) l1_len++;
    int l1_w = l1_len * 8;
    draw_string(x + (EXPLORER_ITEM_WIDTH - l1_w)/2, y + 50, line1, color);
    
    if (line2[0]) {
        int l2_len = 0; while(line2[l2_len]) l2_len++;
        int l2_w = l2_len * 8;
        draw_string(x + (EXPLORER_ITEM_WIDTH - l2_w)/2, y + 60, line2, color);
    }
}

// === Dialog and File Operations ===

static bool check_desktop_limit_explorer(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (explorer_str_starts_with(state->current_path, "/Desktop")) {
        // Check if root desktop
        if (explorer_strcmp(state->current_path, "/Desktop") == 0 || explorer_strcmp(state->current_path, "/Desktop/") == 0) { // Check if root desktop
             if (state->item_count >= desktop_max_cols * (desktop_max_rows_per_col > 1 ? desktop_max_rows_per_col - 1 : 0)) {
                 state->dialog_state = DIALOG_ERROR;
                 explorer_strcpy(state->dialog_input, "Desktop is full!");
                 return false;
             }
        }
    }
    return true;
}

static void dialog_open_create_file(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_CREATE_FILE;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    explorer_strcpy(state->dialog_creation_path, path);
}

static void dialog_open_create_folder(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_CREATE_FOLDER;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    explorer_strcpy(state->dialog_creation_path, path);
}

static void dialog_open_delete_confirm(Window *win, int item_idx) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (item_idx < 0 || item_idx >= state->item_count) return;
    
    state->dialog_state = DIALOG_DELETE_CONFIRM;
    state->dialog_target_is_dir = state->items[item_idx].is_directory;
    
    // Build full path to target
    explorer_strcpy(state->dialog_target_path, state->current_path);
    if (state->dialog_target_path[explorer_strlen(state->dialog_target_path) - 1] != '/') {
        explorer_strcat(state->dialog_target_path, "/");
    }
    explorer_strcat(state->dialog_target_path, state->items[item_idx].name);
}

static void dialog_close(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_NONE;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    state->dialog_target_path[0] = 0;
}

static void dialog_confirm_create_file(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (state->dialog_input[0] == 0) return;
    
    if (!check_desktop_limit_explorer(win)) return;
    
    char full_path[256];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    if (fat32_exists(full_path)) {
        state->dialog_state = DIALOG_CREATE_REPLACE_CONFIRM;
        return;
    }
    
    // Create empty file
    FAT32_FileHandle *file = fat32_open(full_path, "w");
    if (file) {
        fat32_close(file);
        explorer_refresh_all();
    }
    
    dialog_close(win);
}

static void dialog_force_create_file(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    char full_path[256];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    FAT32_FileHandle *file = fat32_open(full_path, "w");
    if (file) {
        fat32_close(file);
        explorer_refresh_all();
    }
    dialog_close(win);
}

static void dialog_confirm_create_folder(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (state->dialog_input[0] == 0) return;
    
    if (!check_desktop_limit_explorer(win)) return;
    
    char full_path[256];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    // Create directory
    if (fat32_mkdir(full_path)) {
        explorer_refresh_all();
    }
    
    dialog_close(win);
}

// Recursive delete for directories
bool explorer_delete_permanently(const char *path) {
    if (fat32_is_directory(path)) {
        // List contents and delete recursively
        FAT32_FileInfo *entries = (FAT32_FileInfo*)kmalloc(64 * sizeof(FAT32_FileInfo));
        if (!entries) return false;

        int count = fat32_list_directory(path, entries, 64);
        
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(entries[i].name, ".") == 0 || explorer_strcmp(entries[i].name, "..") == 0) continue;

            char child_path[256];
            explorer_strcpy(child_path, path);
            if (child_path[explorer_strlen(child_path) - 1] != '/') {
                explorer_strcat(child_path, "/");
            }
            explorer_strcat(child_path, entries[i].name);
            
            if (entries[i].is_directory) {
                explorer_delete_permanently(child_path);
            } else {
                fat32_delete(child_path);
            }
        }
        kfree(entries);
        // Delete the directory itself
        return fat32_rmdir(path);
    } else {
        // Regular file
        return fat32_delete(path);
    }
}

bool explorer_delete_recursive(const char *path) {
    if (explorer_str_starts_with(path, "/RecycleBin")) {
        return explorer_delete_permanently(path);
    } else {
        // Move to Recycle Bin
        char filename[256];
        int len = explorer_strlen(path);
        int i = len - 1;
        while (i >= 0 && path[i] != '/') i--;
        int j = 0;
        for (int k = i + 1; k < len; k++) filename[j++] = path[k];
        filename[j] = 0;
        
        // Extract drive from path
        char drive_prefix[3] = "A:";
        if (path[0] && path[1] == ':') {
            drive_prefix[0] = path[0];
        }
        
        char dest_path[256];
        explorer_strcpy(dest_path, drive_prefix);
        explorer_strcat(dest_path, "/RecycleBin/");
        explorer_strcat(dest_path, filename);
        
        // Save origin
        char origin_path[256];
        explorer_strcpy(origin_path, dest_path);
        explorer_strcat(origin_path, ".origin");
        FAT32_FileHandle *fh = fat32_open(origin_path, "w");
        if (fh) {
            fat32_write(fh, path, explorer_strlen(path));
            fat32_close(fh);
        }
        
        // Use copy + delete (permanent) to simulate move
        explorer_copy_recursive(path, dest_path);
        explorer_delete_permanently(path);
        return true;
    }
}

static void dialog_confirm_delete(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_delete_recursive(state->dialog_target_path);
    explorer_refresh_all();
    dialog_close(win);
}

static void dialog_confirm_replace(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_perform_paste(win, state->dialog_dest_dir);
    dialog_close(win);
}

static void dialog_confirm_replace_move(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_perform_move_internal(win, state->dialog_move_src, state->dialog_dest_dir);
    dialog_close(win);
}

// === Clipboard Functions ===

void explorer_clipboard_copy(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 1; // Copy
}

void explorer_clipboard_cut(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 2; // Cut
}

bool explorer_clipboard_has_content(void) {
    return clipboard_action != 0 && clipboard_path[0] != 0;
}

static bool explorer_copy_recursive(const char *src_path, const char *dest_path) {
    if (fat32_is_directory(src_path)) {
        if (!fat32_mkdir(dest_path)) return false;
        FAT32_FileInfo *files = (FAT32_FileInfo*)kmalloc(64 * sizeof(FAT32_FileInfo));
        if (!files) return false;
        
        int count = fat32_list_directory(src_path, files, 64);
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(files[i].name, ".") == 0 || explorer_strcmp(files[i].name, "..") == 0) continue;
            
            char s_sub[256], d_sub[256];
            explorer_strcpy(s_sub, src_path);
            if (s_sub[explorer_strlen(s_sub)-1] != '/') explorer_strcat(s_sub, "/");
            explorer_strcat(s_sub, files[i].name);
            
            explorer_strcpy(d_sub, dest_path);
            if (d_sub[explorer_strlen(d_sub)-1] != '/') explorer_strcat(d_sub, "/");
            explorer_strcat(d_sub, files[i].name);
            
            if (!explorer_copy_recursive(s_sub, d_sub)) { kfree(files); return false; }
        }
        kfree(files);
        return true;
    } else {
        // Copy file
        FAT32_FileHandle *src = fat32_open(src_path, "r");
        FAT32_FileHandle *dst = fat32_open(dest_path, "w");
        bool success = false;
        if (src && dst) {
            uint8_t *buf = (uint8_t*)kmalloc(4096);
            if (buf) {
                int bytes;
                success = true;
                while ((bytes = fat32_read(src, buf, 4096)) > 0) {
                    if (fat32_write(dst, buf, bytes) != bytes) { success = false; break; }
                }
                kfree(buf);
            }
        }
        if (src) fat32_close(src);
        if (dst) fat32_close(dst);
        return success;
    }
}

static void explorer_copy_file_internal(const char *src_path, const char *dest_dir) {
    char filename[256];
    int len = explorer_strlen(src_path);
    int i = len - 1;
    while (i >= 0 && src_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = src_path[k];
    filename[j] = 0;
    
    char dest_path[256];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (explorer_strcmp(src_path, dest_path) == 0) return;
    
    explorer_copy_recursive(src_path, dest_path);
}

static void explorer_perform_paste(Window *win, const char *dest_dir) {
    (void)win;
    explorer_copy_file_internal(clipboard_path, dest_dir);
    
    if (clipboard_action == 2) { // Cut
        // Delete source
        if (fat32_is_directory(clipboard_path)) {
            explorer_delete_permanently(clipboard_path);
        } else {
            fat32_delete(clipboard_path);
        }
        clipboard_action = 0; // Clear clipboard after cut-paste
    }
    explorer_refresh_all();
}

void explorer_clipboard_paste(Window *win, const char *dest_dir) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (!explorer_clipboard_has_content()) return;
    
    // Prevent pasting a directory into itself or its subdirectories
    if (explorer_str_starts_with(dest_dir, clipboard_path)) {
        int src_len = explorer_strlen(clipboard_path);
        if (dest_dir[src_len] == '\0' || dest_dir[src_len] == '/') {
            return;
        }
    }

    // Check for collision
    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(clipboard_path);
    int i = len - 1;
    while (i >= 0 && clipboard_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = clipboard_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (fat32_exists(dest_path)) {
        state->dialog_state = DIALOG_REPLACE_CONFIRM;
        explorer_strcpy(state->dialog_dest_dir, dest_dir);
        return;
    }
    
    explorer_perform_paste(win, dest_dir);
}

void explorer_create_shortcut(Window *win, const char *target_path) {
    ExplorerState *state = (ExplorerState*)win->data;
    char filename[256];
    int len = explorer_strlen(target_path);
    int i = len - 1;
    while (i >= 0 && target_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = target_path[k];
    filename[j] = 0;
    
    char shortcut_path[256];
    explorer_strcpy(shortcut_path, state->current_path);
    if (shortcut_path[explorer_strlen(shortcut_path)-1] != '/') explorer_strcat(shortcut_path, "/");
    explorer_strcat(shortcut_path, filename);
    explorer_strcat(shortcut_path, ".shortcut");
    
    FAT32_FileHandle *fh = fat32_open(shortcut_path, "w");
    if (fh) {
        fat32_write(fh, target_path, explorer_strlen(target_path));
        fat32_close(fh);
        explorer_refresh_all();
    }
}

static void dropdown_menu_toggle(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dropdown_menu_visible = !state->dropdown_menu_visible;
}

// === Context Menu Builder ===
static int explorer_build_context_menu(Window *win, ExplorerContextItem *items_out) {
    ExplorerState *state = (ExplorerState*)win->data;
    int count = 0;
    if (state->file_context_menu_item == -1) {
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            // Dead space in Recycle Bin - no actions for now
            return 0;
        }
        // Dead space
        items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_WHITE : COLOR_DKGRAY};
    } else {
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            items_out[count++] = (ExplorerContextItem){"Restore", ACTION_RESTORE, true, COLOR_BLACK};
            items_out[count++] = (ExplorerContextItem){"Delete Forever", 106, true, COLOR_RED};
            return count;
        }

        bool is_dir = state->items[state->file_context_menu_item].is_directory;
        
        if (!is_dir) {
             items_out[count++] = (ExplorerContextItem){"Open", 100, true, COLOR_WHITE};
             items_out[count++] = (ExplorerContextItem){"Open w/ textedit", 110, true, COLOR_WHITE};
             if (explorer_is_markdown_file(state->items[state->file_context_menu_item].name)) {
        items_out[count++] = (ExplorerContextItem){"Open w/ Markdown", 109, true, COLOR_WHITE};
             }
        }
        
        items_out[count++] = (ExplorerContextItem){"Cut", 104, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Copy", 105, true, COLOR_WHITE};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_WHITE : COLOR_DKGRAY};
            items_out[count++] = (ExplorerContextItem){"Open in new window", 112, true, COLOR_WHITE};
        }
        
        items_out[count++] = (ExplorerContextItem){"Delete", 106, true, COLOR_RED};
        items_out[count++] = (ExplorerContextItem){"Rename", 111, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Create Shortcut", ACTION_CREATE_SHORTCUT, true, COLOR_WHITE};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_WHITE};
            items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_WHITE};
            
            // Only show color options if it's NOT the Recycle Bin folder (i love hardcoding stuff cause it's lowk easier (cry about it))
            if (explorer_strcmp(state->items[state->file_context_menu_item].name, "RecycleBin") != 0) {
                items_out[count++] = (ExplorerContextItem){"---", 0, false, 0}; // Marker
                items_out[count++] = (ExplorerContextItem){"Blue", 200, true, COLOR_APPLE_BLUE};
                items_out[count++] = (ExplorerContextItem){"Red", 201, true, COLOR_RED};
                items_out[count++] = (ExplorerContextItem){"Yellow", 202, true, COLOR_APPLE_YELLOW};
                items_out[count++] = (ExplorerContextItem){"Green", 203, true, COLOR_APPLE_GREEN};
                items_out[count++] = (ExplorerContextItem){"Black", 204, true, COLOR_BLACK};
            }
        }
    }
    return count;
}

// === Helper Functions (continued)

// === Explorer Logic ===

static uint32_t explorer_get_folder_color(const char *folder_path) {
    char color_file_path[256];
    explorer_strcpy(color_file_path, folder_path);
    if (color_file_path[explorer_strlen(color_file_path) - 1] != '/') {
        explorer_strcat(color_file_path, "/");
    }
    explorer_strcat(color_file_path, ".color");
    
    FAT32_FileHandle *file = fat32_open(color_file_path, "r");
    if (file) {
        uint32_t color = 0;
        int bytes_read = fat32_read(file, &color, sizeof(uint32_t));
        fat32_close(file);
        if (bytes_read == sizeof(uint32_t)) {
            return color;
        }
    }
    return COLOR_APPLE_YELLOW;
}

static void explorer_set_folder_color(const char *folder_path, uint32_t color) {
    char color_file_path[256];
    explorer_strcpy(color_file_path, folder_path);
    if (color_file_path[explorer_strlen(color_file_path) - 1] != '/') {
        explorer_strcat(color_file_path, "/");
    }
    explorer_strcat(color_file_path, ".color");
    
    FAT32_FileHandle *file = fat32_open(color_file_path, "w");
    if (file) {
        fat32_write(file, &color, sizeof(uint32_t));
        fat32_close(file);
    }
}

static void explorer_restore_file(Window *win, int item_idx) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (item_idx < 0 || item_idx >= state->item_count) return;
    
    char recycle_path[256];
    explorer_strcpy(recycle_path, state->current_path);
    if (recycle_path[explorer_strlen(recycle_path) - 1] != '/') explorer_strcat(recycle_path, "/");
    explorer_strcat(recycle_path, state->items[item_idx].name);
    
    char origin_file_path[256];
    explorer_strcpy(origin_file_path, recycle_path);
    explorer_strcat(origin_file_path, ".origin");
    
    char original_path[256] = {0};
    FAT32_FileHandle *fh = fat32_open(origin_file_path, "r");
    if (fh) {
        int len = fat32_read(fh, original_path, 255);
        if (len > 0) original_path[len] = 0;
        fat32_close(fh);
    }
    
    if (original_path[0] == 0) return; // No origin info
    
    // Restore
    explorer_copy_recursive(recycle_path, original_path);
    explorer_delete_permanently(recycle_path);
    fat32_delete(origin_file_path);
    
    explorer_refresh_all();
}

static void explorer_load_directory(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    bool path_changed = (explorer_strcmp(state->current_path, path) != 0);
    explorer_strcpy(state->current_path, path);
    
    FAT32_FileInfo *entries = (FAT32_FileInfo*)kmalloc(EXPLORER_MAX_FILES * sizeof(FAT32_FileInfo));
    if (!entries) return;

    int count = fat32_list_directory(path, entries, EXPLORER_MAX_FILES);
    
    state->item_count = 0;
    for (int i = 0; i < count && i < EXPLORER_MAX_FILES; i++) {
        // Skip .color files
        if (explorer_strcmp(entries[i].name, ".color") == 0) {
            continue;
        }
        
        // Skip .origin files
        if (explorer_str_ends_with(entries[i].name, ".origin")) {
            continue;
        }

        explorer_strcpy(state->items[state->item_count].name, entries[i].name);
        state->items[state->item_count].is_directory = entries[i].is_directory;
        state->items[state->item_count].size = entries[i].size;
        
        if (state->items[state->item_count].is_directory) {
            char subfolder_path[256];
            explorer_strcpy(subfolder_path, state->current_path);
            if (subfolder_path[explorer_strlen(subfolder_path) - 1] != '/') {
                explorer_strcat(subfolder_path, "/");
            }
            explorer_strcat(subfolder_path, state->items[state->item_count].name);
            state->items[state->item_count].color = explorer_get_folder_color(subfolder_path);
        } else {
            state->items[state->item_count].color = COLOR_APPLE_YELLOW;
        }
        state->item_count++;
    }
    
    kfree(entries);
    if (path_changed) {
        state->selected_item = -1;
        state->explorer_scroll_row = 0;
    }
}

static void explorer_navigate_to(Window *win, const char *dirname) {
    ExplorerState *state = (ExplorerState*)win->data;
    char new_path[256];
    
    if (explorer_strcmp(dirname, "..") == 0) {
        // Go to parent directory
        int len = explorer_strlen(state->current_path);
        int i = len - 1;
        
        // Skip trailing slashes
        while (i > 0 && state->current_path[i] == '/') i--;
        
        // Find last slash
        while (i > 0 && state->current_path[i] != '/') i--;
        
        if (i == 0) {
            explorer_strcpy(new_path, "/");
        } else {
            for (int j = 0; j < i; j++) {
                new_path[j] = state->current_path[j];
            }
            new_path[i] = 0;
        }
    } else {
        // Go to subdirectory
        explorer_strcpy(new_path, state->current_path);
        if (new_path[explorer_strlen(new_path) - 1] != '/') {
            explorer_strcat(new_path, "/");
        }
        explorer_strcat(new_path, dirname);
    }
    
    explorer_load_directory(win, new_path);
}

void explorer_open_directory(const char *path) {
    explorer_create_window(path);
}

static void explorer_open_target(const char *path) {
    if (fat32_is_directory(path)) {
        explorer_open_directory(path);
    } else {
        if (explorer_is_markdown_file(path)) {
            wm_bring_to_front(&win_markdown);
            markdown_open_file(path);
        } else if (explorer_str_ends_with(path, ".pnt")) {
            paint_load(path);
            wm_bring_to_front(&win_paint);
        } else {
            wm_bring_to_front(&win_editor);
            editor_open_file(path);
        }
    }
}

static void explorer_open_item(Window *win, int index) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (index < 0 || index >= state->item_count) return;

    if (state->items[index].is_directory) {
        explorer_navigate_to(win, state->items[index].name);
        return;
    }

    char full_path[256];
    explorer_strcpy(full_path, state->current_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->items[index].name);

    // Check if shortcut
    if (explorer_str_ends_with(state->items[index].name, ".shortcut")) {
        Window *target = NULL;
        if (explorer_strcmp(state->items[index].name, "Notepad.shortcut") == 0) {
            target = &win_notepad; notepad_reset();
        } else if (explorer_strcmp(state->items[index].name, "Calculator.shortcut") == 0) {
            target = &win_calculator;
        } else if (explorer_strcmp(state->items[index].name, "Terminal.shortcut") == 0) {
            target = &win_cmd; cmd_reset();
        } else if (explorer_strcmp(state->items[index].name, "Minesweeper.shortcut") == 0) {
            target = &win_minesweeper;
        } else if (explorer_strcmp(state->items[index].name, "Control Panel.shortcut") == 0) {
            target = &win_control_panel;
        } else if (explorer_strcmp(state->items[index].name, "About.shortcut") == 0) {
            target = &win_about;
        } else if (explorer_strcmp(state->items[index].name, "Explorer.shortcut") == 0) {
            explorer_open_directory("/"); return;
        } else if (explorer_strcmp(state->items[index].name, "Recycle Bin.shortcut") == 0) {
            explorer_open_directory("/RecycleBin"); return;
        }

        if (target) {
            wm_bring_to_front(target);
            return;
        }

        // Generic shortcut
        FAT32_FileHandle *fh = fat32_open(full_path, "r");
        if (fh) {
            char buf[256];
            int len = fat32_read(fh, buf, 255);
            fat32_close(fh);
            if (len > 0) {
                buf[len] = 0;
                explorer_open_target(buf);
                return;
            }
        }
    }

    // Default open
    explorer_open_target(full_path);
}

// Draw a simple file icon
static void explorer_draw_file_icon(int x, int y, bool is_dir, uint32_t color, const char *filename) {
    if (is_dir) {
        if (explorer_strcmp(filename, "RecycleBin") == 0) {
            // Align with folder body position
            draw_recycle_bin_icon(x - 19, y + 10, "");
            return;
        }
        // Folder icon (colored folder) - Desktop style
        // Folder tab
        draw_rect(x + 10, y + 10, 15, 6, COLOR_LTGRAY);
        draw_rect(x + 10, y + 10, 15, 1, COLOR_BLACK);
        draw_rect(x + 10, y + 10, 1, 6, COLOR_BLACK);
        draw_rect(x + 24, y + 10, 1, 6, COLOR_BLACK);
        
        // Folder body
        draw_rect(x + 10, y + 16, 25, 15, color);
        draw_rect(x + 10, y + 16, 25, 1, COLOR_BLACK);
        draw_rect(x + 10, y + 16, 1, 15, COLOR_BLACK);
        draw_rect(x + 34, y + 16, 1, 15, COLOR_BLACK);
        draw_rect(x + 10, y + 30, 25, 1, COLOR_BLACK);
    } else if (explorer_str_ends_with(filename, ".shortcut")) {
        // App Shortcut - Draw specific icon
        // Strip extension for check
        // Draw icon at x+5, y+5
        // The draw_*_icon functions in wm.c draw at x, y
        // Pass a label, but avoid text drawn by the icon function inside the explorer item
        // because explorer draws its own text. Pass "" as label.
        if (explorer_strcmp(filename, "Notepad.shortcut") == 0) draw_notepad_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Calculator.shortcut") == 0) draw_calculator_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Terminal.shortcut") == 0) draw_terminal_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Minesweeper.shortcut") == 0) draw_minesweeper_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Control Panel.shortcut") == 0) draw_control_panel_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "About.shortcut") == 0) draw_about_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Explorer.shortcut") == 0) draw_folder_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Recycle Bin.shortcut") == 0) draw_recycle_bin_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "RecycleBin") == 0) draw_recycle_bin_icon(x + 5, y + 5, "");
        else draw_icon(x + 5, y + 5, "");
    } else if (explorer_str_ends_with(filename, ".pnt")) {
        draw_paint_icon(x - 15, y + 10, "");
    } else {
        // Document icon - larger
        draw_rect(x + 12, y + 10, 20, 25, COLOR_WHITE);
        draw_rect(x + 12, y + 10, 20, 2, COLOR_BLACK);
        draw_rect(x + 12, y + 10, 2, 25, COLOR_BLACK);
        draw_rect(x + 30, y + 10, 2, 25, COLOR_BLACK);
        draw_rect(x + 12, y + 33, 20, 2, COLOR_BLACK);
        
        if (explorer_str_ends_with(filename, ".md")) {
            draw_string(x + 14, y + 12, "MD", COLOR_BLACK);
        } else if (explorer_str_ends_with(filename, ".c") || explorer_str_ends_with(filename, ".C")) {
            draw_string(x + 14, y + 12, "C", COLOR_APPLE_BLUE);
        } else {
            // Lines on document
            draw_rect(x + 15, y + 18, 14, 1, COLOR_DKGRAY);
            draw_rect(x + 15, y + 23, 14, 1, COLOR_DKGRAY);
            draw_rect(x + 15, y + 28, 14, 1, COLOR_DKGRAY);
        }
    }
}

// === Paint Function ===

static void explorer_paint(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    DirtyRect dirty = graphics_get_dirty_rect();
    
    // Fill background with dark mode
    draw_rect(offset_x, offset_y, win->w - 8, win->h - 28, COLOR_DARK_BG);
    
    // Draw Drive Button (modern rounded style)
    char drive_label[8];
    // Extract drive from the window's current_path instead of using global current_drive
    char current_drv = 'A';
    if (state->current_path[0] && state->current_path[1] == ':') {
        current_drv = state->current_path[0];
    } else if (state->current_path[0] && (state->current_path[0] >= 'A' && state->current_path[0] <= 'Z')) {
        current_drv = state->current_path[0];
    }
    
    drive_label[0] = '[';
    drive_label[1] = ' ';
    drive_label[2] = current_drv;
    drive_label[3] = ':';
    drive_label[4] = ' ';
    drive_label[5] = 'v';
    drive_label[6] = ' ';
    drive_label[7] = ']';
    
    // Button at x+4, y+4, w=60 (rounded)
    draw_rounded_rect_filled(win->x + 4, offset_y + 4, 60, 30, 6, COLOR_DARK_PANEL);
    draw_string(win->x + 12, offset_y + 12, drive_label, COLOR_DARK_TEXT);

    // Draw path bar (shifted right, rounded, dark mode)
    int path_height = 30;
    int path_x = offset_x + 64;
    int path_w = win->w - 16 - 64;
    draw_rounded_rect_filled(path_x, offset_y + 4, path_w, path_height, 6, COLOR_DARK_PANEL);
    draw_string(path_x + 6, offset_y + 10, "Path", COLOR_DARK_TEXT);
    draw_string(path_x + 46, offset_y + 10, state->current_path, COLOR_DARK_TEXT);
    
    // Draw dropdown menu button (right-aligned, before back button, rounded)
    int dropdown_btn_x = win->x + win->w - 90;
    draw_rounded_rect_filled(dropdown_btn_x, offset_y + 4, 35, 30, 6, COLOR_DARK_PANEL);
    draw_string(dropdown_btn_x + 10, offset_y + 10, "...", COLOR_DARK_TEXT);
    
    // Draw back button (right-aligned, rounded)
    draw_rounded_rect_filled(win->x + win->w - 40, offset_y + 4, 30, 30, 6, COLOR_DARK_PANEL);
    draw_string(win->x + win->w - 32, offset_y + 10, "<", COLOR_DARK_TEXT);
    
    // Draw scroll buttons (left of dropdown, rounded)
    draw_rounded_rect_filled(win->x + win->w - 160, offset_y + 4, 30, 30, 6, COLOR_DARK_PANEL);
    draw_string(win->x + win->w - 150, offset_y + 10, "^", COLOR_DARK_TEXT);
    draw_rounded_rect_filled(win->x + win->w - 125, offset_y + 4, 30, 30, 6, COLOR_DARK_PANEL);
    draw_string(win->x + win->w - 115, offset_y + 10, "v", COLOR_DARK_TEXT);
    
    // Draw file list
    int content_start_y = offset_y + 40;
    
    // Clip content to window area (excluding borders and top bar)
    graphics_set_clipping(win->x + 4, content_start_y, win->w - 8, win->h - 64 - 4);
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        // Draw item background (dark mode with rounded corners)
        uint32_t bg_color = (i == state->selected_item) ? 0xFF4A90E2 : COLOR_DARK_PANEL;
        uint32_t text_color = (i == state->selected_item) ? COLOR_WHITE : COLOR_DARK_TEXT;
        draw_rounded_rect_filled(item_x, item_y, EXPLORER_ITEM_WIDTH, EXPLORER_ITEM_HEIGHT, 6, bg_color);
        
        // Draw icon (larger area)
        explorer_draw_file_icon(item_x + 5, item_y + 5, state->items[i].is_directory, state->items[i].color, state->items[i].name);
        
        // Draw name using intelligent wrapping
        const char *display_name = state->items[i].name;
        if (explorer_strcmp(state->items[i].name, "RecycleBin") == 0) {
            display_name = "Recycle Bin";
        }
        explorer_draw_icon_label(item_x, item_y, display_name, text_color);
    }
    
    // Restore dirty-rect clipping instead of clearing it entirely.
    // This ensures the context menu respects the dirty region.
    if (dirty.active) {
        graphics_set_clipping(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        graphics_clear_clipping();
    }
    
    // Draw Drive Menu if visible (dark mode)
    if (state->drive_menu_visible) {
        int menu_x = win->x + 4;
        int menu_y = offset_y + 34;
        int menu_w = 80;
        int count = disk_get_count();
        int menu_h = count * 25;
        
        draw_rounded_rect_filled(menu_x, menu_y, menu_w, menu_h, 6, COLOR_DARK_PANEL);
        
        for (int i = 0; i < count; i++) {
            Disk *d = disk_get_by_index(i);
            if (d) {
                char buf[16];
                buf[0] = d->letter;
                buf[1] = ':';
                buf[2] = ' ';
                // Copy name truncated
                int n = 0; while(d->name[n] && n < 10) { buf[3+n] = d->name[n]; n++; }
                buf[3+n] = 0;
                
                // Highlight current (dark blue)
                if (d->letter == current_drv) {
                    draw_rounded_rect_filled(menu_x + 2, menu_y + i*25 + 2, menu_w - 4, 21, 4, 0xFF4A90E2);
                    draw_string(menu_x + 5, menu_y + i*25 + 6, buf, COLOR_WHITE);
                } else {
                    draw_string(menu_x + 5, menu_y + i*25 + 6, buf, COLOR_BLACK);
                }
            }
        }
    }
    
    // Draw dropdown menu if visible
    if (state->dropdown_menu_visible) {
        int menu_x = dropdown_btn_x;
        int menu_y = offset_y + 34;
        
        // Draw menu background
        draw_rounded_rect_filled(menu_x, menu_y, DROPDOWN_MENU_WIDTH, dropdown_menu_item_height * DROPDOWN_MENU_ITEMS, 6, COLOR_DARK_PANEL);
        
        // Draw menu items
        draw_string(menu_x + 8, menu_y + 5, "New File", COLOR_WHITE);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height + 5, "New Folder", COLOR_WHITE);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height * 2 + 5, "Delete", COLOR_RED);
    }

    // Draw dialogs
    if (state->dialog_state == DIALOG_CREATE_FILE) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background (modern dark, rounded)
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "Create New File", COLOR_WHITE);
        
        // Input field (rounded dark)
        draw_rounded_rect_filled(dlg_x + 10, dlg_y + 35, 280, 20, 4, COLOR_DARK_BG);
        draw_string(dlg_x + 15, dlg_y + 40, state->dialog_input, COLOR_WHITE);
        draw_rect(dlg_x + 15 + state->dialog_input_cursor * 8, dlg_y + 39, 2, 12, COLOR_WHITE);
        
        // Buttons (rounded)
        draw_rounded_rect_filled(dlg_x + 50, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 70, dlg_y + 72, "Create", COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 170, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 185, dlg_y + 72, "Cancel", COLOR_WHITE);
    } else if (state->dialog_state == DIALOG_CREATE_FOLDER) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background (modern dark, rounded)
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "Create New Folder", COLOR_WHITE);
        
        // Input field (rounded dark)
        draw_rounded_rect_filled(dlg_x + 10, dlg_y + 35, 280, 20, 4, COLOR_DARK_BG);
        draw_string(dlg_x + 15, dlg_y + 40, state->dialog_input, COLOR_WHITE);
        draw_rect(dlg_x + 15 + state->dialog_input_cursor * 8, dlg_y + 39, 2, 12, COLOR_WHITE);
        
        // Buttons (rounded)
        draw_rounded_rect_filled(dlg_x + 50, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 70, dlg_y + 72, "Create", COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 170, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 185, dlg_y + 72, "Cancel", COLOR_WHITE);
    } else if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        const char *title = state->dialog_target_is_dir ? "Delete Folder?" : "Delete File?";
        draw_string(dlg_x + 10, dlg_y + 10, title, COLOR_BLACK);
        
        // Message
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            draw_string(dlg_x + 10, dlg_y + 35, "This action cannot be undone.", COLOR_BLACK);
            draw_string(dlg_x + 10, dlg_y + 48, "Delete forever?", COLOR_BLACK);
        } else {
            draw_string(dlg_x + 10, dlg_y + 35, "This file will be moved to", COLOR_BLACK);
            draw_string(dlg_x + 10, dlg_y + 45, "the recycle bin.", COLOR_BLACK);
        }
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 65, 80, 25, "Delete", false);
        draw_button(dlg_x + 170, dlg_y + 65, 80, 25, "Cancel", false);
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Replace", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Replace", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Overwrite existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Overwrite", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (state->dialog_state == DIALOG_ERROR) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        draw_string(dlg_x + 10, dlg_y + 10, "Error", COLOR_RED);
        draw_string(dlg_x + 10, dlg_y + 40, state->dialog_input, COLOR_BLACK);
        
        // OK Button
        draw_button(dlg_x + 110, dlg_y + 70, 80, 25, "OK", false);
    } else if (state->dialog_state == DIALOG_RENAME) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        // Rename dialog (modern)
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        draw_string(dlg_x + 10, dlg_y + 10, "Rename", COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 10, dlg_y + 35, 280, 20, 4, COLOR_DARK_BG);
        draw_string(dlg_x + 15, dlg_y + 40, state->dialog_input, COLOR_WHITE);
        draw_rect(dlg_x + 15 + state->dialog_input_cursor * 8, dlg_y + 39, 2, 12, COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 50, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 68, dlg_y + 72, "Rename", COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 170, dlg_y + 65, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 185, dlg_y + 72, "Cancel", COLOR_WHITE);
    }
    
    // Draw context menu if visible
    if (state->file_context_menu_visible) {
        // Convert window-relative coordinates to screen coordinates for drawing
        int menu_screen_x = win->x + state->file_context_menu_x;
        int menu_screen_y = win->y + state->file_context_menu_y;
        
        ExplorerContextItem menu_items[20];
        int count = explorer_build_context_menu(win, menu_items);
        
        int menu_height = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) menu_height += 5; // Separator
            else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
        }
        
        // Draw menu background (modern dark, rounded)
        draw_rounded_rect_filled(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, 8, COLOR_DARK_PANEL);
        
        int y_offset = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) {
                // Separator (subtle)
                draw_rect(menu_screen_x + 8, menu_screen_y + y_offset + 3, FILE_CONTEXT_MENU_WIDTH - 16, 1, COLOR_DARK_BORDER);
                y_offset += 5;
            } else {
                draw_string(menu_screen_x + 10, menu_screen_y + y_offset + 6, menu_items[i].label, menu_items[i].color);
                y_offset += CONTEXT_MENU_ITEM_HEIGHT;
            }
        }
    }
}

// === Mouse Handler ===

static void explorer_handle_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    // Handle file context menu clicks first
    if (state->file_context_menu_visible) {
        explorer_handle_file_context_menu_click(win, x, y);
        return;
    }
    
    // Handle dialog clicks
    if (state->dialog_state == DIALOG_CREATE_FILE || state->dialog_state == DIALOG_CREATE_FOLDER) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        // Create button
        if (x >= dlg_x + 50 && x < dlg_x + 130 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            if (state->dialog_state == DIALOG_CREATE_FILE) {
                dialog_confirm_create_file(win);
            } else {
                dialog_confirm_create_folder(win);
            }
            return;
        }
        
        // Cancel button
        if (x >= dlg_x + 170 && x < dlg_x + 250 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_close(win);
            return;
        }
        
        // Input field click
        if (x >= dlg_x + 10 && x < dlg_x + 290 &&
            y >= dlg_y + 35 && y < dlg_y + 55) {
            state->dialog_input_cursor = (x - dlg_x - 15) / 8;
            if (state->dialog_input_cursor > (int)explorer_strlen(state->dialog_input)) {
                state->dialog_input_cursor = explorer_strlen(state->dialog_input);
            }
            return;
        }
    } else if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        // Delete button
        if (x >= dlg_x + 50 && x < dlg_x + 130 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_confirm_delete(win);
            return;
        }
        
        // Cancel button
        if (x >= dlg_x + 170 && x < dlg_x + 250 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_close(win);
            return;
        }
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_confirm_replace(win);
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close(win);
            return;
        }
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_confirm_replace_move(win);
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close(win);
            return;
        }
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_force_create_file(win);
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close(win);
            return;
        }
    } else if (state->dialog_state == DIALOG_ERROR) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 110 && x < dlg_x + 190 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close(win);
            return;
        }
    } else if (state->dialog_state == DIALOG_RENAME) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 65 && y < dlg_y + 90) {
            char new_path[256];
            explorer_strcpy(new_path, state->current_path);
            if (new_path[explorer_strlen(new_path)-1] != '/') explorer_strcat(new_path, "/");
            explorer_strcat(new_path, state->dialog_input);
            
            if (fat32_rename(state->dialog_target_path, new_path)) explorer_refresh_all();
            dialog_close(win);
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_close(win);
            return;
        }
    }
    
    // Handle Drive Menu Selection
    if (state->drive_menu_visible) {
        int menu_x = 4; // Window relative
        int menu_y = 58; // 24+34
        int menu_w = 80;
        int count = disk_get_count();
        int menu_h = count * 25;
        
        if (x >= menu_x && x < menu_x + menu_w && y >= menu_y && y < menu_y + menu_h) {
            int idx = (y - menu_y) / 25;
            Disk *d = disk_get_by_index(idx);
            if (d) {
                // Do not change global drive, just navigate explorer to it
                char path[4];
                path[0] = d->letter;
                path[1] = ':';
                path[2] = '/';
                path[3] = 0;
                explorer_load_directory(win, path);
            }
            state->drive_menu_visible = false;
            return;
        }
        
        // Click outside closes menu
        state->drive_menu_visible = false;
        return;
    }
    
    // Handle dropdown menu clicks
    if (state->dropdown_menu_visible) {
        int dropdown_btn_x = win->w - 90;  // Window-relative
        int menu_y = 58;  // Window-relative (offset_y + 34, where offset_y = 24)
        
        // New File
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y && y < menu_y + dropdown_menu_item_height) {
            dropdown_menu_toggle(win);
            dialog_open_create_file(win, state->current_path);
            return;
        }
        
        // New Folder
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height && 
            y < menu_y + dropdown_menu_item_height * 2) {
            dropdown_menu_toggle(win);
            dialog_open_create_folder(win, state->current_path);
            return;
        }
        
        // Delete
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height * 2 && 
            y < menu_y + dropdown_menu_item_height * 3) {
            dropdown_menu_toggle(win);
            if (state->selected_item >= 0) {
                dialog_open_delete_confirm(win, state->selected_item);
            }
            return;
        }
        
        // Click outside menu closes it
        dropdown_menu_toggle(win);
        return;
    }
    
    // x, y are already relative to window (0,0 is top-left of window content area)
    
    // Check Drive Button
    int button_y = 28;
    if (x >= 4 && x < 64 && y >= button_y && y < button_y + 30) {
        state->drive_menu_visible = !state->drive_menu_visible;
        state->dropdown_menu_visible = false; // Close other menu
        return;
    }
    
    // Check dropdown menu button
    if (x >= win->w - 90 && x < win->w - 55 &&
        y >= button_y && y < button_y + 30) {
        // Dropdown menu button clicked
        dropdown_menu_toggle(win);
        state->drive_menu_visible = false; // Close other menu
        return;
    }
    
    // Check back button (right-aligned)
    if (x >= win->w - 40 && x < win->w - 10 &&
        y >= button_y && y < button_y + 30) {
        // Back button clicked
        explorer_navigate_to(win, "..");
        return;
    }
    
    // Check scroll buttons
    // Up: w-160
    if (x >= win->w - 160 && x < win->w - 130 &&
        y >= button_y && y < button_y + 30) {
        if (state->explorer_scroll_row > 0) state->explorer_scroll_row--;
        return;
    }
    
    // Down: w-125
    if (x >= win->w - 125 && x < win->w - 95 &&
        y >= button_y && y < button_y + 30) {
        int total_rows = (state->item_count + EXPLORER_COLS - 1) / EXPLORER_COLS;
        if (total_rows == 0) total_rows = 1;
        if (state->explorer_scroll_row < total_rows - (EXPLORER_ROWS - 1)) state->explorer_scroll_row++;
        return;
    }
    
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling logic for hit test
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            // Check for double-click
            if (state->last_clicked_item == i) {
                // Double-click detected
                explorer_open_item(win, i);
                state->last_clicked_item = -1;
            } else {
                // Single-click - select
                state->selected_item = i;
                state->last_clicked_item = i;
                state->last_click_time = 0;  // Reset for next click
            }
            return;
        }
    }
}

// === Key Handler ===

static void explorer_handle_key(Window *win, char c) {
    ExplorerState *state = (ExplorerState*)win->data;
    
    // Handle dialog input
    if (state->dialog_state == DIALOG_CREATE_FILE || state->dialog_state == DIALOG_CREATE_FOLDER || state->dialog_state == DIALOG_RENAME) {
        if (c == 27) {  // ESC - close dialog
            dialog_close(win);
            return;
        } else if (c == '\n') {  // ENTER - confirm
            if (state->dialog_state == DIALOG_CREATE_FILE) {
                dialog_confirm_create_file(win);
            } else if (state->dialog_state == DIALOG_CREATE_FOLDER) {
                dialog_confirm_create_folder(win);
            } else if (state->dialog_state == DIALOG_RENAME) {
                char new_path[256];
                explorer_strcpy(new_path, state->current_path);
                if (new_path[explorer_strlen(new_path)-1] != '/') explorer_strcat(new_path, "/");
                explorer_strcat(new_path, state->dialog_input);
                if (fat32_rename(state->dialog_target_path, new_path)) explorer_refresh(win);
                dialog_close(win);
            }
        } else if (c == 19) {  // LEFT arrow
            if (state->dialog_input_cursor > 0) state->dialog_input_cursor--;
        } else if (c == 20) {  // RIGHT arrow
            if (state->dialog_input_cursor < (int)explorer_strlen(state->dialog_input)) state->dialog_input_cursor++;
        } else if (c == 8 || c == 127) {  // BACKSPACE
            if (state->dialog_input_cursor > 0) {
                state->dialog_input_cursor--;
                // Shift characters
                for (int i = state->dialog_input_cursor; i < (int)explorer_strlen(state->dialog_input); i++) {
                    state->dialog_input[i] = state->dialog_input[i + 1];
                }
            }
        } else if (c >= 32 && c < 127) {  // Printable character
            int len = explorer_strlen(state->dialog_input);
            if (len < DIALOG_INPUT_MAX - 1) {
                // Shift characters to make room
                for (int i = len; i >= state->dialog_input_cursor; i--) {
                    state->dialog_input[i + 1] = state->dialog_input[i];
                }
                state->dialog_input[state->dialog_input_cursor] = c;
                state->dialog_input_cursor++;
            }
        }
        wm_mark_dirty(win->x, win->y, win->w, win->h);
        return;
    }
    
    if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        if (c == 27) {  // ESC
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_confirm_replace(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_confirm_replace_move(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_force_create_file(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_ERROR) {
        if (c == 27 || c == '\n') {
            dialog_close(win);
        }
        return;
    }
    

    
    // Close dropdown menu if open with ESC
    if (state->dropdown_menu_visible && c == 27) {
        dropdown_menu_toggle(win);
        return;
    }
    
    if (c == 17) {  // UP
        if (state->selected_item > 0) {
            state->selected_item -= EXPLORER_COLS;
            if (state->selected_item < 0) state->selected_item = 0;
            // Scroll if needed
            int row = state->selected_item / EXPLORER_COLS;
            if (row < state->explorer_scroll_row) state->explorer_scroll_row = row;
        }
    } else if (c == 18) {  // DOWN
        if (state->selected_item < state->item_count - 1) {
            state->selected_item += EXPLORER_COLS;
            if (state->selected_item >= state->item_count) state->selected_item = state->item_count - 1;
            // Scroll if needed
            int row = state->selected_item / EXPLORER_COLS;
            if (row >= state->explorer_scroll_row + (EXPLORER_ROWS - 1)) state->explorer_scroll_row = row - (EXPLORER_ROWS - 1) + 1;
        }
    } else if (c == 19) {  // LEFT
        if (state->selected_item > 0) {
            state->selected_item--;
        }
    } else if (c == 20) {  // RIGHT
        if (state->selected_item < state->item_count - 1) {
            state->selected_item++;
        }
    } else if (c == '\n') {  // ENTER
        if (state->selected_item >= 0 && state->selected_item < state->item_count) {
            if (state->items[state->selected_item].is_directory) {
                explorer_open_item(win, state->selected_item);
            }
        }
    } else if (c == 'd' || c == 'D') {  // Delete key
        if (state->selected_item >= 0) {
            dialog_open_delete_confirm(win, state->selected_item);
        }
    } else if (c == 'n' || c == 'N') {  // New file
        dialog_open_create_file(win, state->current_path);
    } else if (c == 'f' || c == 'F') {  // New folder
        dialog_open_create_folder(win, state->current_path);
    }
}

// === Right-Click Handler ===

static void explorer_handle_right_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling logic for hit test
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            // Right-click on a file or folder item
            // Show context menu
            state->file_context_menu_visible = true;
            state->file_context_menu_item = i;
            state->file_context_menu_x = x;
            state->file_context_menu_y = y;
            return;
        }
    }
    
    // Clicked on empty space
    state->file_context_menu_visible = true;
    state->file_context_menu_item = -1; // Background
    state->file_context_menu_x = x;
    state->file_context_menu_y = y;
}

static void explorer_handle_file_context_menu_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    
    if (!state->file_context_menu_visible) {
        return;
    }
    
    // Adjust coordinates to be relative to context menu
    int relative_x = x - state->file_context_menu_x;
    int relative_y = y - state->file_context_menu_y;
    
    ExplorerContextItem menu_items[20];
    int count = explorer_build_context_menu(win, menu_items);
    int menu_height = 0;
    for (int i = 0; i < count; i++) {
        if (menu_items[i].action_id == 0) menu_height += 5; else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
    }
    
    if (relative_x < 0 || relative_x > FILE_CONTEXT_MENU_WIDTH ||
        relative_y < 0 || relative_y > menu_height) {
        // Clicked outside menu - close it
        state->file_context_menu_visible = false;
        state->file_context_menu_item = -1;
        return;
    }
    
    // Find clicked item
    int current_y = 0;
    int clicked_action = 0;
    
    for (int i = 0; i < count; i++) {
        int h = (menu_items[i].action_id == 0) ? 5 : CONTEXT_MENU_ITEM_HEIGHT;
        if (relative_y >= current_y && relative_y < current_y + h) {
            if (menu_items[i].enabled && menu_items[i].action_id != 0) {
                clicked_action = menu_items[i].action_id;
            }
            break;
        }
        current_y += h;
    }
    
    if (clicked_action == 0) return;
    
    // Execute Action
    char full_path[256];
    if (state->file_context_menu_item >= 0) {
        explorer_strcpy(full_path, state->current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') explorer_strcat(full_path, "/");
        explorer_strcat(full_path, state->items[state->file_context_menu_item].name);
    }
    
    if (clicked_action == 100) { // Open
        explorer_open_item(win, state->file_context_menu_item);
    } else if (clicked_action == 109) { // Open MD
        explorer_open_item(win, state->file_context_menu_item);
    } else if (clicked_action == 101) { // New File
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            dialog_open_create_file(win, full_path);
        } else {
            dialog_open_create_file(win, state->current_path);
        }
    } else if (clicked_action == 102) { // New Folder
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            dialog_open_create_folder(win, full_path);
        } else {
            dialog_open_create_folder(win, state->current_path);
        }
    } else if (clicked_action == 103) { // Paste
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            explorer_clipboard_paste(win, full_path);
        } else {
            explorer_clipboard_paste(win, state->current_path);
        }
    } else if (clicked_action == 104) { // Cut
        explorer_clipboard_cut(full_path);
    } else if (clicked_action == 105) { // Copy
        explorer_clipboard_copy(full_path);
    } else if (clicked_action == 106) { // Delete
        dialog_open_delete_confirm(win, state->file_context_menu_item);
    } else if (clicked_action == 111) { // Rename
        state->dialog_state = DIALOG_RENAME;
        explorer_strcpy(state->dialog_input, state->items[state->file_context_menu_item].name);
        state->dialog_input_cursor = explorer_strlen(state->dialog_input);
        explorer_strcpy(state->dialog_target_path, full_path);
    } else if (clicked_action == 110) { // Open with Text Editor
        win_editor.visible = true; win_editor.focused = true;
        int max_z = 0;
        for (int i = 0; i < explorer_win_count; i++) if (explorer_wins[i]->z_index > max_z) max_z = explorer_wins[i]->z_index;
        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
        if (win_editor.z_index > max_z) max_z = win_editor.z_index;
        if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
        if (win_control_panel.z_index > max_z) max_z = win_control_panel.z_index;
        if (win_about.z_index > max_z) max_z = win_about.z_index;
        if (win_minesweeper.z_index > max_z) max_z = win_minesweeper.z_index;
        win_editor.z_index = max_z + 1;
        editor_open_file(full_path);
    } else if (clicked_action == ACTION_RESTORE) {
        explorer_restore_file(win, state->file_context_menu_item);
    } else if (clicked_action == ACTION_CREATE_SHORTCUT) {
        explorer_create_shortcut(win, full_path);
    } else if (clicked_action == 112) { // Open in new window
        explorer_create_window(full_path);
    } else if (clicked_action >= 200 && clicked_action <= 204) { // Colors
        uint32_t new_color = state->items[state->file_context_menu_item].color;
        if (clicked_action == 200) new_color = COLOR_APPLE_BLUE;
        else if (clicked_action == 201) new_color = COLOR_RED;
        else if (clicked_action == 202) new_color = COLOR_APPLE_YELLOW;
        else if (clicked_action == 203) new_color = COLOR_APPLE_GREEN;
        else if (clicked_action == 204) new_color = COLOR_BLACK;
        state->items[state->file_context_menu_item].color = new_color;
        explorer_set_folder_color(full_path, new_color);
    }
    
    state->file_context_menu_visible = false;
    state->file_context_menu_item = -1;
}

// === Drag and Drop Support ===

bool explorer_get_file_at(int screen_x, int screen_y, char *out_path, bool *is_dir) {
    for (int w = 0; w < explorer_win_count; w++) {
        Window *win = explorer_wins[w];
        if (!win->visible) continue;
        
        ExplorerState *state = (ExplorerState*)win->data;
        int rel_x = screen_x - win->x;
        int rel_y = screen_y - win->y;
        
        if (rel_x < 4 || rel_x > win->w - 4 || rel_y < 64 || rel_y > win->h - 4) continue;
        
        int content_start_y = 64;
        int offset_x = 4;
        
        for (int i = 0; i < state->item_count; i++) {
            int row = i / EXPLORER_COLS;
            int col = i % EXPLORER_COLS;
            if (row < state->explorer_scroll_row) continue;
            if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
            
            int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
            int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
            
            if (rel_x >= item_x && rel_x < item_x + EXPLORER_ITEM_WIDTH &&
                rel_y >= item_y && rel_y < item_y + EXPLORER_ITEM_HEIGHT) {
                explorer_strcpy(out_path, state->current_path);
                if (out_path[explorer_strlen(out_path) - 1] != '/') explorer_strcat(out_path, "/");
                explorer_strcat(out_path, state->items[i].name);
                *is_dir = state->items[i].is_directory;
                return true;
            }
        }
    }
    return false;
}

void explorer_clear_click_state(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->last_clicked_item = -1;
}

void explorer_refresh(Window *win) {
    if (!win) return;
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_load_directory(win, state->current_path);
    wm_mark_dirty(win->x, win->y, win->w, win->h);
}

void explorer_refresh_all(void) {
    for (int i = 0; i < explorer_win_count; i++) {
        explorer_refresh(explorer_wins[i]);
    }
    wm_refresh_desktop();
}

static void explorer_perform_move_internal(Window *win, const char *source_path, const char *dest_dir) {
    (void)win;
    // 1. Extract filename
    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    // 2. Build dest path
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    // Check if source and dest are the same to prevent deletion
    if (explorer_strcmp(source_path, dest_path) == 0) {
        return;
    }
    
    if (explorer_str_starts_with(dest_path, "/RecycleBin") && !explorer_str_starts_with(source_path, "/RecycleBin")) {
        char origin_path[FAT32_MAX_PATH];
        explorer_strcpy(origin_path, dest_path);
        explorer_strcat(origin_path, ".origin");
        FAT32_FileHandle *fh = fat32_open(origin_path, "w");
        if (fh) {
            fat32_write(fh, source_path, explorer_strlen(source_path));
            fat32_close(fh);
        }
    }

    if (!explorer_str_starts_with(dest_path, "/RecycleBin") && explorer_str_starts_with(source_path, "/RecycleBin")) {
        char origin_path[FAT32_MAX_PATH];
        explorer_strcpy(origin_path, source_path);
        explorer_strcat(origin_path, ".origin");
        fat32_delete(origin_path);
    }

    if (explorer_copy_recursive(source_path, dest_path)) {
        // 4. Delete source (Move operation)
        explorer_delete_permanently(source_path);
    }
        
    // Refresh
    explorer_refresh_all();
}

void explorer_import_file_to(Window *win, const char *source_path, const char *dest_dir) {
    ExplorerState *state = (ExplorerState*)win->data;

    // Prevent moving a directory into itself or its subdirectories
    if (explorer_str_starts_with(dest_dir, source_path)) {
        int src_len = explorer_strlen(source_path);
        if (dest_dir[src_len] == '\0' || dest_dir[src_len] == '/') {
            return;
        }
    }

    // Check for collision
    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') explorer_strcat(dest_path, "/");
    explorer_strcat(dest_path, filename);
    
    if (fat32_exists(dest_path) && explorer_strcmp(source_path, dest_path) != 0) {
        explorer_strcpy(state->dialog_move_src, source_path);
        explorer_strcpy(state->dialog_dest_dir, dest_dir);
        state->dialog_state = DIALOG_REPLACE_MOVE_CONFIRM;
        return;
    }
    
    explorer_perform_move_internal(win, source_path, dest_dir);
}

void explorer_import_file(Window *win, const char *source_path) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_import_file_to(win, source_path, state->current_path);
}

// === Initialization ===

Window* explorer_create_window(const char *path) {
    if (explorer_win_count >= 10) return NULL;
    
    Window *win = (Window*)kmalloc(sizeof(Window));
    ExplorerState *state = (ExplorerState*)kmalloc(sizeof(ExplorerState));
    
    win->title = "Files";
    win->x = 300 + (explorer_win_count * 30);
    win->y = 100 + (explorer_win_count * 30);
    win->w = 600;
    win->h = 400;
    win->visible = true;
    win->focused = true;
    win->z_index = 10;
    win->paint = explorer_paint;
    win->handle_key = explorer_handle_key;
    win->handle_click = explorer_handle_click;
    win->handle_right_click = explorer_handle_right_click;
    win->data = state;
    
    state->selected_item = -1;
    state->last_clicked_item = -1;
    state->explorer_scroll_row = 0;
    state->dialog_state = DIALOG_NONE;
    state->dropdown_menu_visible = false;
    state->drive_menu_visible = false;
    state->file_context_menu_visible = false;
    
    explorer_wins[explorer_win_count++] = win;
    wm_add_window(win);
    wm_bring_to_front(win);
    
    if (explorer_strcmp(path, "/") == 0) explorer_load_directory(win, "A:/");
    else explorer_load_directory(win, path);
    return win;
}

void explorer_init(void) {
    ExplorerState *state = (ExplorerState*)kmalloc(sizeof(ExplorerState));
    win_explorer.title = "Files";
    win_explorer.x = 300;
    win_explorer.y = 100;
    win_explorer.w = 600;
    win_explorer.h = 400;
    win_explorer.visible = false;
    win_explorer.focused = false;
    win_explorer.z_index = 0;
    win_explorer.paint = explorer_paint;
    win_explorer.handle_key = explorer_handle_key;
    win_explorer.handle_click = explorer_handle_click;
    win_explorer.handle_right_click = explorer_handle_right_click;
    win_explorer.data = state;
    
    state->drive_menu_visible = false;
    explorer_wins[explorer_win_count++] = &win_explorer;
    explorer_load_directory(&win_explorer, "A:/");
}
void explorer_reset(void) {
    ExplorerState *state = (ExplorerState*)win_explorer.data;
    // Reset explorer to root directory on close/reopen
    explorer_load_directory(&win_explorer, "A:/");
    win_explorer.focused = false;
    state->explorer_scroll_row = 0;
}
