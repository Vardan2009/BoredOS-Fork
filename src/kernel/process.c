// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "process.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "io.h"
#include "platform.h"
#include "memory_manager.h"
#include "elf.h"
#include "wm.h"

extern void cmd_write(const char *str);
extern void serial_write(const char *str);

#define MAX_PROCESSES 16
static process_t processes[MAX_PROCESSES];
static int process_count = 0;
static process_t* current_process = NULL;
static uint32_t next_pid = 0;

void process_init(void) {
    // Current kernel execution is PID 0
    process_t *kernel_proc = &processes[process_count++];
    kernel_proc->pid = next_pid++;
    kernel_proc->is_user = false;
    
    // We don't have its RSP or PML4 yet, but it's already running.
    // The timer interrupt will naturally capture its context on the first tick!
    kernel_proc->pml4_phys = paging_get_pml4_phys();
    kernel_proc->kernel_stack = 0;
    
    for (int i = 0; i < MAX_PROCESS_FDS; i++) kernel_proc->fds[i] = NULL;
    
    kernel_proc->next = kernel_proc; // Circular linked list
    current_process = kernel_proc;
}

void process_create(void* entry_point, bool is_user) {
    if (process_count >= MAX_PROCESSES) return;
    
    process_t *new_proc = &processes[process_count++];
    new_proc->pid = next_pid++;
    new_proc->is_user = is_user;
    
    // 1. Setup Page Table
    if (is_user) {
        new_proc->pml4_phys = paging_create_user_pml4_phys();
    } else {
        new_proc->pml4_phys = paging_get_pml4_phys();
    }
    
    if (!new_proc->pml4_phys) return;
    
    // 2. Allocate aligned stack
    void* stack = kmalloc_aligned(4096, 4096);
    void* kernel_stack = kmalloc_aligned(16384, 16384); // Needed for when user interrupts to Ring 0
    
    if (is_user) {
        // Map user stack to 0x800000
        paging_map_page(new_proc->pml4_phys, 0x800000, v2p((uint64_t)stack), PT_PRESENT | PT_RW | PT_USER);
        
        // Allocate code page aligned and copy code
        void* code = kmalloc_aligned(4096, 4096);
        for(int i=0; i<128; i++) ((uint8_t*)code)[i] = ((uint8_t*)entry_point)[i];
        
        paging_map_page(new_proc->pml4_phys, 0x400000, v2p((uint64_t)code), PT_PRESENT | PT_RW | PT_USER);
        
        // Build initial stack frame for iretq
        // Stack grows down, start at top
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 16384);
        
        *(--stack_ptr) = 0x1B;          // SS (User Data)
        *(--stack_ptr) = 0x800000 + 4096; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS (IF=1)
        *(--stack_ptr) = 0x23;          // CS (User Code)
        *(--stack_ptr) = 0x400000;      // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        new_proc->kernel_stack = (uint64_t)kernel_stack + 16384;
        new_proc->rsp = (uint64_t)stack_ptr;
    } else {
        // Kernel thread
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)stack + 4096);
        *(--stack_ptr) = 0x10;          // SS (Kernel Data)
        stack_ptr--;
        *stack_ptr = (uint64_t)stack_ptr; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS
        *(--stack_ptr) = 0x08;          // CS (Kernel Code)
        *(--stack_ptr) = (uint64_t)entry_point; // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        new_proc->kernel_stack = 0;
        new_proc->rsp = (uint64_t)stack_ptr;
    }
    
    // Add to linked list
    new_proc->next = current_process->next;
    current_process->next = new_proc;
}

process_t* process_create_elf(const char* filepath, const char* args_str) {
    process_t *new_proc = NULL;
    
    // Find an available slot
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == 0xFFFFFFFF || i >= process_count) {
            new_proc = &processes[i];
            if (i >= process_count) process_count = i + 1;
            break;
        }
    }

    if (!new_proc) return NULL;
    
    new_proc->pid = next_pid++;
    new_proc->is_user = true;
    
    // 1. Setup Page Table
    new_proc->pml4_phys = paging_create_user_pml4_phys();
    if (!new_proc->pml4_phys) return NULL;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) new_proc->fds[i] = NULL;
    new_proc->gui_event_head = 0;
    new_proc->gui_event_tail = 0;
    new_proc->ui_window = NULL;
    new_proc->heap_start = 0x20000000; // 512MB mark
    new_proc->heap_end = 0x20000000;
    new_proc->is_terminal_proc = false;

    // 2. Load ELF executable
    uint64_t entry_point = elf_load(filepath, new_proc->pml4_phys);
    if (entry_point == 0) {
        serial_write("[PROCESS] Failed to load ELF: ");
        serial_write(filepath);
        serial_write("\n");
        // We technically leak the page table here, but let's ignore cleanup for now
        return NULL;
    }

    // 3. Allocate generic User stack and Kernel stack for interrupts
    void* stack = kmalloc_aligned(65536, 4096);
    void* kernel_stack = kmalloc_aligned(16384, 16384); 
    
    // Map User stack to 0x800000 (starting from 0x7F0000 for 64KB)
    for (uint64_t i = 0; i < 16; i++) {
        paging_map_page(new_proc->pml4_phys, 0x800000 - 65536 + (i * 4096), v2p((uint64_t)stack + (i * 4096)), PT_PRESENT | PT_RW | PT_USER);
    }

 
    int argc = 1;
    char *args_buf = (char *)stack + 65536;
    uint64_t user_args_buf = 0x800000;

    // Copy filepath as argv[0]
    int path_len = 0;
    while (filepath[path_len]) path_len++;
    args_buf -= (path_len + 1);
    user_args_buf -= (path_len + 1);
    for (int i = 0; i <= path_len; i++) args_buf[i] = filepath[i];
    
    uint64_t argv_ptrs[32];
    argv_ptrs[0] = user_args_buf;

    if (args_str) {
        int i = 0;
        while (args_str[i] && argc < 31) {
            // Skip spaces
            while (args_str[i] == ' ') i++;
            if (!args_str[i]) break;

            int arg_start = i;
            bool in_quotes = false;
            
            if (args_str[i] == '"') {
                in_quotes = true;
                i++;
                arg_start = i;
                while (args_str[i] && args_str[i] != '"') i++;
            } else {
                while (args_str[i] && args_str[i] != ' ') i++;
            }
            
            int arg_len = i - arg_start;

            args_buf -= (arg_len + 1);
            user_args_buf -= (arg_len + 1);
            
            for (int k = 0; k < arg_len; k++) {
                args_buf[k] = args_str[arg_start + k];
            }
            args_buf[arg_len] = '\0';
            
            argv_ptrs[argc++] = user_args_buf;
            
            if (in_quotes && args_str[i] == '"') i++; // Skip closing quote
        }
    }
    argv_ptrs[argc] = 0; // Null terminator for argv

    // Align stack to 8 bytes before pushing argv array
    uint64_t current_user_sp = user_args_buf;
    current_user_sp &= ~7ULL;
    args_buf = (char *)((uint64_t)stack + (current_user_sp - (0x800000 - 65536)));

    // Push argv array
    int argv_size = (argc + 1) * sizeof(uint64_t);
    args_buf -= argv_size;
    current_user_sp -= argv_size;
    
    uint64_t actual_argv_ptr = current_user_sp; // Store the true pointer to argv array
    
    uint64_t *user_argv_array = (uint64_t *)args_buf;
    for (int i = 0; i <= argc; i++) {
        user_argv_array[i] = argv_ptrs[i];
    }
    
    // Align stack to 16 bytes. crt0.asm does `and rsp, -16`, but it's good practice
    current_user_sp &= ~15ULL;

    // 4. Build Stack Frame for context switch via IRETQ
    uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 16384);
    *(--stack_ptr) = 0x1B;            // SS (User Mode Data)
    *(--stack_ptr) = current_user_sp; // RSP (Updated user stack pointer)
    *(--stack_ptr) = 0x202;           // RFLAGS (Interrupts Enabled)
    *(--stack_ptr) = 0x23;            // CS (User Mode Code)
    *(--stack_ptr) = entry_point;     // RIP
    *(--stack_ptr) = 0;               // int_no
    *(--stack_ptr) = 0;               // err_code

    // 15 General purpose registers
    *(--stack_ptr) = 0;                // RAX
    *(--stack_ptr) = 0;                // RBX
    *(--stack_ptr) = 0;                // RCX
    *(--stack_ptr) = 0;                // RDX
    *(--stack_ptr) = 0;                // RBP
    *(--stack_ptr) = argc;             // RDI = argc
    *(--stack_ptr) = actual_argv_ptr;  // RSI = actual argv array
    *(--stack_ptr) = 0;                // R8
    *(--stack_ptr) = 0;                // R9
    *(--stack_ptr) = 0;                // R10
    *(--stack_ptr) = 0;                // R11
    *(--stack_ptr) = 0;                // R12
    *(--stack_ptr) = 0;                // R13
    *(--stack_ptr) = 0;                // R14
    *(--stack_ptr) = 0;                // R15

    new_proc->kernel_stack = (uint64_t)kernel_stack + 16384;
    new_proc->kernel_stack_alloc = kernel_stack;
    new_proc->user_stack_alloc = stack;
    new_proc->rsp = (uint64_t)stack_ptr;

    // Slot is already counted in process_count if new, or reused.

    // Add to linked list
    new_proc->next = current_process->next;
    current_process->next = new_proc;
    
    serial_write("[PROCESS] Spawned ELF Executable: ");
    serial_write(filepath);
    serial_write("\n");
    return new_proc;
}

process_t* process_get_current(void) {
    return current_process;
}

uint64_t process_schedule(uint64_t current_rsp) {
    if (!current_process || !current_process->next || current_process == current_process->next) 
        return current_rsp;
        
    // serial_write("SCHED\n");

    // Save context
    current_process->rsp = current_rsp;
    
    // Switch process
    current_process = current_process->next;
    
    // Update Kernel Stack for User Mode interrupts and System Calls
    if (current_process->is_user && current_process->kernel_stack) {
        tss_set_stack(current_process->kernel_stack);
        extern uint64_t kernel_syscall_stack;
        kernel_syscall_stack = current_process->kernel_stack;
    }
    
    // Switch page table
    paging_switch_directory(current_process->pml4_phys);
    
    return current_process->rsp;
}

uint64_t process_terminate_current(void) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    if (!current_process) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return 0;
    }
    
    // 1. Cleanup side effects
    extern Window win_cmd;
    if (current_process->ui_window && (current_process->ui_window != &win_cmd)) {
        wm_remove_window((Window *)current_process->ui_window);
        current_process->ui_window = NULL;
    }
    
    extern void fat32_close(struct FAT32_FileHandle *fh);
    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (current_process->fds[i]) {
            fat32_close(current_process->fds[i]);
            current_process->fds[i] = NULL;
        }
    }
    
    extern void cmd_process_finished(void);
    cmd_process_finished();

    // 2. Find previous process in circular list
    process_t *prev = current_process;
    while (prev->next != current_process) {
        prev = prev->next;
    }

    // 3. Remove current from list
    process_t *to_delete = current_process;
    
    if (prev == current_process) {
        // Only one process (should be kernel), cannot terminate.
        asm volatile("push %0; popfq" : : "r"(rflags));
        return to_delete->rsp;
    }

    prev->next = to_delete->next;
    current_process = to_delete->next;
    
    // Mark slot as free
    to_delete->pid = 0xFFFFFFFF; 

    // 4. Load context for the NEXT process
    if (current_process->is_user && current_process->kernel_stack) {
        tss_set_stack(current_process->kernel_stack);
        extern uint64_t kernel_syscall_stack;
        kernel_syscall_stack = current_process->kernel_stack;
    }
    
    paging_switch_directory(current_process->pml4_phys);

    // 5. Actually free the memory (after switching state to avoid issues)
    // We only safely free the user stack. Immediate freeing of the current 
    // kernel stack is unsafe while we are still running on it.
    if (to_delete->user_stack_alloc) kfree(to_delete->user_stack_alloc);
    
    // Clear pointers to avoid double-free during slot reuse
    to_delete->user_stack_alloc = NULL;
    to_delete->kernel_stack_alloc = NULL; // Leak the small kernel stack for safety
    
    uint64_t next_rsp = current_process->rsp;
    asm volatile("push %0; popfq" : : "r"(rflags));
    return next_rsp;
}

void process_push_gui_event(process_t *proc, gui_event_t *ev) {
    if (!proc) return;

    // Coalesce PAINT events: if a PAINT event is already in the queue, don't add another
    if (ev->type == 1) { // GUI_EVENT_PAINT
        int curr = proc->gui_event_head;
        while (curr != proc->gui_event_tail) {
            if (proc->gui_events[curr].type == 1) {
                return; // Already has a paint event pending
            }
            curr = (curr + 1) % MAX_GUI_EVENTS;
        }
    }

    int next_tail = (proc->gui_event_tail + 1) % MAX_GUI_EVENTS;
    // Drop event if queue is full
    if (next_tail == proc->gui_event_head) {
        extern void serial_write(const char *str);
        return;
    }
    proc->gui_events[proc->gui_event_tail] = *ev;
    proc->gui_event_tail = next_tail;
}

