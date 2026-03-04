// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
#include "libc/syscall.h"
#include "libc/libui.h"
#include "nanojpeg.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define WIN_W 1280
#define WIN_H 960
#define URL_BAR_H 30
#define SCROLL_BAR_W 16
#define RESP_BUF_SIZE (32 * 1024 * 1024)

#define COLOR_URL_BAR    0xFF303030
#define COLOR_URL_TEXT   0xFFF0F0F0
#define COLOR_BG         0xFFFFFFFF
#define COLOR_TEXT       0xFF000000
#define COLOR_LINK       0xFF0000EE
#define COLOR_SCROLL_BG  0xFFEEEEEE
#define COLOR_SCROLL_BTN 0xFFCCCCCC

#define BTN_W 30
#define BTN_H 22
#define BTN_PAD 4
#define HOME_BTN_X (WIN_W - SCROLL_BAR_W - BTN_W - BTN_PAD)
#define BACK_BTN_X (HOME_BTN_X - BTN_W - BTN_PAD)

#define HISTORY_MAX 32
static char history_stack[HISTORY_MAX][512];
static int history_count = 0;

static char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++; n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

static char* str_istrstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            char ch = *h; char cn = *n;
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            if (cn >= 'A' && cn <= 'Z') cn += 32;
            if (ch != cn) break;
            h++; n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

static long strtol(const char* nptr, char** endptr, int base) {
    long res = 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') nptr++;
    bool neg = false;
    if (*nptr == '-') { neg = true; nptr++; }
    else if (*nptr == '+') nptr++;
    
    while (*nptr) {
        int v = -1;
        if (*nptr >= '0' && *nptr <= '9') v = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'z') v = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'Z') v = *nptr - 'A' + 10;
        if (v < 0 || v >= base) break;
        res = res * base + v;
        nptr++;
    }
    if (endptr) *endptr = (char*)nptr;
    return neg ? -res : res;
}

#define TAG_NONE 0
#define TAG_IMG 1
#define TAG_INPUT 2
#define TAG_BUTTON 3
#define TAG_HR 4

typedef struct {
    char content[1024];
    int x, y, w, h;
    int tag; // Changed from HTMLTag enum to int
    char link_url[256];
    char attr_value[256];
    uint32_t color;
    bool centered;
    bool bold;
    uint32_t *img_pixels;
    int img_w, img_h;
    char form_action[256];
    char input_name[64];
    int form_id;
    int input_cursor;
    int input_scroll;
    float scale;
} RenderElement;

#define MAX_ELEMENTS 65536
static RenderElement elements[MAX_ELEMENTS];
static int element_count = 0;

static char url_input_buffer[512] = "http://find.boreddev.nl";
static int url_cursor = 22;
static char current_host[256] = "find.boreddev.nl";
static int current_port = 80;
static int next_form_id = 1;

static ui_window_t win_browser;
static int scroll_y = 0;
static int total_content_height = 0;
static int focused_element = -1; 

static void parse_html(const char *html);
static void parse_html_incremental(const char *html, int safe_len);
static void browser_paint(void);
static int inc_parse_offset = 0;

static void browser_clear(void) {
    for (int i = 0; i < element_count; i++) {
        if (elements[i].img_pixels) free(elements[i].img_pixels);
    }
    element_count = 0;
    total_content_height = 0;
}

static bool str_istarts_with(const char *str, const char *prefix) {
    while (*prefix) {
        char s = *str; char p = *prefix;
        if (s >= 'A' && s <= 'Z') s += 32;
        if (p >= 'A' && p <= 'Z') p += 32;
        if (s != p) return false;
        str++; prefix++;
    }
    return true;
}

static int parse_ip(const char* str, net_ipv4_address_t* ip) {
    int val = 0;
    int part = 0;
    const char* p = str;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
        } else if (*p == '.') {
            if (part > 3) return -1;
            ip->bytes[part++] = (uint8_t)val;
            val = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (part != 3) return -1;
    ip->bytes[3] = (uint8_t)val;
    return 0;
}

static int fetch_content(const char *url, char *dest_buf, int max_len, bool progressive) {
    const char* host_start = url;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (url[4] == 's' && url[5] == ':') host_start = url + 8;
        else if (url[4] == ':') host_start = url + 7;
    }

    char hostname[256];
    int port = 80;
    int i = 0;
    while (host_start[i] && host_start[i] != '/' && host_start[i] != ':' && i < 255) {
        hostname[i] = host_start[i];
        i++;
    }
    hostname[i] = 0;

    if (host_start[i] == ':') {
        i++;
        char port_str[10];
        int j = 0;
        while (host_start[i] && host_start[i] != '/' && j < 9) {
            port_str[j++] = host_start[i++];
        }
        port_str[j] = 0;
        port = atoi(port_str);
    }
    current_port = port;

    if (hostname[0]) {
        int k=0; while(hostname[k]) { current_host[k] = hostname[k]; k++; } current_host[k] = 0;
    }
    
    net_ipv4_address_t ip;
    if (parse_ip(hostname, &ip) != 0) {
        if (sys_dns_lookup(hostname, &ip) != 0) return 0;
    }
    
    if (sys_tcp_connect(&ip, port) != 0) return 0;
    
    const char* path = host_start + i;
    if (*path == 0) path = "/";
    
    char request[2048];
    char* r = request;
    const char* s;
    s = "GET "; while(*s) *r++ = *s++;
    s = path; while(*s) *r++ = *s++;
    s = " HTTP/1.1\r\nHost: "; while(*s) *r++ = *s++;
    s = hostname; while(*s) *r++ = *s++;
    if (current_port != 80) {
        *r++ = ':';
        char pbuf[10]; itoa(current_port, pbuf);
        s = pbuf; while(*s) *r++ = *s++;
    }
    s = "\r\nUser-Agent: BoredOS/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n"; while(*s) *r++ = *s++;
    
    sys_tcp_send(request, r - request);
    
    int total = 0;
    int last_render = 0;
    if (progressive) inc_parse_offset = 0; // Reset incremental state for new page
    while (1) {
        int len = sys_tcp_recv(dest_buf + total, max_len - 1 - total);
        if (len <= 0) break;
        total += len;
        if (total >= max_len - 1) break;

        if (progressive && total - last_render > 32768) {
            dest_buf[total] = 0;
            char *body = strstr(dest_buf, "\r\n\r\n");
            if (body) {
                body += 4;
                if (!strstr(dest_buf, "Transfer-Encoding: chunked")) {
                    // Find last '>' to avoid splitting a tag
                    int body_len = total - (body - dest_buf);
                    int safe_len = body_len;
                    while (safe_len > 0 && body[safe_len - 1] != '>') safe_len--;
                    if (safe_len > inc_parse_offset) {
                        parse_html_incremental(body, safe_len);
                        browser_paint();
                        ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
                        last_render = total;
                    }
                }
            }
        }
    }
    dest_buf[total] = 0;
    sys_tcp_close();
    return total;
}

static void decode_jpeg(unsigned char *data, int len, RenderElement *el) {
    njInit();
    if (njDecode(data, len) == NJ_OK) {
        int w = njGetWidth(); int h = njGetHeight();
        unsigned char *rgb = njGetImage();
        if (rgb) {
            int fit_w = w; int fit_h = h;
            if (fit_w > WIN_W - 60) { fit_h = fit_h * (WIN_W - 60) / fit_w; fit_w = WIN_W - 60; }
            if (fit_h > 400) { fit_w = fit_w * 400 / fit_h; fit_h = 400; }
            el->img_pixels = malloc(fit_w * fit_h * sizeof(uint32_t));
            if (el->img_pixels) {
                for (int y = 0; y < fit_h; y++) {
                    int sy = y * h / fit_h;
                    for (int x = 0; x < fit_w; x++) {
                        int sx = x * w / fit_w;
                        int idx = (sy * w + sx) * 3;
                        el->img_pixels[y * fit_w + x] = 0xFF000000 | (rgb[idx] << 16) | (rgb[idx+1] << 8) | rgb[idx+2];
                    }
                }
                el->img_w = fit_w; el->img_h = fit_h;
            }
        }
    }
    njDone();
}

static int decode_chunked_bin(char *body, int total_len) {
    char *src = body; char *dst = body;
    int remaining = total_len;
    int final_len = 0;
    while (remaining > 0) {
        char *endptr;
        int chunk_size = (int)strtol(src, &endptr, 16);
        int head_len = endptr - src;
        src = endptr;
        if (*src == '\r') { src++; head_len++; }
        if (*src == '\n') { src++; head_len++; }
        remaining -= head_len;
        if (chunk_size == 0) break;
        if (remaining < chunk_size) chunk_size = remaining;
        
        for (int i = 0; i < chunk_size; i++) *dst++ = *src++;
        final_len += chunk_size;
        remaining -= chunk_size;
        if (remaining > 0 && *src == '\r') { src++; remaining--; }
        if (remaining > 0 && *src == '\n') { src++; remaining--; }
    }
    *dst = 0;
    return final_len;
}

static void load_image(RenderElement *el) {
    char url[512];
    if (str_istarts_with(el->attr_value, "http")) {
        int k=0; while(el->attr_value[k]) { url[k] = el->attr_value[k]; k++; } url[k] = 0;
    } else {
        char *u = url;
        const char *s = "http://"; while(*s) *u++ = *s++;
        s = current_host; while(*s) *u++ = *s++;
        if (el->attr_value[0] != '/') *u++ = '/';
        s = el->attr_value; while(*s) *u++ = *s++;
        *u = 0;
    }
    static char img_resp[RESP_BUF_SIZE];
    int resp_len = fetch_content(url, img_resp, sizeof(img_resp), false);
    char *body = strstr(img_resp, "\r\n\r\n");
    if (body) {
        body += 4;
        int hdr_len = body - img_resp;
        int body_len = resp_len - hdr_len;
        if (strstr(img_resp, "Transfer-Encoding: chunked")) {
            body_len = decode_chunked_bin(body, body_len);
        }
        decode_jpeg((unsigned char*)body, body_len, el);
    }
}

static int line_elements[512];
static int line_element_count = 0;
static int cur_line_y = 10;
static int cur_line_x = 10;
static int list_depth = 0;

// Incremental parser state for progressive rendering
static int inc_center_depth = 0;
static int inc_table_depth = 0;
static bool inc_is_bold = false;
static uint32_t inc_current_color = COLOR_TEXT;
static char inc_current_link[256] = "";
static float inc_current_scale = 15.0f;
static float inc_base_scale = 15.0f;
static bool inc_is_space_pending = false;
static char inc_form_action[256] = "";
static int inc_form_id = 0;
static bool inc_skip_content = false;

static void flush_line(bool centered) {
    if (line_element_count == 0) return;
    int line_w = 0;
    for (int i = 0; i < line_element_count; i++) line_w += elements[line_elements[i]].w;
    int offset_x = centered ? (WIN_W - SCROLL_BAR_W - line_w) / 2 : 10 + (list_depth * 20);
    if (offset_x < 10) offset_x = 10;

    int max_h = 16;
    int max_baseline = 16;
    
    // First pass: find maximum height and maximum baseline
    for (int i = 0; i < line_element_count; i++) {
        RenderElement *el = &elements[line_elements[i]];
        if (el->tag == TAG_IMG && el->img_h + 10 > max_h) max_h = el->img_h + 10;
        if ((el->tag == TAG_INPUT || el->tag == TAG_BUTTON) && 20 + 10 > max_h) max_h = 20 + 10;
        if (el->tag == TAG_NONE) {
            int fh = ui_get_font_height_scaled(el->scale);
            if (fh + 4 > max_h) max_h = fh + 4;
            if (fh > max_baseline) max_baseline = fh;
        }
    }
    
    // Second pass: apply coordinates with baseline alignment
    for (int i = 0; i < line_element_count; i++) {
        RenderElement *el = &elements[line_elements[i]];
        el->x = offset_x;
        
        // Align text to the baseline of the largest text on this line
        if (el->tag == TAG_NONE) {
            int fh = ui_get_font_height_scaled(el->scale);
            el->y = cur_line_y + (max_baseline - fh); 
        } else {
            el->y = cur_line_y; // Keep images/inputs at the top for now
        }
        
        offset_x += el->w;
    }
    
    cur_line_y += max_h;
    cur_line_x = centered ? 10 : 10 + (list_depth * 20);
    line_element_count = 0;
    total_content_height = cur_line_y + 50;
}


static uint32_t parse_html_color(const char *str) {
    if (!str) return COLOR_TEXT;
    while (*str == ' ' || *str == '\"' || *str == '\'') str++;
    if (*str == '#') {
        char *end;
        uint32_t val = (uint32_t)strtol(str + 1, &end, 16);
        return 0xFF000000 | val; // Assumes RRGGBB
    }
    if (str_istarts_with(str, "red")) return 0xFFFF0000;
    if (str_istarts_with(str, "green")) return 0xFF008000;
    if (str_istarts_with(str, "blue")) return 0xFF0000FF;
    if (str_istarts_with(str, "white")) return 0xFFFFFFFF;
    if (str_istarts_with(str, "black")) return 0xFF000000;
    if (str_istarts_with(str, "yellow")) return 0xFFFFFF00;
    if (str_istarts_with(str, "gray")) return 0xFF808080;
    if (str_istarts_with(str, "purple")) return 0xFF800080;
    return COLOR_TEXT;
}

static void decode_html_entities(char *str) {
    if (!str) return;
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '&') {
            if (str_istarts_with(src, "&quot;")) { *dst++ = '\"'; src += 6; continue; }
            if (str_istarts_with(src, "&amp;")) { *dst++ = '&'; src += 5; continue; }
            if (str_istarts_with(src, "&lt;")) { *dst++ = '<'; src += 4; continue; }
            if (str_istarts_with(src, "&gt;")) { *dst++ = '>'; src += 4; continue; }
            if (str_istarts_with(src, "&apos;")) { *dst++ = '\''; src += 6; continue; }
            if (str_istarts_with(src, "&nbsp;")) { *dst++ = ' '; src += 6; continue; }
            if (str_istarts_with(src, "&mdash;")) { *dst++ = '-'; src += 7; continue; }
            if (str_istarts_with(src, "&ndash;")) { *dst++ = '-'; src += 7; continue; }
            if (str_istarts_with(src, "&lsquo;")) { *dst++ = '\''; src += 7; continue; }
            if (str_istarts_with(src, "&rsquo;")) { *dst++ = '\''; src += 7; continue; }
            if (str_istarts_with(src, "&ldquo;")) { *dst++ = '\"'; src += 7; continue; }
            if (str_istarts_with(src, "&rdquo;")) { *dst++ = '\"'; src += 7; continue; }

            if (src[1] == '#') {
                int val = 0;
                char *end = NULL;
                if (src[2] == 'x' || src[2] == 'X') {
                    val = strtol(src + 3, &end, 16);
                } else {
                    val = strtol(src + 2, &end, 10);
                }
                if (end && *end == ';' && end > src + 2) {
                    if (val == 8216 || val == 8217) val = '\'';
                    else if (val == 8220 || val == 8221) val = '\"';
                    else if (val == 8211 || val == 8212) val = '-';
                    else if (val == 160) val = ' ';
                    
                    if (val > 0 && val < 256) {
                        *dst++ = (char)val;
                        src = end + 1;
                        continue;
                    }
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = 0;
}

static void parse_html(const char *html) {
    browser_clear();
    list_depth = 0;
    cur_line_y = 10; cur_line_x = 10; line_element_count = 0;
    int i = 0; int center_depth = 0; int table_depth = 0; bool is_bold = false; uint32_t current_color = COLOR_TEXT; char current_link[256] = "";
    #define EFF_CENTER ((center_depth > 0) && (table_depth == 0))
    float current_scale = 15.0f; float base_scale = 15.0f;
    bool is_space_pending = false;
    char current_form_action[256] = ""; int current_form_id = 0;
    bool skip_content = false;
    next_form_id = 1;

    while (html[i] && element_count < MAX_ELEMENTS) {
        if (html[i] == '<') {
            if (html[i+1] == '!' && html[i+2] == '-' && html[i+3] == '-') {
                i += 4;
                while (html[i] && !(html[i] == '-' && html[i+1] == '-' && html[i+2] == '>')) i++;
                if (html[i]) i += 3;
                continue;
            }
            i++; char tag_name[64]; int tag_idx = 0;
            while (html[i] && html[i] != '>' && html[i] != ' ' && tag_idx < 63) tag_name[tag_idx++] = html[i++];
            tag_name[tag_idx] = 0;
            char attr_buf[1024] = "";
            if (html[i] == ' ') {
                i++; int a_idx = 0;
                while (html[i] && html[i] != '>' && a_idx < 1023) attr_buf[a_idx++] = html[i++];
                attr_buf[a_idx] = 0;
            }
            if (html[i] == '>') i++;
            decode_html_entities(attr_buf);

            if (tag_name[0] == '/') {
                if (str_istarts_with(tag_name+1, "center")) { flush_line(EFF_CENTER); if (center_depth > 0) center_depth--; }
                else if (str_istarts_with(tag_name+1, "table")) { flush_line(EFF_CENTER); if (table_depth > 0) table_depth--; }
                else if (str_istarts_with(tag_name+1, "ul") || str_istarts_with(tag_name+1, "ol")) { flush_line(EFF_CENTER); if (list_depth > 0) list_depth--; }
                else if (tag_name[1] == 'h' && tag_name[2] >= '1' && tag_name[2] <= '6') { flush_line(EFF_CENTER); cur_line_y += 10; is_bold = false; base_scale = 15.0f; current_scale = 15.0f; }
                else if (str_istarts_with(tag_name+1, "form")) {
                    flush_line(EFF_CENTER);
                    current_form_id = 0; current_form_action[0] = 0;
                }
                else if (str_istarts_with(tag_name+1, "a")) current_link[0] = 0;
                else if (str_istarts_with(tag_name+1, "p") || str_istarts_with(tag_name+1, "li") || str_istarts_with(tag_name+1, "div")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name+1, "font")) { current_color = COLOR_TEXT; current_scale = base_scale; }
                else if (str_istarts_with(tag_name+1, "head") || (tag_name[1] == 's' && tag_name[2] == 'c') || (tag_name[1] == 's' && tag_name[2] == 'i') || (tag_name[1] == 's' && tag_name[2] == 't') || str_istarts_with(tag_name+1, "title") || str_istarts_with(tag_name+1, "noscript") || str_istarts_with(tag_name+1, "style")) skip_content = false;
            } else {
                if (str_istarts_with(tag_name, "center")) { flush_line(EFF_CENTER); center_depth++; }
                else if (str_istarts_with(tag_name, "table")) { flush_line(EFF_CENTER); table_depth++; }
                else if (str_istarts_with(tag_name, "ul") || str_istarts_with(tag_name, "ol")) { flush_line(EFF_CENTER); list_depth++; }
                else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') { 
                    flush_line(EFF_CENTER); cur_line_y += 10; is_bold = true; 
                    if (tag_name[1] == '1') base_scale = 32.0f;
                    else if (tag_name[1] == '2') base_scale = 24.0f;
                    else if (tag_name[1] == '3') base_scale = 20.0f;
                    else base_scale = 18.0f;
                    current_scale = base_scale;
                }
                else if (str_istarts_with(tag_name, "font")) {
                    char *color_str = str_istrstr(attr_buf, "color=\"");
                    if (color_str) {
                        current_color = parse_html_color(color_str + 7);
                    } else {
                        color_str = str_istrstr(attr_buf, "color=");
                        if (color_str) current_color = parse_html_color(color_str + 6);
                    }
                    
                    char *size_str = str_istrstr(attr_buf, "size=\"");
                    int offset = 0;
                    if (size_str) {
                        offset = 6;
                    } else {
                        size_str = str_istrstr(attr_buf, "size=");
                        if (size_str) offset = 5;
                    }
                    if (size_str) {
                        char s_char = size_str[offset];
                        if (s_char == '+') {
                            int inc = size_str[offset+1] - '0';
                            int new_sz = 3 + inc;
                            if (new_sz > 7) new_sz = 7;
                            if (new_sz < 1) new_sz = 1;
                            s_char = '0' + new_sz;
                        } else if (s_char == '-') {
                            int dec = size_str[offset+1] - '0';
                            int new_sz = 3 - dec;
                            if (new_sz > 7) new_sz = 7;
                            if (new_sz < 1) new_sz = 1;
                            s_char = '0' + new_sz;
                        }
                        if (s_char == '1') current_scale = 10.0f;
                        else if (s_char == '2') current_scale = 13.0f;
                        else if (s_char == '3') current_scale = 15.0f;
                        else if (s_char == '4') current_scale = 18.0f;
                        else if (s_char == '5') current_scale = 24.0f;
                        else if (s_char == '6') current_scale = 32.0f;
                        else if (s_char >= '7' && s_char <= '9') current_scale = 48.0f;
                    }
                }
                else if (str_istarts_with(tag_name, "br")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name, "p") || str_istarts_with(tag_name, "div")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name, "li")) {
                    flush_line(EFF_CENTER);
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_NONE;
                    el->content[0] = '-'; el->content[1] = ' '; el->content[2] = 0;
                    el->w = ui_get_string_width_scaled("- ", current_scale);
                    el->h = ui_get_font_height_scaled(current_scale);
                    el->color = current_color;
                    el->centered = EFF_CENTER;
                    el->bold = is_bold;
                    el->scale = current_scale;
                    line_elements[line_element_count++] = element_count - 1;
                    cur_line_x += el->w;
                }
                else if (str_istarts_with(tag_name, "form")) {
                    flush_line(EFF_CENTER);
                    current_form_id = next_form_id++;
                    char *action = str_istrstr(attr_buf, "action=\"");
                    if (action) {
                        action += 8; int l = 0;
                        while(action[l] && action[l] != '\"' && l < 255) { current_form_action[l] = action[l]; l++; }
                        current_form_action[l] = 0;
                    } else current_form_action[0] = 0;
                }
                else if (str_istarts_with(tag_name, "head") || str_istarts_with(tag_name, "script") || str_istarts_with(tag_name, "style") || str_istarts_with(tag_name, "title") || str_istarts_with(tag_name, "noscript")) skip_content = true;
                else if (str_istarts_with(tag_name, "body")) skip_content = false;
                else if (str_istarts_with(tag_name, "a")) {
                    char *href = str_istrstr(attr_buf, "href=\"");
                    if (href) {
                        href += 6; int l = 0;
                        while(href[l] && href[l] != '\"' && l < 255) { current_link[l] = href[l]; l++; }
                        current_link[l] = 0;
                    }
                } else if (str_istarts_with(tag_name, "hr")) {
                    flush_line(EFF_CENTER);
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_HR; 
                    el->w = WIN_W - SCROLL_BAR_W - 40; 
                    el->h = 10; // Extra padding
                    el->centered = true;
                    line_elements[line_element_count++] = element_count - 1;
                    flush_line(EFF_CENTER);
                } else if (str_istarts_with(tag_name, "img")) {
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_IMG; el->w = 100; el->h = 80; el->centered = EFF_CENTER;
                    char *src = str_istrstr(attr_buf, "src=\"");
                    if (src) {
                        src += 5; int l = 0;
                        while(src[l] && src[l] != '\"' && l < 255) { el->attr_value[l] = src[l]; l++; }
                        el->attr_value[l] = 0; load_image(el);
                    }
                    if (el->img_pixels) { el->w = el->img_w; el->h = el->img_h; }
                    line_elements[line_element_count++] = element_count - 1;
                    if (EFF_CENTER || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(EFF_CENTER);
                    else cur_line_x += el->w;
                } else if (str_istarts_with(tag_name, "input")) {
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_INPUT; el->w = 160; el->h = 20; el->centered = EFF_CENTER;
                    char *val = str_istrstr(attr_buf, "value=\"");
                    char *ph = str_istrstr(attr_buf, "placeholder=\"");
                    char *type = str_istrstr(attr_buf, "type=\"");
                    char *name = str_istrstr(attr_buf, "name=\"");
                    
                    el->form_id = current_form_id;
                    el->input_cursor = 0;
                    el->input_scroll = 0;
                    int l;
                    l = 0; while(current_form_action[l]) { el->form_action[l] = current_form_action[l]; l++; } el->form_action[l] = 0;
                    
                    if (name) {
                        name += 6; l = 0;
                        while(name[l] && name[l] != '\"' && l < 63) { el->input_name[l] = name[l]; l++; }
                        el->input_name[l] = 0;
                    } else {
                        l = 0; const char *dn = "q"; while(dn[l]) { el->input_name[l] = dn[l]; l++; } el->input_name[l] = 0;
                    }
                    
                    if (type && str_istarts_with(type+6, "submit")) el->tag = TAG_BUTTON;
                    
                    if (val) {
                        val += 7; int l = 0;
                        while(val[l] && val[l] != '\"' && l < 255) { el->attr_value[l] = val[l]; l++; }
                        el->attr_value[l] = 0;
                    } else if (ph) {
                        ph += 13; int l = 0;
                        while(ph[l] && ph[l] != '\"' && l < 255) { el->attr_value[l] = ph[l]; l++; }
                        el->attr_value[l] = 0;
                    } else el->attr_value[0] = 0;
                    if (el->tag == TAG_BUTTON) el->w = ui_get_string_width(el->attr_value) + 20;
                    line_elements[line_element_count++] = element_count - 1;
                    if (EFF_CENTER || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(EFF_CENTER);
                    else cur_line_x += el->w;
                }
            }
        } else {
            if (!skip_content) {
                while (html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) {
                    is_space_pending = true;
                    i++;
                }

                while (html[i] && html[i] != '<') {
                    char word[256]; int w_idx = 0;
                    if (is_space_pending) {
                        if (cur_line_x > 10) word[w_idx++] = ' ';
                        is_space_pending = false;
                    }
                    
                    while (html[i] && html[i] != '<' && html[i] != ' ' && html[i] != '\n' && html[i] != '\r' && w_idx < 254) {
                        word[w_idx++] = html[i++];
                    }
                    
                    if (html[i] == ' ' || html[i] == '\n' || html[i] == '\r') {
                        is_space_pending = true;
                        while (html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) i++;
                    }
                    
                    word[w_idx] = 0;
                    decode_html_entities(word);
                    w_idx = 0; while (word[w_idx]) w_idx++;
                    if (w_idx > 0) {
                        if (element_count >= MAX_ELEMENTS) break;
                        int word_w = ui_get_string_width_scaled(word, current_scale);
                        if (cur_line_x + word_w > WIN_W - SCROLL_BAR_W - 20) {
                            flush_line(EFF_CENTER);
                            if (word[0] == ' ') {
                                for (int k=0; k<w_idx; k++) word[k] = word[k+1];
                                word_w = ui_get_string_width_scaled(word, current_scale);
                                if (word[0] == 0) continue;
                            }
                        }
                        
                        RenderElement *el = &elements[element_count++];
                        for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                        int k=0; while(word[k]) { el->content[k] = word[k]; k++; } 
                        el->content[k] = 0;
                        el->w = word_w;
                        el->h = ui_get_font_height_scaled(current_scale);
                        el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                        el->centered = EFF_CENTER; el->bold = is_bold;
                        el->scale = current_scale;
                        if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                        
                        line_elements[line_element_count++] = element_count - 1;
                        cur_line_x += el->w;
                    }
                }
            } else {
                while (html[i] && html[i] != '<') i++;
            }
        }
    }
    flush_line(EFF_CENTER);
}

static void parse_html_incremental(const char *html, int safe_len) {
    // On first call, reset everything
    if (inc_parse_offset == 0) {
        browser_clear();
        list_depth = 0;
        cur_line_y = 10; cur_line_x = 10; line_element_count = 0;
        inc_center_depth = 0; inc_table_depth = 0; inc_is_bold = false;
        inc_current_color = COLOR_TEXT; inc_current_link[0] = 0;
        inc_current_scale = 15.0f; inc_base_scale = 15.0f;
        inc_is_space_pending = false;
        inc_form_action[0] = 0; inc_form_id = 0;
        inc_skip_content = false;
        next_form_id = 1;
    }

    int i = inc_parse_offset;
    int center_depth = inc_center_depth;
    int table_depth = inc_table_depth;
    bool is_bold = inc_is_bold;
    uint32_t current_color = inc_current_color;
    char current_link[256];
    { int k=0; while(inc_current_link[k]) { current_link[k] = inc_current_link[k]; k++; } current_link[k] = 0; }
    float current_scale = inc_current_scale;
    float base_scale = inc_base_scale;
    bool is_space_pending = inc_is_space_pending;
    char current_form_action[256];
    { int k=0; while(inc_form_action[k]) { current_form_action[k] = inc_form_action[k]; k++; } current_form_action[k] = 0; }
    int current_form_id = inc_form_id;
    bool skip_content = inc_skip_content;
    #undef EFF_CENTER
    #define EFF_CENTER ((center_depth > 0) && (table_depth == 0))

    while (i < safe_len && html[i] && element_count < MAX_ELEMENTS) {
        if (html[i] == '<') {
            if (html[i+1] == '!' && html[i+2] == '-' && html[i+3] == '-') {
                i += 4;
                while (i < safe_len && html[i] && !(html[i] == '-' && html[i+1] == '-' && html[i+2] == '>')) i++;
                if (html[i]) i += 3;
                continue;
            }
            i++; char tag_name[64]; int tag_idx = 0;
            while (i < safe_len && html[i] && html[i] != '>' && html[i] != ' ' && tag_idx < 63) tag_name[tag_idx++] = html[i++];
            tag_name[tag_idx] = 0;
            char attr_buf[1024] = "";
            if (html[i] == ' ') {
                i++; int a_idx = 0;
                while (i < safe_len && html[i] && html[i] != '>' && a_idx < 1023) attr_buf[a_idx++] = html[i++];
                attr_buf[a_idx] = 0;
            }
            if (html[i] == '>') i++;
            decode_html_entities(attr_buf);

            if (tag_name[0] == '/') {
                if (str_istarts_with(tag_name+1, "center")) { flush_line(EFF_CENTER); if (center_depth > 0) center_depth--; }
                else if (str_istarts_with(tag_name+1, "table")) { flush_line(EFF_CENTER); if (table_depth > 0) table_depth--; }
                else if (str_istarts_with(tag_name+1, "ul") || str_istarts_with(tag_name+1, "ol")) { flush_line(EFF_CENTER); if (list_depth > 0) list_depth--; }
                else if (tag_name[1] == 'h' && tag_name[2] >= '1' && tag_name[2] <= '6') { flush_line(EFF_CENTER); cur_line_y += 10; is_bold = false; base_scale = 15.0f; current_scale = 15.0f; }
                else if (str_istarts_with(tag_name+1, "form")) {
                    flush_line(EFF_CENTER);
                    current_form_id = 0; current_form_action[0] = 0;
                }
                else if (str_istarts_with(tag_name+1, "a")) current_link[0] = 0;
                else if (str_istarts_with(tag_name+1, "p") || str_istarts_with(tag_name+1, "li") || str_istarts_with(tag_name+1, "div")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name+1, "font")) { current_color = COLOR_TEXT; current_scale = base_scale; }
                else if (str_istarts_with(tag_name+1, "head") || (tag_name[1] == 's' && tag_name[2] == 'c') || (tag_name[1] == 's' && tag_name[2] == 'i') || (tag_name[1] == 's' && tag_name[2] == 't') || str_istarts_with(tag_name+1, "title") || str_istarts_with(tag_name+1, "noscript") || str_istarts_with(tag_name+1, "style")) skip_content = false;
            } else {
                if (str_istarts_with(tag_name, "center")) { flush_line(EFF_CENTER); center_depth++; }
                else if (str_istarts_with(tag_name, "table")) { flush_line(EFF_CENTER); table_depth++; }
                else if (str_istarts_with(tag_name, "ul") || str_istarts_with(tag_name, "ol")) { flush_line(EFF_CENTER); list_depth++; }
                else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') {
                    flush_line(EFF_CENTER); cur_line_y += 10; is_bold = true;
                    if (tag_name[1] == '1') base_scale = 32.0f;
                    else if (tag_name[1] == '2') base_scale = 24.0f;
                    else if (tag_name[1] == '3') base_scale = 20.0f;
                    else base_scale = 18.0f;
                    current_scale = base_scale;
                }
                else if (str_istarts_with(tag_name, "font")) {
                    char *color_str = str_istrstr(attr_buf, "color=\"");
                    if (color_str) {
                        current_color = parse_html_color(color_str + 7);
                    } else {
                        color_str = str_istrstr(attr_buf, "color=");
                        if (color_str) current_color = parse_html_color(color_str + 6);
                    }
                    char *size_str = str_istrstr(attr_buf, "size=\"");
                    int offset = 0;
                    if (size_str) { offset = 6; } else { size_str = str_istrstr(attr_buf, "size="); if (size_str) offset = 5; }
                    if (size_str) {
                        char s_char = size_str[offset];
                        if (s_char == '+') { int inc = size_str[offset+1] - '0'; int new_sz = 3 + inc; if (new_sz > 7) new_sz = 7; if (new_sz < 1) new_sz = 1; s_char = '0' + new_sz; }
                        else if (s_char == '-') { int dec = size_str[offset+1] - '0'; int new_sz = 3 - dec; if (new_sz > 7) new_sz = 7; if (new_sz < 1) new_sz = 1; s_char = '0' + new_sz; }
                        if (s_char == '1') current_scale = 10.0f;
                        else if (s_char == '2') current_scale = 13.0f;
                        else if (s_char == '3') current_scale = 15.0f;
                        else if (s_char == '4') current_scale = 18.0f;
                        else if (s_char == '5') current_scale = 24.0f;
                        else if (s_char == '6') current_scale = 32.0f;
                        else if (s_char >= '7' && s_char <= '9') current_scale = 48.0f;
                    }
                }
                else if (str_istarts_with(tag_name, "br")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name, "p") || str_istarts_with(tag_name, "div")) flush_line(EFF_CENTER);
                else if (str_istarts_with(tag_name, "li")) {
                    flush_line(EFF_CENTER);
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_NONE;
                    el->content[0] = '-'; el->content[1] = ' '; el->content[2] = 0;
                    el->w = ui_get_string_width_scaled("- ", current_scale);
                    el->h = ui_get_font_height_scaled(current_scale);
                    el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->scale = current_scale;
                    line_elements[line_element_count++] = element_count - 1;
                    cur_line_x += el->w;
                }
                else if (str_istarts_with(tag_name, "form")) {
                    flush_line(EFF_CENTER);
                    current_form_id = next_form_id++;
                    char *action = str_istrstr(attr_buf, "action=\"");
                    if (action) { action += 8; int l = 0; while(action[l] && action[l] != '"' && l < 255) { current_form_action[l] = action[l]; l++; } current_form_action[l] = 0; }
                    else current_form_action[0] = 0;
                }
                else if (str_istarts_with(tag_name, "head") || str_istarts_with(tag_name, "script") || str_istarts_with(tag_name, "style") || str_istarts_with(tag_name, "title") || str_istarts_with(tag_name, "noscript")) skip_content = true;
                else if (str_istarts_with(tag_name, "body")) skip_content = false;
                else if (str_istarts_with(tag_name, "a")) {
                    char *href = str_istrstr(attr_buf, "href=\"");
                    if (href) { href += 6; int l = 0; while(href[l] && href[l] != '"' && l < 255) { current_link[l] = href[l]; l++; } current_link[l] = 0; }
                } else if (str_istarts_with(tag_name, "hr")) {
                    flush_line(EFF_CENTER);
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_HR; el->w = WIN_W - SCROLL_BAR_W - 40; el->h = 10; el->centered = true;
                    line_elements[line_element_count++] = element_count - 1;
                    flush_line(EFF_CENTER);
                } else if (str_istarts_with(tag_name, "img")) {
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_IMG; el->w = 100; el->h = 80; el->centered = EFF_CENTER;
                    char *src = str_istrstr(attr_buf, "src=\"");
                    if (src) { src += 5; int l = 0; while(src[l] && src[l] != '"' && l < 255) { el->attr_value[l] = src[l]; l++; } el->attr_value[l] = 0; load_image(el); }
                    if (el->img_pixels) { el->w = el->img_w; el->h = el->img_h; }
                    line_elements[line_element_count++] = element_count - 1;
                    if (EFF_CENTER || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(EFF_CENTER);
                    else cur_line_x += el->w;
                } else if (str_istarts_with(tag_name, "input")) {
                    RenderElement *el = &elements[element_count++];
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_INPUT; el->w = 160; el->h = 20; el->centered = EFF_CENTER;
                    char *val = str_istrstr(attr_buf, "value=\""); char *ph = str_istrstr(attr_buf, "placeholder=\"");
                    char *type = str_istrstr(attr_buf, "type=\""); char *name = str_istrstr(attr_buf, "name=\"");
                    el->form_id = current_form_id; el->input_cursor = 0; el->input_scroll = 0;
                    int l; l = 0; while(current_form_action[l]) { el->form_action[l] = current_form_action[l]; l++; } el->form_action[l] = 0;
                    if (name) { name += 6; l = 0; while(name[l] && name[l] != '"' && l < 63) { el->input_name[l] = name[l]; l++; } el->input_name[l] = 0; }
                    else { l = 0; const char *dn = "q"; while(dn[l]) { el->input_name[l] = dn[l]; l++; } el->input_name[l] = 0; }
                    if (type && str_istarts_with(type+6, "submit")) el->tag = TAG_BUTTON;
                    if (val) { val += 7; int l = 0; while(val[l] && val[l] != '"' && l < 255) { el->attr_value[l] = val[l]; l++; } el->attr_value[l] = 0; }
                    else if (ph) { ph += 13; int l = 0; while(ph[l] && ph[l] != '"' && l < 255) { el->attr_value[l] = ph[l]; l++; } el->attr_value[l] = 0; }
                    else el->attr_value[0] = 0;
                    if (el->tag == TAG_BUTTON) el->w = ui_get_string_width(el->attr_value) + 20;
                    line_elements[line_element_count++] = element_count - 1;
                    if (EFF_CENTER || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(EFF_CENTER);
                    else cur_line_x += el->w;
                }
            }
        } else {
            if (!skip_content) {
                while (i < safe_len && html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) {
                    is_space_pending = true; i++;
                }
                while (i < safe_len && html[i] && html[i] != '<') {
                    char word[256]; int w_idx = 0;
                    if (is_space_pending) { if (cur_line_x > 10) word[w_idx++] = ' '; is_space_pending = false; }
                    while (i < safe_len && html[i] && html[i] != '<' && html[i] != ' ' && html[i] != '\n' && html[i] != '\r' && w_idx < 254) {
                        word[w_idx++] = html[i++];
                    }
                    if (html[i] == ' ' || html[i] == '\n' || html[i] == '\r') {
                        is_space_pending = true;
                        while (i < safe_len && html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) i++;
                    }
                    word[w_idx] = 0;
                    decode_html_entities(word);
                    w_idx = 0; while (word[w_idx]) w_idx++;
                    if (w_idx > 0) {
                        if (element_count >= MAX_ELEMENTS) break;
                        int word_w = ui_get_string_width_scaled(word, current_scale);
                        if (cur_line_x + word_w > WIN_W - SCROLL_BAR_W - 20) {
                            flush_line(EFF_CENTER);
                            if (word[0] == ' ') { for (int k=0; k<w_idx; k++) word[k] = word[k+1]; word_w = ui_get_string_width_scaled(word, current_scale); if (word[0] == 0) continue; }
                        }
                        RenderElement *el = &elements[element_count++];
                        for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                        int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                        el->w = word_w; el->h = ui_get_font_height_scaled(current_scale);
                        el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                        el->centered = EFF_CENTER; el->bold = is_bold; el->scale = current_scale;
                        if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                        line_elements[line_element_count++] = element_count - 1;
                        cur_line_x += el->w;
                    }
                }
            } else {
                while (i < safe_len && html[i] && html[i] != '<') i++;
            }
        }
    }
    flush_line(EFF_CENTER);

    // Save state for next incremental call
    inc_parse_offset = i;
    inc_center_depth = center_depth;
    inc_table_depth = table_depth;
    inc_is_bold = is_bold;
    inc_current_color = current_color;
    { int k=0; while(current_link[k]) { inc_current_link[k] = current_link[k]; k++; } inc_current_link[k] = 0; }
    inc_current_scale = current_scale;
    inc_base_scale = base_scale;
    inc_is_space_pending = is_space_pending;
    { int k=0; while(current_form_action[k]) { inc_form_action[k] = current_form_action[k]; k++; } inc_form_action[k] = 0; }
    inc_form_id = current_form_id;
    inc_skip_content = skip_content;
}

static void browser_paint(void) {
    ui_draw_rect(win_browser, 0, 0, WIN_W, WIN_H, COLOR_BG);
    
    for (int i = 0; i < element_count; i++) {
        RenderElement *el = &elements[i];
        int draw_y = el->y - scroll_y + URL_BAR_H;
        if (draw_y < URL_BAR_H - 400 || draw_y > WIN_H) continue;
        if (el->tag == TAG_IMG) {
            if (el->img_pixels) ui_draw_image(win_browser, el->x, draw_y, el->img_w, el->img_h, el->img_pixels);
            else ui_draw_rect(win_browser, el->x, draw_y, 100, 80, 0xFFCCCCCC);
        } else if (el->tag == TAG_INPUT) {
            ui_draw_rect(win_browser, el->x, draw_y, el->w, el->h, 0xFFFFFFFF);
            uint32_t border = (focused_element == i) ? 0xFF0000FF : 0xFF808080;
            ui_draw_rect(win_browser, el->x, draw_y, el->w, 1, border);
            ui_draw_rect(win_browser, el->x, draw_y + el->h - 1, el->w, 1, border);
            ui_draw_rect(win_browser, el->x, draw_y, 1, el->h, border);
            ui_draw_rect(win_browser, el->x + el->w - 1, draw_y, 1, el->h, border);
            
            char visible[64];
            int v_len = 0;
            int max_v = (el->w - 10) / 8;
            if (max_v > 63) max_v = 63;
            for (int k = el->input_scroll; el->attr_value[k] && v_len < max_v; k++) {
                visible[v_len++] = el->attr_value[k];
            }
            visible[v_len] = 0;
            ui_draw_string(win_browser, el->x + 5, draw_y + 2, visible, (focused_element == i) ? 0xFF000000 : 0xFF808080);
            
            if (focused_element == i) {
                int cursor_pos = el->input_cursor - el->input_scroll;
                if (cursor_pos >= 0 && cursor_pos < max_v) {
                    char sub[64];
                    int k;
                    for (k = 0; k < cursor_pos && visible[k]; k++) sub[k] = visible[k];
                    sub[k] = 0;
                    int cx = ui_get_string_width(sub);
                    ui_draw_rect(win_browser, el->x + 5 + cx, draw_y + 16, 8, 2, 0xFF000000);
                }
            }
        } else if (el->tag == TAG_BUTTON) {
            ui_draw_rect(win_browser, el->x, draw_y, el->w, el->h, 0xFFDDDDDD);
            ui_draw_rect(win_browser, el->x, draw_y, el->w, 1, 0xFFFFFFFF);
            ui_draw_rect(win_browser, el->x, draw_y + el->h - 1, el->w, 1, 0xFF888888);
            ui_draw_rect(win_browser, el->x, draw_y, 1, el->h, 0xFFFFFFFF);
            ui_draw_rect(win_browser, el->x + el->w - 1, draw_y, 1, el->h, 0xFF888888);
            ui_draw_string(win_browser, el->x + 10, draw_y + 4, el->attr_value, 0xFF000000);
        } else if (el->tag == TAG_HR) {
            ui_draw_rect(win_browser, el->x, draw_y + el->h / 2, el->w, 2, 0xFF888888);
            ui_draw_rect(win_browser, el->x, draw_y + (el->h / 2) + 2, el->w, 1, 0xFFFFFFFF);
        } else {
            ui_draw_string_scaled(win_browser, el->x, draw_y, el->content, el->color, el->scale);
            if (el->bold) ui_draw_string_scaled(win_browser, el->x + 1, draw_y, el->content, el->color, el->scale);
        }
    }

    ui_draw_rect(win_browser, 0, 0, WIN_W, URL_BAR_H, COLOR_URL_BAR);
    ui_draw_string(win_browser, 10, 8, url_input_buffer, COLOR_URL_TEXT);
    if (focused_element == -1) {
        char sub[512];
        int k;
        for (k = 0; k < url_cursor && url_input_buffer[k]; k++) sub[k] = url_input_buffer[k];
        sub[k] = 0;
        int cx = ui_get_string_width(sub);
        ui_draw_rect(win_browser, 10 + cx, 22, 8, 2, COLOR_URL_TEXT);
    }

    // Back button
    int btn_y = (URL_BAR_H - BTN_H) / 2;
    uint32_t back_col = history_count > 0 ? 0xFF505050 : 0xFF404040;
    ui_draw_rect(win_browser, BACK_BTN_X, btn_y, BTN_W, BTN_H, back_col);
    ui_draw_rect(win_browser, BACK_BTN_X, btn_y, BTN_W, 1, 0xFF606060);
    ui_draw_rect(win_browser, BACK_BTN_X, btn_y, 1, BTN_H, 0xFF606060);
    ui_draw_rect(win_browser, BACK_BTN_X, btn_y + BTN_H - 1, BTN_W, 1, 0xFF202020);
    ui_draw_rect(win_browser, BACK_BTN_X + BTN_W - 1, btn_y, 1, BTN_H, 0xFF202020);
    ui_draw_string(win_browser, BACK_BTN_X + 10, btn_y + 4, "<", history_count > 0 ? 0xFFFFFFFF : 0xFF808080);

    // Home button
    ui_draw_rect(win_browser, HOME_BTN_X, btn_y, BTN_W, BTN_H, 0xFF505050);
    ui_draw_rect(win_browser, HOME_BTN_X, btn_y, BTN_W, 1, 0xFF606060);
    ui_draw_rect(win_browser, HOME_BTN_X, btn_y, 1, BTN_H, 0xFF606060);
    ui_draw_rect(win_browser, HOME_BTN_X, btn_y + BTN_H - 1, BTN_W, 1, 0xFF202020);
    ui_draw_rect(win_browser, HOME_BTN_X + BTN_W - 1, btn_y, 1, BTN_H, 0xFF202020);
    ui_draw_string(win_browser, HOME_BTN_X + 10, btn_y + 4, "H", 0xFFFFFFFF);
    
    // Scroll bar
    ui_draw_rect(win_browser, WIN_W - SCROLL_BAR_W, URL_BAR_H, SCROLL_BAR_W, WIN_H - URL_BAR_H, COLOR_SCROLL_BG);
    int thumb_h = (WIN_H - URL_BAR_H) * (WIN_H - URL_BAR_H) / (total_content_height > WIN_H ? total_content_height : WIN_H);
    if (thumb_h < 20) thumb_h = 20;
    int thumb_y = URL_BAR_H + (scroll_y * (WIN_H - URL_BAR_H - thumb_h)) / (total_content_height > WIN_H - URL_BAR_H ? total_content_height - (WIN_H - URL_BAR_H) : 1);
    ui_draw_rect(win_browser, WIN_W - SCROLL_BAR_W + 2, thumb_y, SCROLL_BAR_W - 4, thumb_h, COLOR_SCROLL_BTN);
}

static void navigate(const char *url) {
    static char main_resp[RESP_BUF_SIZE];
    int resp_len = fetch_content(url, main_resp, sizeof(main_resp), true);
    if (resp_len <= 0) return;
    char *body = strstr(main_resp, "\r\n\r\n");
    if (body) {
        body += 4;
        int hdr_len = body - main_resp;
        int body_len = resp_len - hdr_len;
        if (strstr(main_resp, "Transfer-Encoding: chunked")) {
            body_len = decode_chunked_bin(body, body_len);
            parse_html(body);
        } else {
            // For non-chunked: finish incremental parse instead of re-parsing from scratch
            parse_html_incremental(body, body_len);
        }
    }
}

static void net_init_if_needed(void) {
    if (!sys_network_is_initialized()) sys_network_init();
    if (!sys_network_has_ip()) sys_network_dhcp_acquire();
}

int main(int argc, char **argv) {
    win_browser = ui_window_create("Bored Web", 50, 50, WIN_W, WIN_H);
    net_init_if_needed();
    if (argc > 1) { int k=0; while(argv[1][k]) { url_input_buffer[k] = argv[1][k]; k++; } url_input_buffer[k] = 0; url_cursor = k; }
    navigate(url_input_buffer);
    browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
    gui_event_t ev;
    bool needs_repaint = false;
    while (1) {
        while (ui_get_event(win_browser, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) { needs_repaint = true; }
            else if (ev.type == GUI_EVENT_CLICK) {
                int mx = ev.arg1;
                if (mx >= WIN_W - SCROLL_BAR_W) {
                    if (ev.arg2 < URL_BAR_H + (WIN_H - URL_BAR_H)/2) scroll_y -= 100;
                    else scroll_y += 100;
                    if (scroll_y < 0) scroll_y = 0;
                    needs_repaint = true;
                    continue;
                }
                if (ev.arg2 < URL_BAR_H) {
                    // Check back button
                    if (mx >= BACK_BTN_X && mx < BACK_BTN_X + BTN_W && history_count > 0) {
                        history_count--;
                        int j=0; while(history_stack[history_count][j]) { url_input_buffer[j] = history_stack[history_count][j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                        navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        needs_repaint = true; continue;
                    }
                    // Check home button
                    if (mx >= HOME_BTN_X && mx < HOME_BTN_X + BTN_W) {
                        if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                        const char *home = "http://find.boreddev.nl";
                        int j=0; while(home[j]) { url_input_buffer[j] = home[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                        navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        needs_repaint = true; continue;
                    }
                    focused_element = -1; needs_repaint = true; continue;
                }
                int my = ev.arg2 - URL_BAR_H + scroll_y;
                bool found = false;
                for (int i = 0; i < element_count; i++) {
                    RenderElement *el = &elements[i];
                    if (mx >= el->x && mx < el->x + el->w && my >= el->y && my < el->y + el->h) {
                        if (el->tag == TAG_INPUT) {
                            focused_element = i;
                            int len = 0; while(el->attr_value[len]) len++;
                            el->input_cursor = len;
                            int max_v = (el->w - 10) / 8;
                            if (el->input_cursor < el->input_scroll) el->input_scroll = el->input_cursor;
                            if (el->input_cursor >= el->input_scroll + max_v) el->input_scroll = el->input_cursor - max_v + 1;
                            found = true; needs_repaint = true; break;
                        }
                        if (el->tag == TAG_BUTTON) {
                            int fid = el->form_id;
                            int search_idx = -1;
                            for (int k=0; k<element_count; k++) if (elements[k].tag == TAG_INPUT && elements[k].form_id == fid) { search_idx = k; break; }
                            if (search_idx >= 0) {
                                char search_url[1024];
                                char *u = search_url;
                                const char *s;
                                if (el->form_action[0] == '/') {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    s = el->form_action; while(*s) *u++ = *s++;
                                } else if (str_istarts_with(el->form_action, "http")) {
                                    s = el->form_action; while(*s) *u++ = *s++;
                                } else {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    if (current_host[0] && current_host[0] != '/') *u++ = '/';
                                    if (el->form_action[0]) { s = el->form_action; while(*s) *u++ = *s++; }
                                }
                                
                                s = (strstr(search_url, "?") ? "&" : "?");
                                while(*s) *u++ = *s++;
                                s = elements[search_idx].input_name; while(*s) *u++ = *s++;
                                *u++ = '=';
                                
                                for (int m=0; elements[search_idx].attr_value[m] && (u - search_url) < 1020; m++) {
                                    char sc = elements[search_idx].attr_value[m];
                                    if (sc == ' ') *u++ = '+';
                                    else *u++ = sc;
                                }
                                *u = 0;
                                if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                                int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                                navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                                needs_repaint = true;
                                found = true; break;
                            }
                        }
                        if (el->link_url[0]) {
                            char new_url[512];
                            if (el->link_url[0] == '/') {
                                char *u = new_url; const char *s = "http://"; while(*s) *u++ = *s++;
                                s = current_host; while(*s) *u++ = *s++;
                                if (current_port != 80) {
                                    *u++ = ':';
                                    char pbuf[10]; itoa(current_port, pbuf);
                                    const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                }
                                s = el->link_url; while(*s) *u++ = *s++; *u = 0;
                            } else if (str_istarts_with(el->link_url, "http")) {
                                int k=0; while(el->link_url[k]) { new_url[k] = el->link_url[k]; k++; } new_url[k] = 0;
                            } else {
                                char *u = new_url; const char *s = "http://"; while(*s) *u++ = *s++;
                                s = current_host; while(*s) *u++ = *s++; 
                                if (current_port != 80) {
                                    *u++ = ':';
                                    char pbuf[10]; itoa(current_port, pbuf);
                                    const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                }
                                if (current_host[0] && current_host[0] != '/') *u++ = '/';
                                s = el->link_url; while(*s) *u++ = *s++; *u = 0;
                            }
                            if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                            int j=0; while(new_url[j]) { url_input_buffer[j] = new_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                            navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                            needs_repaint = true;
                            found = true; break;
                        }
                    }
                }
                if (!found) { focused_element = -1; needs_repaint = true; }
            } else if (ev.type == GUI_EVENT_KEY || ev.type == GUI_EVENT_KEYUP) {
                if (ev.type == GUI_EVENT_KEYUP) continue;
                char c = (char)ev.arg1;
                if (focused_element == -1) {
                    if (c == 13 || c == 10) {
                        if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                        navigate(url_input_buffer); scroll_y = 0;
                    }
                    else if (c == 19) { if (url_cursor > 0) url_cursor--; }
                    else if (c == 20) { int len = 0; while(url_input_buffer[len]) len++; if (url_cursor < len) url_cursor++; }
                    else if (c == 127 || c == 8) { 
                        if (url_cursor > 0) {
                            int len = 0; while(url_input_buffer[len]) len++;
                            for (int k=url_cursor-1; k<len; k++) url_input_buffer[k] = url_input_buffer[k+1];
                            url_cursor--;
                        }
                    }
                    else if (c >= 32 && c <= 126 && url_cursor < 511) { 
                        int len = 0; while(url_input_buffer[len]) len++;
                        for (int k=len; k>=url_cursor; k--) url_input_buffer[k+1] = url_input_buffer[k];
                        url_input_buffer[url_cursor++] = c;
                    }
                } else {
                    RenderElement *el = &elements[focused_element];
                    int len = 0; while(el->attr_value[len]) len++;
                    if (c == 13 || c == 10) { 
                        char search_url[1024];
                        char *u = search_url;
                        const char *s;
                        if (el->form_action[0] == '/') {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            s = el->form_action; while(*s) *u++ = *s++;
                        } else if (str_istarts_with(el->form_action, "http")) {
                            s = el->form_action; while(*s) *u++ = *s++;
                        } else {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            if (current_host[0] && current_host[0] != '/') *u++ = '/';
                            if (el->form_action[0]) { s = el->form_action; while(*s) *u++ = *s++; }
                        }
                        
                        s = (strstr(search_url, "?") ? "&" : "?");
                        while(*s) *u++ = *s++;
                        s = el->input_name; while(*s) *u++ = *s++;
                        *u++ = '=';
                        
                        for (int m=0; el->attr_value[m] && (u - search_url) < 1020; m++) {
                            char sc = el->attr_value[m];
                            if (sc == ' ') *u++ = '+';
                            else *u++ = sc;
                        }
                        *u = 0;
                        int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                        navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        needs_repaint = true;
                    }
                    else if (c == 19) { if (el->input_cursor > 0) el->input_cursor--; }
                    else if (c == 20) { if (el->input_cursor < len) el->input_cursor++; }
                    else if (c == 127 || c == 8) { 
                        if (el->input_cursor > 0) {
                            for (int k=el->input_cursor-1; k<len; k++) el->attr_value[k] = el->attr_value[k+1];
                            el->input_cursor--;
                        }
                    }
                    else if (c >= 32 && c <= 126 && len < 255) { 
                        for (int k=len; k>=el->input_cursor; k--) el->attr_value[k+1] = el->attr_value[k];
                        el->attr_value[el->input_cursor++] = c;
                    }

                    int max_v = (el->w - 10) / 8;
                    if (el->input_cursor < el->input_scroll) el->input_scroll = el->input_cursor;
                    if (el->input_cursor >= el->input_scroll + max_v) el->input_scroll = el->input_cursor - max_v + 1;
                }
                
                if (c == 17) { scroll_y -= 40; }
                else if (c == 18) { scroll_y += 40; }
                else if (c == 19) { scroll_y -= 200; } // Page Up
                else if (c == 20) { scroll_y += 200; } // Page Down

                int max_scroll = total_content_height - (WIN_H - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;

                needs_repaint = true;
            } else if (ev.type == 9) { // GUI_EVENT_MOUSE_WHEEL
                scroll_y += ev.arg1 * 20;
                int max_scroll = total_content_height - (WIN_H - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_CLOSE) sys_exit(0);
        }
        if (needs_repaint) {
            browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
            needs_repaint = false;
        }
        for(volatile int x=0; x<10000; x++);
    }
    return 0;
}
