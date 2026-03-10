// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gui_ipc.h"

#define MAX_GUI_EVENTS 32
#define MAX_PROCESS_FDS 16

struct FAT32_FileHandle;

typedef struct registers_t {
    uint8_t fxsave_region[512]; 
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed, aligned(16))) registers_t;

typedef struct process {
    uint32_t pid;
    uint64_t rsp; 
    uint64_t pml4_phys; 
    uint64_t kernel_stack; 
    bool is_user;
    
    gui_event_t gui_events[MAX_GUI_EVENTS];
    int gui_event_head;
    int gui_event_tail;
    void *ui_window; 
    
    uint64_t heap_start;
    uint64_t heap_end;
    
    void *fds[MAX_PROCESS_FDS];
    
    void *kernel_stack_alloc; 
    void *user_stack_alloc;  

    bool is_terminal_proc;   

    struct process *next;

    bool fpu_initialized; 
    
    char name[64];
    uint64_t ticks;
    uint64_t sleep_until;
} __attribute__((aligned(16))) process_t;

typedef struct {
    uint32_t pid;
    char name[64];
    uint64_t ticks;
    size_t used_memory;
} ProcessInfo;

void process_init(void);
void process_create(void* entry_point, bool is_user);
process_t* process_create_elf(const char* filepath, const char* args_str);
process_t* process_get_current(void);
uint64_t process_schedule(uint64_t current_rsp);
uint64_t process_terminate_current(void);
void process_terminate(process_t *proc);
process_t* process_get_by_pid(uint32_t pid);

void process_push_gui_event(process_t *proc, gui_event_t *ev);
process_t* process_get_by_ui_window(void* win);

#endif

