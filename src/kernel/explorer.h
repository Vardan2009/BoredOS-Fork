#ifndef EXPLORER_H
#define EXPLORER_H

#include "wm.h"
#include <stddef.h>

// External windows references (for opening other apps)
extern Window win_explorer;
extern Window win_editor;
extern Window win_cmd;
extern Window win_notepad;
extern Window win_calculator;
extern Window win_markdown;

#define EXPLORER_MAX_FILES 64
#define DIALOG_INPUT_MAX 256

typedef struct {
    char name[256];
    bool is_directory;
    uint32_t size;
    uint32_t color;
} ExplorerItem;

typedef struct {
    ExplorerItem items[EXPLORER_MAX_FILES];
    int item_count;
    int selected_item;
    char current_path[256];
    int last_clicked_item;
    uint32_t last_click_time;
    int explorer_scroll_row;

    // Dialog state
    int dialog_state;
    char dialog_input[DIALOG_INPUT_MAX];
    int dialog_input_cursor;
    char dialog_target_path[256];
    bool dialog_target_is_dir;
    char dialog_dest_dir[256];
    char dialog_creation_path[256];
    char dialog_move_src[256];

    // Dropdown menu state
    bool dropdown_menu_visible;
    
    // File context menu state
    bool file_context_menu_visible;
    int file_context_menu_x;
    int file_context_menu_y;
    int file_context_menu_item;

} ExplorerState;

void explorer_init(void);
void explorer_reset(void);
void explorer_open_directory(const char *path); // Creates a NEW window

// Drag and Drop support
// This now needs to find WHICH explorer window is under the mouse
bool explorer_get_file_at(int screen_x, int screen_y, char *out_path, bool *is_dir);
void explorer_import_file(Window *win, const char *source_path); // To focused or default
void explorer_import_file_to(Window *win, const char *source_path, const char *dest_dir);
void explorer_refresh(Window *win);
void explorer_refresh_all(void);
void explorer_clear_click_state(Window *win);

// String Helpers
size_t explorer_strlen(const char *str);
void explorer_strcpy(char *dest, const char *src);
void explorer_strcat(char *dest, const char *src);

// Clipboard (System-wide)
void explorer_clipboard_copy(const char *path);
void explorer_clipboard_cut(const char *path);
void explorer_clipboard_paste(Window *win, const char *dest_dir);
bool explorer_clipboard_has_content(void);

// File Operations
bool explorer_delete_permanently(const char *path);
bool explorer_delete_recursive(const char *path);
void explorer_create_shortcut(Window *win, const char *target_path);

#endif