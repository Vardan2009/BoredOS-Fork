// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
#include "syscall.h"
#include "libui.h"
#include "stdlib.h"

#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_PANEL    0xFF1E1E1E
#define COLOR_DARK_TEXT     0xFFE0E0E0
#define COLOR_DARK_BORDER   0xFF333333
#define COLOR_ACCENT        0xFF4FC3F7
#define COLOR_CPU           0xFF81C784
#define COLOR_MEM           0xFFFFB74D
#define COLOR_KILL          0xFFE57373
#define COLOR_DIM_TEXT      0xFF999999

#define MAX_VISIBLE_PROCS 10
#define GRAPH_POINTS 60

static ui_window_t win_taskman;
static ProcessInfo proc_list[32];
static int proc_count = 0;
static int selected_proc = -1;

// History as fixed-point (x100)
static int cpu_history[GRAPH_POINTS];
static int mem_history[GRAPH_POINTS];
static int history_idx = 0;

static uint64_t uptime_prev = 0;
static uint64_t kernel_ticks_prev = 0;
static uint64_t total_mem_system = 0;
static uint64_t used_mem_system = 0;
static char cpu_model_name[64] = "Unknown CPU";

typedef struct {
    size_t total_memory;
    size_t used_memory;
    size_t available_memory;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t largest_free_block;
    size_t smallest_free_block;
    size_t fragmentation_percent;
    size_t peak_memory_used;
} MemStats;

static void update_proc_list(void) {
    proc_count = sys_system(SYSTEM_CMD_PROCESS_LIST, (uint64_t)proc_list, 32, 0, 0);
    
    uint64_t uptime_now = sys_system(SYSTEM_CMD_UPTIME, 0, 0, 0, 0);
    uint64_t kernel_ticks_now = 0;
    
    for (int i = 0; i < proc_count; i++) {
        if (proc_list[i].pid == 0) {
            kernel_ticks_now = proc_list[i].ticks;
            break;
        }
    }
    
    if (uptime_prev > 0) {
        uint64_t total_delta = uptime_now - uptime_prev;
        if (total_delta > 0) {
            uint64_t kernel_delta = kernel_ticks_now - kernel_ticks_prev;
            if (kernel_delta > total_delta) kernel_delta = total_delta;
            
            uint64_t used_delta = total_delta - kernel_delta;
            int usage = (int)((used_delta * 100) / total_delta);
            cpu_history[history_idx] = usage;
        }
    }
    
    uptime_prev = uptime_now;
    kernel_ticks_prev = kernel_ticks_now;
    
    MemStats stats;
    sys_system(SYSTEM_CMD_MEMINFO, (uint64_t)&stats, 0, 0, 0);
    total_mem_system = stats.total_memory;
    used_mem_system = stats.used_memory;
    mem_history[history_idx] = (int)(stats.used_memory / 1024);
    
    history_idx = (history_idx + 1) % GRAPH_POINTS;
}

static void draw_graph(int x, int y, int w, int h, int *data, uint32_t color, int max_val) {
    ui_draw_rect(win_taskman, x, y, w, h, COLOR_DARK_PANEL);
    ui_draw_rect(win_taskman, x, y, w, 1, COLOR_DARK_BORDER);
    ui_draw_rect(win_taskman, x, y + h - 1, w, 1, COLOR_DARK_BORDER);
    
    if (max_val == 0) max_val = 1;
    
    for (int i = 0; i < GRAPH_POINTS - 1; i++) {
        int idx1 = (history_idx + i) % GRAPH_POINTS;
        
        long long val = (long long)data[idx1];
        int h_val = (int)((val * h) / max_val);
        if (h_val > h) h_val = h;
        if (h_val < 0) h_val = 0;
        
        int x1 = x + (i * w) / GRAPH_POINTS;
        int next_x = x + ((i + 1) * w) / GRAPH_POINTS;
        int draw_w = next_x - x1;
        if (draw_w <= 0) draw_w = 1;
        
        ui_draw_rect(win_taskman, x1, y + h - h_val, draw_w, h_val ? h_val : 1, color);
    }
}

static void format_gib(uint64_t bytes, char *out) {
    uint64_t gib_int = bytes / (1024 * 1024 * 1024);
    uint64_t gib_frac = ((bytes % (1024 * 1024 * 1024)) * 100) / (1024 * 1024 * 1024);
    
    char s_int[16], s_frac[16];
    itoa((int)gib_int, s_int);
    itoa((int)gib_frac, s_frac);
    
    out[0] = 0;
    strcat(out, s_int);
    strcat(out, ".");
    if (gib_frac < 10) strcat(out, "0");
    strcat(out, s_frac);
    strcat(out, " GiB");
}

static void draw_taskman(void) {
    int win_w = 400;
    int win_h = 480;
    
    ui_draw_rect(win_taskman, 0, 0, win_w, win_h, COLOR_DARK_BG);
    
    // CPU Graph Area
    ui_draw_string(win_taskman, 10, 10, "PROCESSOR", COLOR_CPU);
    char cpu_label[16];
    int current_cpu = cpu_history[(history_idx + GRAPH_POINTS - 1) % GRAPH_POINTS];
    itoa(current_cpu, cpu_label);
    strcat(cpu_label, "%");
    ui_draw_string(win_taskman, 140, 10, cpu_label, COLOR_CPU);
    draw_graph(10, 25, 185, 60, cpu_history, COLOR_CPU, 100);
    
    // CPU Model (Safe truncation)
    char model_disp[32]; 
    int mlen = strlen(cpu_model_name);
    if (mlen > 22) {
        memcpy(model_disp, cpu_model_name, 19);
        model_disp[19] = '.'; model_disp[20] = '.'; model_disp[21] = '.'; model_disp[22] = 0;
    } else {
        strcpy(model_disp, cpu_model_name);
    }
    ui_draw_string(win_taskman, 10, 92, model_disp, COLOR_DIM_TEXT);
    
    // Memory Graph Area
    ui_draw_string(win_taskman, 205, 10, "MEMORY", COLOR_MEM);
    char mem_pct_label[16];
    int current_mem_pct = 0;
    if (total_mem_system > 0) current_mem_pct = (int)((used_mem_system * 100) / total_mem_system);
    itoa(current_mem_pct, mem_pct_label);
    strcat(mem_pct_label, "%");
    ui_draw_string(win_taskman, 340, 10, mem_pct_label, COLOR_MEM);
    
    int max_mem_kb = (int)(total_mem_system / 1024);
    draw_graph(205, 25, 185, 60, mem_history, COLOR_MEM, max_mem_kb);
    
    // Memory GiB usage
    char s_used[24], s_total[24], mem_text[64];
    format_gib(used_mem_system, s_used);
    format_gib(total_mem_system, s_total);
    mem_text[0] = 0;
    strcat(mem_text, s_used);
    strcat(mem_text, " / ");
    strcat(mem_text, s_total);
    
    ui_draw_string(win_taskman, 205, 92, mem_text, COLOR_DIM_TEXT);
    
    // Process List Header
    ui_draw_rect(win_taskman, 10, 120, 380, 24, COLOR_DARK_PANEL);
    ui_draw_string(win_taskman, 15, 125, "PID", COLOR_DIM_TEXT);
    ui_draw_string(win_taskman, 60, 125, "NAME", COLOR_DIM_TEXT);
    ui_draw_string(win_taskman, 250, 125, "MEMORY", COLOR_DIM_TEXT);
    
    // Process Rows
    int row = 0;
    for (int i = 0; i < proc_count && row < MAX_VISIBLE_PROCS; i++) {
        if (proc_list[i].pid == 0xFFFFFFFF) continue;
        
        int ry = 150 + row * 26;
        uint32_t bg = (selected_proc == row) ? 0xFF334455 : COLOR_DARK_PANEL;
        ui_draw_rounded_rect_filled(win_taskman, 10, ry, 380, 24, 4, bg);
        
        char pid_str[16];
        itoa(proc_list[i].pid, pid_str);
        ui_draw_string(win_taskman, 20, ry + 6, pid_str, COLOR_DARK_TEXT);
        
        char name_disp[28];
        if (strlen(proc_list[i].name) > 22) {
            memcpy(name_disp, proc_list[i].name, 19);
            name_disp[19] = '.'; name_disp[20] = '.'; name_disp[21] = '.'; name_disp[22] = 0;
        } else {
            strcpy(name_disp, proc_list[i].name);
        }
        ui_draw_string(win_taskman, 65, ry + 6, name_disp, COLOR_DARK_TEXT);
        
        char m_str[32];
        itoa((int)(proc_list[i].used_memory / 1024), m_str);
        strcat(m_str, " KB");
        ui_draw_string(win_taskman, 255, ry + 6, m_str, COLOR_DARK_TEXT);
        
        row++;
    }
    
    // Kill button (Positioned relative to window height)
    int btn_x = 400 - 110;
    int btn_y = 480 - 70;
    int btn_w = 100;
    int btn_h = 30;

    // Disable kill for PID 0
    bool can_kill = (selected_proc != -1);
    if (can_kill) {
        int v_cnt = 0;
        for (int i = 0; i < proc_count; i++) {
            if (proc_list[i].pid != 0xFFFFFFFF) {
                if (v_cnt == selected_proc) {
                    if (proc_list[i].pid == 0) can_kill = false;
                    break;
                }
                v_cnt++;
            }
        }
    }

    ui_draw_rounded_rect_filled(win_taskman, btn_x, btn_y, btn_w, btn_h, 6, can_kill ? COLOR_KILL : COLOR_DARK_BORDER);
    
    const char *btn_text = "FORCE KILL";
    int tx = btn_x + (btn_w - 80) / 2;
    int ty = btn_y + (btn_h - 12) / 2;
    ui_draw_string(win_taskman, tx, ty, btn_text, can_kill ? 0xFFFFFFFF : 0xFF666666);
}

int main(void) {
    win_taskman = ui_window_create("Task Manager", 100, 100, 400, 480);
    
    // Fetch CPU model
    sys_system(SYSTEM_CMD_GET_CPU_MODEL, (uint64_t)cpu_model_name, 0, 0, 0);
    
    for(int i=0; i<GRAPH_POINTS; i++) { cpu_history[i] = 0; mem_history[i] = 0; }
    
    gui_event_t ev;
    
    while (1) {
        // Drain events
        while (ui_get_event(win_taskman, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            } else if (ev.type == GUI_EVENT_CLICK) {
                int mx = ev.arg1;
                int my = ev.arg2;
                
                if (mx >= 10 && mx < 390 && my >= 150 && my < 150 + MAX_VISIBLE_PROCS * 26) {
                    int idx = (my - 150) / 26;
                    int valid_count = 0;
                    int target_i = -1;
                    for (int i = 0; i < proc_count; i++) {
                        if (proc_list[i].pid != 0xFFFFFFFF) {
                            if (valid_count == idx) { target_i = i; break; }
                            valid_count++;
                        }
                    }
                    if (target_i != -1) selected_proc = idx;
                    else selected_proc = -1;
                    
                    draw_taskman();
                    ui_mark_dirty(win_taskman, 0, 0, 400, 480);
                } else if (mx >= 290 && mx < 390 && my >= 410 && my < 440) {
                    if (selected_proc != -1) {
                        int valid_count = 0;
                        for (int i = 0; i < proc_count; i++) {
                            if (proc_list[i].pid != 0xFFFFFFFF) {
                                if (valid_count == selected_proc) {
                                    if (proc_list[i].pid != 0) sys_kill(proc_list[i].pid);
                                    break;
                                }
                                valid_count++;
                            }
                        }
                        selected_proc = -1;
                        update_proc_list();
                        draw_taskman();
                        ui_mark_dirty(win_taskman, 0, 0, 400, 480);
                    }
                }
            } else if (ev.type == GUI_EVENT_PAINT) {
                draw_taskman();
                ui_mark_dirty(win_taskman, 0, 0, 400, 480);
            }
        }
        
        update_proc_list();
        draw_taskman();
        ui_mark_dirty(win_taskman, 0, 0, 400, 480);
        
        // Proper blocking sleep (200ms)
        sys_system(46, 200, 0, 0, 0); // SYSTEM_CMD_SLEEP
    }
    
    return 0;
}
