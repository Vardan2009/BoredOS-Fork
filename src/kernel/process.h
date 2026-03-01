// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include "gui_ipc.h"

#define MAX_GUI_EVENTS 32
#define MAX_PROCESS_FDS 16

struct FAT32_FileHandle;

// Registers saved on the stack by interrupts/exceptions
typedef struct registers_t {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef struct process {
    uint32_t pid;
    uint64_t rsp; // Current stack pointer representing context
    uint64_t pml4_phys; // Physical address of the page table
    uint64_t kernel_stack; // Ring 0 stack pointer for user mode switches
    bool is_user;
    
    gui_event_t gui_events[MAX_GUI_EVENTS];
    int gui_event_head;
    int gui_event_tail;
    void *ui_window; // Pointer to the active Window
    
    uint64_t heap_start;
    uint64_t heap_end;
    
    void *fds[MAX_PROCESS_FDS];
    
    void *kernel_stack_alloc; // Original pointer from kmalloc for freeing
    void *user_stack_alloc;   // Original pointer from kmalloc for freeing

    bool is_terminal_proc;     // Was this process started from the shell?

    struct process *next;
} process_t;

void process_init(void);
void process_create(void* entry_point, bool is_user);
process_t* process_create_elf(const char* filepath, const char* args_str);
process_t* process_get_current(void);
uint64_t process_schedule(uint64_t current_rsp);
uint64_t process_terminate_current(void);

void process_push_gui_event(process_t *proc, gui_event_t *ev);

#endif

