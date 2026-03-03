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
#define RESP_BUF_SIZE (1024 * 1024)

#define COLOR_URL_BAR    0xFF303030
#define COLOR_URL_TEXT   0xFFF0F0F0
#define COLOR_BG         0xFFFFFFFF
#define COLOR_TEXT       0xFF000000
#define COLOR_LINK       0xFF0000EE
#define COLOR_SCROLL_BG  0xFFEEEEEE
#define COLOR_SCROLL_BTN 0xFFCCCCCC

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

typedef enum { TAG_NONE, TAG_IMG, TAG_INPUT, TAG_BUTTON } HTMLTag;

typedef struct {
    char content[1024];
    int x, y, w, h;
    HTMLTag tag;
    char link_url[256];
    char attr_value[256];
    uint32_t color;
    bool centered;
    bool bold;
    uint32_t *img_pixels;
    int img_w, img_h;
} RenderElement;

#define MAX_ELEMENTS 8192
static RenderElement elements[MAX_ELEMENTS];
static int element_count = 0;

static char url_input_buffer[512] = "http://frogfind.com";
static int url_cursor = 19;
static char current_host[256] = "frogfind.com";
static int current_port = 80;
static char current_form_action[256] = "";
static char current_input_name[64] = "q";

static ui_window_t win_browser;
static int scroll_y = 0;
static int total_content_height = 0;
static int focused_element = -1; // -1 for URL bar, >= 0 for page elements

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

static int fetch_content(const char *url, char *dest_buf, int max_len) {
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
    while (1) {
        int len = sys_tcp_recv(dest_buf + total, max_len - 1 - total);
        if (len <= 0) break;
        total += len;
        if (total >= max_len - 1) break;
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
    int resp_len = fetch_content(url, img_resp, sizeof(img_resp));
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

static void flush_line(bool centered) {
    if (line_element_count == 0) return;
    int line_w = 0;
    for (int i = 0; i < line_element_count; i++) line_w += elements[line_elements[i]].w;
    int offset_x = centered ? (WIN_W - SCROLL_BAR_W - line_w) / 2 : 10;
    if (offset_x < 10) offset_x = 10;

    int max_h = 16;
    for (int i = 0; i < line_element_count; i++) {
        RenderElement *el = &elements[line_elements[i]];
        el->x = offset_x;
        el->y = cur_line_y;
        offset_x += el->w;
        if (el->tag == TAG_IMG && el->img_h + 10 > max_h) max_h = el->img_h + 10;
        if ((el->tag == TAG_INPUT || el->tag == TAG_BUTTON) && 24 + 10 > max_h) max_h = 24 + 10;
    }
    cur_line_y += max_h;
    cur_line_x = 10;
    line_element_count = 0;
    total_content_height = cur_line_y + 50;
}


static void parse_html(const char *html) {
    browser_clear();
    cur_line_y = 10; cur_line_x = 10; line_element_count = 0;
    int i = 0; bool is_centered = false; bool is_bold = false; uint32_t current_color = COLOR_TEXT; char current_link[256] = "";
    bool skip_content = false;

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

            if (tag_name[0] == '/') {
                if (str_istarts_with(tag_name+1, "center")) { flush_line(is_centered); is_centered = false; }
                else if (tag_name[1] == 'h' && tag_name[2] >= '1' && tag_name[2] <= '6') { flush_line(is_centered); cur_line_y += 10; is_bold = false; }
                else if (str_istarts_with(tag_name+1, "a")) current_link[0] = 0;
                else if (str_istarts_with(tag_name+1, "p") || str_istarts_with(tag_name+1, "li") || str_istarts_with(tag_name+1, "ol") || str_istarts_with(tag_name+1, "form") || str_istarts_with(tag_name+1, "div")) flush_line(is_centered);
                else if (str_istarts_with(tag_name+1, "font")) current_color = COLOR_TEXT;
                else if (str_istarts_with(tag_name+1, "head") || (tag_name[1] == 's' && tag_name[2] == 'c') || (tag_name[1] == 's' && tag_name[2] == 't') || str_istarts_with(tag_name+1, "title") || str_istarts_with(tag_name+1, "noscript") || str_istarts_with(tag_name+1, "style")) skip_content = false;
            } else {
                if (str_istarts_with(tag_name, "center")) { flush_line(is_centered); is_centered = true; }
                else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') { flush_line(is_centered); cur_line_y += 10; is_bold = true; }
                else if (str_istarts_with(tag_name, "br")) flush_line(is_centered);
                else if (str_istarts_with(tag_name, "h") || str_istarts_with(tag_name, "p") || str_istarts_with(tag_name, "hr") || str_istarts_with(tag_name, "li") || str_istarts_with(tag_name, "ol") || str_istarts_with(tag_name, "div")) flush_line(is_centered);
                else if (str_istarts_with(tag_name, "form")) {
                    flush_line(is_centered);
                    char *action = str_istrstr(attr_buf, "action=\"");
                    if (action) {
                        action += 8; int l = 0;
                        while(action[l] && action[l] != '\"' && l < 255) { current_form_action[l] = action[l]; l++; }
                        current_form_action[l] = 0;
                    } else current_form_action[0] = 0;
                }
                else if (str_istarts_with(tag_name, "head") || str_istarts_with(tag_name, "script") || str_istarts_with(tag_name, "style") || str_istarts_with(tag_name, "title") || str_istarts_with(tag_name, "noscript") || str_istarts_with(tag_name, "meta") || str_istarts_with(tag_name, "link") || str_istarts_with(tag_name, "!doctype")) skip_content = true;
                else if (str_istarts_with(tag_name, "body")) skip_content = false;
                else if (str_istarts_with(tag_name, "a")) {
                    char *href = str_istrstr(attr_buf, "href=\"");
                    if (href) {
                        href += 6; int l = 0;
                        while(href[l] && href[l] != '\"' && l < 255) { current_link[l] = href[l]; l++; }
                        current_link[l] = 0;
                    }
                } else if (str_istarts_with(tag_name, "img")) {
                    RenderElement *el = &elements[element_count++];
                    int idx = element_count - 1;
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_IMG; el->w = 100; el->h = 80; el->centered = is_centered;
                    char *src = str_istrstr(attr_buf, "src=\"");
                    if (src) {
                        src += 5; int l = 0;
                        while(src[l] && src[l] != '\"' && l < 255) { el->attr_value[l] = src[l]; l++; }
                        el->attr_value[l] = 0; load_image(el);
                    }
                    if (el->img_pixels) { el->w = el->img_w; el->h = el->img_h; }
                    line_elements[line_element_count++] = element_count - 1;
                    if (is_centered || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(is_centered);
                    else cur_line_x += el->w;
                } else if (str_istarts_with(tag_name, "input")) {
                    RenderElement *el = &elements[element_count++];
                    int idx = element_count - 1;
                    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                    el->tag = TAG_INPUT; el->w = 160; el->h = 24; el->centered = is_centered;
                    char *val = str_istrstr(attr_buf, "value=\"");
                    char *ph = str_istrstr(attr_buf, "placeholder=\"");
                    char *type = str_istrstr(attr_buf, "type=\"");
                    char *name = str_istrstr(attr_buf, "name=\"");
                    if (name) {
                        name += 6; int l = 0;
                        while(name[l] && name[l] != '\"' && l < 63) { current_input_name[l] = name[l]; l++; }
                        current_input_name[l] = 0;
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
                    if (el->tag == TAG_BUTTON) el->w = (int)strlen(el->attr_value) * 8 + 20;
                    line_elements[line_element_count++] = element_count - 1;
                    if (is_centered || cur_line_x + el->w > WIN_W - SCROLL_BAR_W - 20) flush_line(is_centered);
                    else cur_line_x += el->w;
                }
            }
        } else {
            if (!skip_content) {
                while (html[i] && html[i] != '<') {
                    while (html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) i++;
                    if (!html[i] || html[i] == '<') break;
                    
                    char word[256]; int w_idx = 0;
                    while (html[i] && html[i] != '<' && html[i] != ' ' && html[i] != '\n' && html[i] != '\r' && w_idx < 255) {
                        word[w_idx++] = html[i++];
                    }
                    word[w_idx] = 0;
                    if (w_idx > 0) {
                        if (element_count >= MAX_ELEMENTS) break;
                        int word_w = w_idx * 8;
                        if (cur_line_x + word_w > WIN_W - SCROLL_BAR_W - 20) flush_line(is_centered);
                        
                        RenderElement *el = &elements[element_count++];
                        for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                        int k=0; while(word[k]) { el->content[k] = word[k]; k++; } 
                        el->content[k++] = ' '; el->content[k] = 0;
                        el->w = (w_idx + 1) * 8; el->h = 16;
                        el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                        el->centered = is_centered; el->bold = is_bold;
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
    flush_line(is_centered);
}

static void browser_paint(void) {
    ui_draw_rect(win_browser, 0, 0, WIN_W, WIN_H, COLOR_BG);
    ui_draw_rect(win_browser, 0, 0, WIN_W, URL_BAR_H, COLOR_URL_BAR);
    ui_draw_string(win_browser, 10, 8, url_input_buffer, COLOR_URL_TEXT);
    if (focused_element == -1) {
        ui_draw_rect(win_browser, 10 + url_cursor * 8, 22, 8, 2, COLOR_URL_TEXT);
    }
    
    // Scroll bar
    ui_draw_rect(win_browser, WIN_W - SCROLL_BAR_W, URL_BAR_H, SCROLL_BAR_W, WIN_H - URL_BAR_H, COLOR_SCROLL_BG);
    int thumb_h = (WIN_H - URL_BAR_H) * (WIN_H - URL_BAR_H) / (total_content_height > WIN_H ? total_content_height : WIN_H);
    if (thumb_h < 20) thumb_h = 20;
    int thumb_y = URL_BAR_H + (scroll_y * (WIN_H - URL_BAR_H - thumb_h)) / (total_content_height > WIN_H - URL_BAR_H ? total_content_height - (WIN_H - URL_BAR_H) : 1);
    ui_draw_rect(win_browser, WIN_W - SCROLL_BAR_W + 2, thumb_y, SCROLL_BAR_W - 4, thumb_h, COLOR_SCROLL_BTN);

    for (int i = 0; i < element_count; i++) {
        RenderElement *el = &elements[i];
        int draw_y = el->y - scroll_y + URL_BAR_H;
        if (draw_y < URL_BAR_H - 1000 || draw_y > WIN_H) continue;
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
            ui_draw_string(win_browser, el->x + 5, draw_y + 4, el->attr_value, (focused_element == i) ? 0xFF000000 : 0xFF808080);
            if (focused_element == i) {
                int ilen = 0; while(el->attr_value[ilen]) ilen++;
                ui_draw_rect(win_browser, el->x + 5 + ilen * 8, draw_y + 18, 8, 2, 0xFF000000);
            }
        } else if (el->tag == TAG_BUTTON) {
            ui_draw_rect(win_browser, el->x, draw_y, el->w, el->h, 0xFFDDDDDD);
            ui_draw_rect(win_browser, el->x, draw_y, el->w, 1, 0xFFFFFFFF);
            ui_draw_rect(win_browser, el->x, draw_y + el->h - 1, el->w, 1, 0xFF888888);
            ui_draw_rect(win_browser, el->x, draw_y, 1, el->h, 0xFFFFFFFF);
            ui_draw_rect(win_browser, el->x + el->w - 1, draw_y, 1, el->h, 0xFF888888);
            ui_draw_string(win_browser, el->x + 10, draw_y + 4, el->attr_value, 0xFF000000);
        } else {
            ui_draw_string(win_browser, el->x, draw_y, el->content, el->color);
            if (el->bold) ui_draw_string(win_browser, el->x + 1, draw_y, el->content, el->color);
        }
    }
}

static void navigate(const char *url) {
    static char main_resp[RESP_BUF_SIZE];
    int resp_len = fetch_content(url, main_resp, sizeof(main_resp));
    if (resp_len <= 0) return;
    char *body = strstr(main_resp, "\r\n\r\n");
    if (body) {
        body += 4;
        int hdr_len = body - main_resp;
        int body_len = resp_len - hdr_len;
        if (strstr(main_resp, "Transfer-Encoding: chunked")) {
            body_len = decode_chunked_bin(body, body_len);
        }
        parse_html(body);
    }
}

static void net_init_if_needed(void) {
    if (!sys_network_is_initialized()) sys_network_init();
    if (!sys_network_has_ip()) sys_network_dhcp_acquire();
}

int main(int argc, char **argv) {
    win_browser = ui_window_create("Boredweb", 50, 50, WIN_W, WIN_H);
    net_init_if_needed();
    if (argc > 1) { int k=0; while(argv[1][k]) { url_input_buffer[k] = argv[1][k]; k++; } url_input_buffer[k] = 0; url_cursor = k; }
    navigate(url_input_buffer);
    browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
    gui_event_t ev;
    while (1) {
        if (ui_get_event(win_browser, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) { browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H); }
            else if (ev.type == GUI_EVENT_CLICK) {
                int mx = ev.arg1;
                if (mx >= WIN_W - SCROLL_BAR_W) {
                    if (ev.arg2 < URL_BAR_H + (WIN_H - URL_BAR_H)/2) scroll_y -= 100;
                    else scroll_y += 100;
                    if (scroll_y < 0) scroll_y = 0;
                    browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
                    continue;
                }
                if (ev.arg2 < URL_BAR_H) { focused_element = -1; browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H); continue; }
                int my = ev.arg2 - URL_BAR_H + scroll_y;
                bool found = false;
                for (int i = 0; i < element_count; i++) {
                    RenderElement *el = &elements[i];
                    if (mx >= el->x && mx < el->x + el->w && my >= el->y && my < el->y + el->h) {
                        if (el->tag == TAG_INPUT) {
                            focused_element = i; found = true; browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H); break;
                        }
                        if (el->tag == TAG_BUTTON) {
                            int search_idx = -1;
                            for (int k=0; k<element_count; k++) if (elements[k].tag == TAG_INPUT) { search_idx = k; break; }
                            if (search_idx >= 0) {
                                char search_url[1024];
                                char *u = search_url;
                                const char *s;
                                if (current_form_action[0] == '/') {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    s = current_form_action; while(*s) *u++ = *s++;
                                } else if (str_istarts_with(current_form_action, "http")) {
                                    s = current_form_action; while(*s) *u++ = *s++;
                                } else {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    if (current_host[0] && current_host[0] != '/') *u++ = '/';
                                    if (current_form_action[0]) { s = current_form_action; while(*s) *u++ = *s++; }
                                }
                                
                                s = (strstr(search_url, "?") ? "&" : "?");
                                while(*s) *u++ = *s++;
                                s = current_input_name; while(*s) *u++ = *s++;
                                *u++ = '=';
                                
                                for (int m=0; elements[search_idx].attr_value[m] && (u - search_url) < 1020; m++) {
                                    char sc = elements[search_idx].attr_value[m];
                                    if (sc == ' ') *u++ = '+';
                                    else *u++ = sc;
                                }
                                *u = 0;
                                int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                                navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                                browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
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
                            int j=0; while(new_url[j]) { url_input_buffer[j] = new_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                            navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                            browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
                            found = true; break;
                        }
                    }
                }
                if (!found) { focused_element = -1; browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H); }
            } else if (ev.type == GUI_EVENT_KEY) {
                char c = (char)ev.arg1;
                if (focused_element == -1) {
                    if (c == 13 || c == 10) { navigate(url_input_buffer); scroll_y = 0; }
                    else if (c == 127 || c == 8) { if (url_cursor > 0) url_input_buffer[--url_cursor] = 0; }
                    else if (c >= 32 && c <= 126 && url_cursor < 511) { url_input_buffer[url_cursor++] = c; url_input_buffer[url_cursor] = 0; }
                } else {
                    RenderElement *el = &elements[focused_element];
                    int len = 0; while(el->attr_value[len]) len++;
                    if (c == 13 || c == 10) { 
                        char search_url[1024];
                        char *u = search_url;
                        const char *s;
                        if (current_form_action[0] == '/') {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            s = current_form_action; while(*s) *u++ = *s++;
                        } else if (str_istarts_with(current_form_action, "http")) {
                            s = current_form_action; while(*s) *u++ = *s++;
                        } else {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            if (current_host[0] && current_host[0] != '/') *u++ = '/';
                            if (current_form_action[0]) { s = current_form_action; while(*s) *u++ = *s++; }
                        }
                        
                        s = (strstr(search_url, "?") ? "&" : "?");
                        while(*s) *u++ = *s++;
                        s = current_input_name; while(*s) *u++ = *s++;
                        *u++ = '=';
                        
                        for (int m=0; el->attr_value[m] && (u - search_url) < 1020; m++) {
                            char sc = el->attr_value[m];
                            if (sc == ' ') *u++ = '+';
                            else *u++ = sc;
                        }
                        *u = 0;
                        int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                        navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                    }
                    else if (c == 127 || c == 8) { if (len > 0) el->attr_value[--len] = 0; }
                    else if (c >= 32 && c <= 126 && len < 255) { el->attr_value[len++] = c; el->attr_value[len] = 0; }
                }
                
                if (c == 17) { scroll_y -= 40; }
                else if (c == 18) { scroll_y += 40; }
                else if (c == 19) { scroll_y -= 200; } // Page Up
                else if (c == 20) { scroll_y += 200; } // Page Down

                int max_scroll = total_content_height - (WIN_H - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;

                browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
            } else if (ev.type == 9) { // GUI_EVENT_MOUSE_WHEEL
                scroll_y += ev.arg1 * 20;
                int max_scroll = total_content_height - (WIN_H - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;
                browser_paint(); ui_mark_dirty(win_browser, 0, 0, WIN_W, WIN_H);
            } else if (ev.type == GUI_EVENT_CLOSE) sys_exit(0);
        } else { for(volatile int x=0; x<10000; x++); }
    }
    return 0;
}
