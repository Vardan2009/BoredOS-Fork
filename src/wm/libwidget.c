#include "libwidget.h"
#include <stddef.h>

#define COLOR_GRAY 0xFFC0C0C0
#define COLOR_LTGRAY 0xFFDFDFDF
#define COLOR_DKGRAY 0xFF808080
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_BLACK 0xFF000000

#define COLOR_SCROLLBAR_BG 0xFF2A2A2A
#define COLOR_SCROLLBAR_THUMB 0xFF505050
#define COLOR_SCROLLBAR_THUMB_HOVER 0xFF707070
#define COLOR_SCROLLBAR_THUMB_DRAG 0xFF909090

static size_t string_len(const char *str) {
    size_t l = 0;
    while(str && str[l]) l++;
    return l;
}

#define MAC_BTN_BORDER 0xFF4A4A4C
#define MAC_BTN_BG_NORMAL 0xFF353537
#define MAC_BTN_BG_HOVER 0xFF454547
#define MAC_BTN_BG_PRESSED 0xFF555557

// --- Button Implementation ---

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h, const char *text) {
    btn->x = x;
    btn->y = y;
    btn->w = w;
    btn->h = h;
    btn->text = text;
    btn->pressed = false;
    btn->hovered = false;
    btn->on_click = NULL;
}

void widget_button_draw(widget_context_t *ctx, widget_button_t *btn) {
    uint32_t bg_color = MAC_BTN_BG_NORMAL;
    if (btn->pressed) {
        bg_color = MAC_BTN_BG_PRESSED;
    } else if (btn->hovered) {
        bg_color = MAC_BTN_BG_HOVER;
    }

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->x, btn->y, btn->w, btn->h, 6, MAC_BTN_BORDER);
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->x + 1, btn->y + 1, btn->w - 2, btn->h - 2, 5, bg_color);
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, btn->x, btn->y, btn->w, btn->h, MAC_BTN_BORDER);
        ctx->draw_rect(ctx->user_data, btn->x + 1, btn->y + 1, btn->w - 2, btn->h - 2, bg_color);
    }
    
    if (btn->text && ctx->draw_string) {
        int len = string_len(btn->text);
        int tx = btn->x + (btn->w - (len * 8)) / 2;
        int ty = btn->y + (btn->h - 8) / 2;
        ctx->draw_string(ctx->user_data, tx, ty, btn->text, COLOR_WHITE);
    }
}

bool widget_button_handle_mouse(widget_button_t *btn, int mx, int my, bool mouse_down, bool mouse_clicked, void *user_data) {
    bool in_bounds = (mx >= btn->x && mx < btn->x + btn->w && my >= btn->y && my < btn->y + btn->h);
    
    btn->hovered = in_bounds;
    
    if (mouse_clicked && in_bounds) {
        btn->pressed = true;
        return true;
    }
    
    if (!mouse_down && btn->pressed) {
        btn->pressed = false;
        if (in_bounds && btn->on_click) {
            btn->on_click(user_data);
        }
        return true;
    }
    
    return in_bounds;
}

// --- Scrollbar Implementation ---

void widget_scrollbar_init(widget_scrollbar_t *sb, int x, int y, int w, int h) {
    sb->x = x;
    sb->y = y;
    sb->w = w;
    sb->h = h;
    sb->content_height = h;
    sb->scroll_y = 0;
    sb->is_dragging = false;
    sb->on_scroll = NULL;
}

void widget_scrollbar_update(widget_scrollbar_t *sb, int content_height, int scroll_y) {
    sb->content_height = content_height;
    sb->scroll_y = scroll_y;
}

void widget_scrollbar_draw(widget_context_t *ctx, widget_scrollbar_t *sb) {
    // Only draw thumb if content is larger than view
    if (sb->content_height > sb->h) {
        int thumb_h = (sb->h * sb->h) / sb->content_height;
        if (thumb_h < 20) thumb_h = 20;
        
        int max_scroll = sb->content_height - sb->h;
        if (sb->scroll_y > max_scroll) sb->scroll_y = max_scroll;
        if (sb->scroll_y < 0) sb->scroll_y = 0;
        
        int thumb_y = sb->y + (sb->scroll_y * (sb->h - thumb_h)) / max_scroll;
        
        uint32_t color = 0xFF888888; // Subtle gray thumb for mac style
        if (sb->is_dragging) color = 0xFF666666;
        
        if (ctx->draw_rounded_rect_filled) {
            // Pill shaped thumb with margin
            int margin = 2;
            int radius = (sb->w - margin*2) / 2;
            ctx->draw_rounded_rect_filled(ctx->user_data, sb->x + margin, thumb_y + margin, sb->w - margin*2, thumb_h - margin*2, radius, color);
        } else if (ctx->draw_rect) {
            ctx->draw_rect(ctx->user_data, sb->x, thumb_y, sb->w, thumb_h, color);
        }
    }
}

bool widget_scrollbar_handle_mouse(widget_scrollbar_t *sb, int mx, int my, bool mouse_down, void *user_data) {
    if (sb->content_height <= sb->h) return false;
    
    int thumb_h = (sb->h * sb->h) / sb->content_height;
    if (thumb_h < 20) thumb_h = 20;
    
    int max_scroll = sb->content_height - sb->h;
    if (sb->scroll_y > max_scroll) sb->scroll_y = max_scroll;
    if (sb->scroll_y < 0) sb->scroll_y = 0;
    
    int thumb_y = sb->y + (sb->scroll_y * (sb->h - thumb_h)) / max_scroll;
    
    bool in_thumb = (mx >= sb->x && mx < sb->x + sb->w && my >= thumb_y && my < thumb_y + thumb_h);
    bool in_track = (mx >= sb->x && mx < sb->x + sb->w && my >= sb->y && my < sb->y + sb->h);
    
    if (mouse_down && !sb->is_dragging) {
        if (in_thumb) {
            sb->is_dragging = true;
            sb->drag_start_my = my;
            sb->drag_start_scroll_y = sb->scroll_y;
            return true;
        } else if (in_track) {
            // Page scroll
            if (my < thumb_y) {
                sb->scroll_y -= sb->h;
            } else {
                sb->scroll_y += sb->h;
            }
            if (sb->scroll_y < 0) sb->scroll_y = 0;
            if (sb->scroll_y > max_scroll) sb->scroll_y = max_scroll;
            if (sb->on_scroll) sb->on_scroll(user_data, sb->scroll_y);
            return true;
        }
    } else if (!mouse_down) {
        sb->is_dragging = false;
    }
    
    if (sb->is_dragging && mouse_down) {
        int dy = my - sb->drag_start_my;
        int track_h = sb->h - thumb_h;
        if (track_h > 0) {
            float ratio = (float)max_scroll / (float)track_h;
            int new_scroll = sb->drag_start_scroll_y + (int)(dy * ratio);
            
            if (new_scroll < 0) new_scroll = 0;
            if (new_scroll > max_scroll) new_scroll = max_scroll;
            
            if (new_scroll != sb->scroll_y) {
                sb->scroll_y = new_scroll;
                if (sb->on_scroll) sb->on_scroll(user_data, sb->scroll_y);
            }
        }
        return true;
    }
    
    return in_track || sb->is_dragging;
}

// --- TextBox Implementation ---
void widget_textbox_init(widget_textbox_t *tb, int x, int y, int w, int h, char *buffer, int max_len) {
    tb->x = x; tb->y = y; tb->w = w; tb->h = h;
    tb->text = buffer;
    tb->max_len = max_len;
    tb->cursor_pos = string_len(buffer);
    tb->focused = false;
    tb->on_change = NULL;
}

void widget_textbox_draw(widget_context_t *ctx, widget_textbox_t *tb) {
    // Background and border
    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->x, tb->y, tb->w, tb->h, 4, MAC_BTN_BORDER);
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->x + 1, tb->y + 1, tb->w - 2, tb->h - 2, 3, COLOR_BLACK); // dark background
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, tb->x, tb->y, tb->w, tb->h, MAC_BTN_BORDER);
        ctx->draw_rect(ctx->user_data, tb->x + 1, tb->y + 1, tb->w - 2, tb->h - 2, COLOR_BLACK);
    }
    
    if (ctx->draw_string && tb->text) {
        int max_w = tb->w - 15;
        int scroll_x = 0;
        int text_w = 0;
        
        if (ctx->measure_string_width) {
            text_w = ctx->measure_string_width(ctx->user_data, tb->text);
        } else {
            text_w = string_len(tb->text) * 8;
        }
        
        if (text_w > max_w) scroll_x = text_w - max_w;
        
        // Very basic simple drawing, without proper clipping since context lacks it
        ctx->draw_string(ctx->user_data, tb->x + 5, tb->y + (tb->h - 8) / 2, tb->text, COLOR_WHITE);
        
        if (tb->focused && ctx->draw_rect) {
            int cx = 0;
            if (ctx->measure_string_width) {
                // measure up to cursor
                char tmp[256];
                int k = 0;
                for (k = 0; k < tb->cursor_pos && tb->text[k]; k++) {
                    tmp[k] = tb->text[k];
                }
                tmp[k] = 0;
                cx = ctx->measure_string_width(ctx->user_data, tmp);
            } else {
                cx = tb->cursor_pos * 8;
            }
            
            if (cx > max_w) cx = max_w; // clamped to visible end
            
            ctx->draw_rect(ctx->user_data, tb->x + 5 + cx, tb->y + (tb->h - 12) / 2, 2, 12, COLOR_WHITE);
        }
    }
}

bool widget_textbox_handle_mouse(widget_textbox_t *tb, int mx, int my, bool mouse_clicked, void *user_data) {
    bool in_bounds = (mx >= tb->x && mx < tb->x + tb->w && my >= tb->y && my < tb->y + tb->h);
    if (mouse_clicked) {
        tb->focused = in_bounds;
    }
    return in_bounds;
}

bool widget_textbox_handle_key(widget_textbox_t *tb, char c, void *user_data) {
    if (!tb->focused || !tb->text) return false;
    
    int len = string_len(tb->text);
    if (c == '\b') { // backspace
        if (len > 0) {
            tb->text[len - 1] = '\0';
            tb->cursor_pos = len - 1;
            if (tb->on_change) tb->on_change(user_data);
        }
    } else if (c >= 32 && c < 127) {
        if (len < tb->max_len - 1) {
            tb->text[len] = c;
            tb->text[len + 1] = '\0';
            tb->cursor_pos = len + 1;
            if (tb->on_change) tb->on_change(user_data);
        }
    }
    return true;
}

// --- Dropdown Implementation ---
void widget_dropdown_init(widget_dropdown_t *dd, int x, int y, int w, int h, const char **items, int count) {
    dd->x = x; dd->y = y; dd->w = w; dd->h = h;
    dd->items = items;
    dd->item_count = count;
    dd->selected_idx = 0;
    dd->is_open = false;
    dd->on_select = NULL;
}

void widget_dropdown_draw(widget_context_t *ctx, widget_dropdown_t *dd) {
    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, dd->x, dd->y, dd->w, dd->h, 4, MAC_BTN_BORDER);
        ctx->draw_rounded_rect_filled(ctx->user_data, dd->x + 1, dd->y + 1, dd->w - 2, dd->h - 2, 3, MAC_BTN_BG_NORMAL);
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, dd->x, dd->y, dd->w, dd->h, MAC_BTN_BORDER);
        ctx->draw_rect(ctx->user_data, dd->x + 1, dd->y + 1, dd->w - 2, dd->h - 2, MAC_BTN_BG_NORMAL);
    }
    
    if (ctx->draw_string && dd->items && dd->item_count > 0 && dd->selected_idx >= 0 && dd->selected_idx < dd->item_count) {
        ctx->draw_string(ctx->user_data, dd->x + 5, dd->y + (dd->h - 8) / 2, dd->items[dd->selected_idx], COLOR_WHITE);
        ctx->draw_string(ctx->user_data, dd->x + dd->w - 12, dd->y + (dd->h - 8) / 2, "v", COLOR_WHITE);
    }
    
    if (dd->is_open && ctx->draw_rect && dd->items) {
        int menu_h = dd->item_count * dd->h;
        ctx->draw_rect(ctx->user_data, dd->x, dd->y + dd->h, dd->w, menu_h, MAC_BTN_BORDER);
        ctx->draw_rect(ctx->user_data, dd->x + 1, dd->y + dd->h + 1, dd->w - 2, menu_h - 2, MAC_BTN_BG_NORMAL);
        for (int i = 0; i < dd->item_count; i++) {
            if (ctx->draw_string) {
                ctx->draw_string(ctx->user_data, dd->x + 5, dd->y + dd->h + i * dd->h + (dd->h - 8)/2, dd->items[i], COLOR_WHITE);
            }
        }
    }
}

bool widget_dropdown_handle_mouse(widget_dropdown_t *dd, int mx, int my, bool mouse_clicked, void *user_data) {
    if (!mouse_clicked) return false;

    if (dd->is_open) {
        int menu_h = dd->item_count * dd->h;
        if (mx >= dd->x && mx < dd->x + dd->w && my >= dd->y + dd->h && my < dd->y + dd->h + menu_h) {
            int clicked_idx = (my - (dd->y + dd->h)) / dd->h;
            if (clicked_idx >= 0 && clicked_idx < dd->item_count) {
                dd->selected_idx = clicked_idx;
                dd->is_open = false;
                if (dd->on_select) dd->on_select(user_data, clicked_idx);
                return true;
            }
        }
        dd->is_open = false; 
    }

    if (mx >= dd->x && mx < dd->x + dd->w && my >= dd->y && my < dd->y + dd->h) {
        dd->is_open = !dd->is_open;
        return true;
    }
    
    return false;
}

// --- Checkbox / Radio Implementation ---
void widget_checkbox_init(widget_checkbox_t *cb, int x, int y, int w, int h, const char *text, bool is_radio) {
    cb->x = x; cb->y = y; cb->w = w; cb->h = h;
    cb->text = text;
    cb->checked = false;
    cb->is_radio = is_radio;
    cb->on_toggle = NULL;
}

void widget_checkbox_draw(widget_context_t *ctx, widget_checkbox_t *cb) {
    int box_size = 14;
    int box_y = cb->y + (cb->h - box_size) / 2;
    
    if (ctx->draw_rounded_rect_filled) {
        int radius = cb->is_radio ? box_size / 2 : 3;
        ctx->draw_rounded_rect_filled(ctx->user_data, cb->x, box_y, box_size, box_size, radius, MAC_BTN_BORDER);
        ctx->draw_rounded_rect_filled(ctx->user_data, cb->x + 1, box_y + 1, box_size - 2, box_size - 2, radius - 1, MAC_BTN_BG_NORMAL);
        
        if (cb->checked) {
            int inner = box_size - 6;
            int inner_rad = cb->is_radio ? inner / 2 : 2;
            ctx->draw_rounded_rect_filled(ctx->user_data, cb->x + 3, box_y + 3, inner, inner, inner_rad, COLOR_WHITE);
        }
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, cb->x, box_y, box_size, box_size, MAC_BTN_BORDER);
        ctx->draw_rect(ctx->user_data, cb->x + 1, box_y + 1, box_size - 2, box_size - 2, MAC_BTN_BG_NORMAL);
        if (cb->checked) {
            int inner = box_size - 6;
            ctx->draw_rect(ctx->user_data, cb->x + 3, box_y + 3, inner, inner, COLOR_WHITE);
        }
    }
    
    if (ctx->draw_string && cb->text) {
        ctx->draw_string(ctx->user_data, cb->x + box_size + 8, cb->y + (cb->h - 8) / 2, cb->text, COLOR_WHITE);
    }
}

bool widget_checkbox_handle_mouse(widget_checkbox_t *cb, int mx, int my, bool mouse_clicked, void *user_data) {
    if (!mouse_clicked) return false;
    
    if (mx >= cb->x && mx < cb->x + cb->w && my >= cb->y && my < cb->y + cb->h) {
        cb->checked = !cb->checked;
        if (cb->on_toggle) cb->on_toggle(user_data, cb->checked);
        return true;
    }
    return false;
}
