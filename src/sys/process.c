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
#include "spinlock.h"
#include "smp.h"
#include "lapic.h"

extern void cmd_write(const char *str);
extern void serial_write(const char *str);

#define MAX_PROCESSES 16
#define MAX_CPUS_SCHED 32
process_t processes[MAX_PROCESSES] __attribute__((aligned(16)));
int process_count = 0;
static process_t* current_process[MAX_CPUS_SCHED] = {0}; // Per-CPU
static uint32_t next_pid = 0;
static void *free_kernel_stack_later = NULL;
static spinlock_t runqueue_lock = SPINLOCK_INIT;
static uint32_t next_cpu_assign = 1; // Round-robin CPU assignment (start from CPU 1)

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0xFFFFFFFF;
    }

    // Current kernel execution is PID 0
    process_t *kernel_proc = &processes[process_count++];
    kernel_proc->pid = next_pid++;
    kernel_proc->is_user = false;
    
    // We don't have its RSP or PML4 yet, but it's already running.
    // The timer interrupt will naturally capture its context on the first tick!
    kernel_proc->pml4_phys = paging_get_pml4_phys();
    kernel_proc->kernel_stack = 0;
    
    // Initialize FPU/SSE state for kernel (first interrupt will capture it on stack)
    kernel_proc->fpu_initialized = true;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) kernel_proc->fds[i] = NULL;
    
    extern void mem_memcpy(void *dest, const void *src, size_t len);
    mem_memcpy(kernel_proc->name, "kernel", 7);
    kernel_proc->ticks = 0;
    kernel_proc->used_memory = 32768; // Kernel stack

    kernel_proc->next = kernel_proc; // Circular linked list
    kernel_proc->cpu_affinity = 0;   // Kernel always on BSP
    current_process[0] = kernel_proc;
}

process_t* process_create(void (*entry_point)(void), bool is_user) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);
    
    if (process_count >= MAX_PROCESSES) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    process_t *new_proc = &processes[process_count++];
    new_proc->pid = next_pid++;
    new_proc->is_user = is_user;
    
    // 1. Setup Page Table
    if (is_user) {
        new_proc->pml4_phys = paging_create_user_pml4_phys();
    } else {
        new_proc->pml4_phys = paging_get_pml4_phys();
    }
    
    if (!new_proc->pml4_phys) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    // 2. Allocate aligned stack
    void* user_stack = kmalloc_aligned(4096, 4096);
    void* kernel_stack = kmalloc_aligned(32768, 32768); // Needed for when user interrupts to Ring 0
    
    if (is_user) {
        // Map user stack to 0x800000
        paging_map_page(new_proc->pml4_phys, 0x800000, v2p((uint64_t)user_stack), PT_PRESENT | PT_RW | PT_USER);
        
        // Allocate code page aligned and copy code
        void* code = kmalloc_aligned(4096, 4096);
        for(int i=0; i<128; i++) ((uint8_t*)code)[i] = ((uint8_t*)entry_point)[i];
        
        paging_map_page(new_proc->pml4_phys, 0x400000, v2p((uint64_t)code), PT_PRESENT | PT_RW | PT_USER);
        
        // Build initial stack frame for iretq
        // Stack grows down, start at top
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 32768);
        
        *(--stack_ptr) = 0x1B;          // SS (User Data)
        *(--stack_ptr) = 0x800000 + 4096; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS (IF=1)
        *(--stack_ptr) = 0x23;          // CS (User Code)
        *(--stack_ptr) = 0x400000;      // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        // Push 512 bytes for SSE/FPU state (fxsave_region)
        // Zero it out for safety
        stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
        for (int i = 0; i < 512/8; i++) stack_ptr[i] = 0;
        
        new_proc->kernel_stack = (uint64_t)kernel_stack + 32768;
        new_proc->rsp = (uint64_t)stack_ptr;
    } else {
        // Kernel thread
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 32768);
        *(--stack_ptr) = 0x10;          // SS (Kernel Data)
        stack_ptr--;
        *stack_ptr = (uint64_t)stack_ptr; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS
        *(--stack_ptr) = 0x08;          // CS (Kernel Code)
        *(--stack_ptr) = (uint64_t)entry_point; // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        // Push 512 bytes for SSE/FPU state (fxsave_region)
        stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
        // Zero it out for safety
        for (int i = 0; i < 512/8; i++) stack_ptr[i] = 0;

        new_proc->kernel_stack = (uint64_t)kernel_stack + 32768;
        new_proc->rsp = (uint64_t)stack_ptr;
        kfree(user_stack); // Unused for kernel threads
    }

    // Initialize FPU state for new process
    asm volatile("fninit");
    new_proc->fpu_initialized = true;
    
    new_proc->cpu_affinity = 0; // Non-ELF processes stay on BSP
    
    // Add to linked list
    new_proc->next = current_process[0]->next;
    current_process[0]->next = new_proc;
    
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    return new_proc;
}

process_t* process_create_elf(const char* filepath, const char* args_str) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);
    process_t *new_proc = NULL;
    
    // Find an available slot
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == 0xFFFFFFFF || i >= process_count) {
            new_proc = &processes[i];
            if (i >= process_count) process_count = i + 1;
            break;
        }
    }

    if (!new_proc) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    new_proc->pid = next_pid++;
    new_proc->is_user = true;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    
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
    size_t elf_load_size = 0;
    uint64_t entry_point = elf_load(filepath, new_proc->pml4_phys, &elf_load_size);
    if (entry_point == 0) {
        serial_write("[PROCESS] Failed to load ELF: ");
        serial_write(filepath);
        serial_write("\n");
        // We technically leak the page table here, but let's ignore cleanup for now
        return NULL;
    }

    // Set process name from filepath
    int last_slash = -1;
    for (int i = 0; filepath[i]; i++) if (filepath[i] == '/') last_slash = i;
    const char *filename = (last_slash == -1) ? filepath : (filepath + last_slash + 1);
    int ni = 0;
    while (filename[ni] && ni < 63) {
        new_proc->name[ni] = filename[ni];
        ni++;
    }
    new_proc->name[ni] = 0;
    new_proc->ticks = 0;

    // 3. Allocate generic User stack and Kernel stack for interrupts
    // Increase to 256KB to prevent stack smashing on heavy networking
    size_t user_stack_size = 262144;
    void* stack = kmalloc_aligned(user_stack_size, 4096);
    void* kernel_stack = kmalloc_aligned(65536, 65536); 
    
    // Map User stack to 0x800000
    for (uint64_t i = 0; i < (user_stack_size / 4096); i++) {
        paging_map_page(new_proc->pml4_phys, 0x800000 - user_stack_size + (i * 4096), v2p((uint64_t)stack + (i * 4096)), PT_PRESENT | PT_RW | PT_USER);
    }

 
    int argc = 1;
    char *args_buf = (char *)stack + user_stack_size;
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
    args_buf = (char *)((uint64_t)stack + (current_user_sp - (0x800000 - user_stack_size)));

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
    uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 65536);
    *(--stack_ptr) = 0x1B;            // SS (User Mode Data)
    *(--stack_ptr) = current_user_sp; // RSP (Updated user stack pointer)
    *(--stack_ptr) = 0x202;           // RFLAGS (Interrupts Enabled)
    *(--stack_ptr) = 0x23;            // CS (User Mode Code)
    *(--stack_ptr) = entry_point;     // RIP
    *(--stack_ptr) = 0;               // err_code
    *(--stack_ptr) = 0;               // int_no
    // 15 General purpose registers
    *(--stack_ptr) = 0;                // RAX
    *(--stack_ptr) = 0;                // RBX
    *(--stack_ptr) = 0;                // RCX
    *(--stack_ptr) = 0;                // RDX
    *(--stack_ptr) = actual_argv_ptr;  // RSI = actual argv array
    *(--stack_ptr) = argc;             // RDI = argc
    *(--stack_ptr) = 0;                // RBP
    *(--stack_ptr) = 0;                // R8
    *(--stack_ptr) = 0;                // R9
    *(--stack_ptr) = 0;                // R10
    *(--stack_ptr) = 0;                // R11
    *(--stack_ptr) = 0;                // R12
    *(--stack_ptr) = 0;                // R13
    *(--stack_ptr) = 0;                // R14
    *(--stack_ptr) = 0;                // R15
    
    // Space for 512-byte fxsave_region
    stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
    // Initialize with a clean FPU state
    asm volatile("fninit");
    asm volatile("fxsave %0" : "=m"(*stack_ptr));

    new_proc->kernel_stack = (uint64_t)kernel_stack + 65536;
    new_proc->kernel_stack_alloc = kernel_stack;
    new_proc->user_stack_alloc = stack;
    new_proc->rsp = (uint64_t)stack_ptr;
    new_proc->used_memory = elf_load_size + user_stack_size + 65536;

    // Initialize FPU state for new process
    asm volatile("fninit");
    new_proc->fpu_initialized = true;

    // Assign to an AP core via round-robin (if SMP is active)
    uint32_t cpu_count = smp_cpu_count();
    if (cpu_count > 1) {
        new_proc->cpu_affinity = next_cpu_assign;
        next_cpu_assign++;
        if (next_cpu_assign >= cpu_count) next_cpu_assign = 1; // Wrap, skip CPU 0
    } else {
        new_proc->cpu_affinity = 0;
    }

    // Add to linked list (Critical Section)
    rflags = spinlock_acquire_irqsave(&runqueue_lock);
    new_proc->next = current_process[0]->next;
    current_process[0]->next = new_proc;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    
    serial_write("[PROCESS] Spawned ELF Executable: ");
    serial_write(filepath);
    serial_write("\n");
    return new_proc;
}

process_t* process_get_current_for_cpu(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS_SCHED) return NULL;
    return current_process[cpu_id];
}

void process_set_current_for_cpu(uint32_t cpu_id, process_t* p) {
    if (cpu_id >= MAX_CPUS_SCHED) return;
    current_process[cpu_id] = p;
}

process_t* process_get_current(void) {
    uint32_t cpu = smp_this_cpu_id();
    return current_process[cpu];
}

uint64_t process_schedule(uint64_t current_rsp) {
    if (free_kernel_stack_later) {
        kfree(free_kernel_stack_later);
        free_kernel_stack_later = NULL;
    }

    uint32_t my_cpu = smp_this_cpu_id();
    process_t *cur = current_process[my_cpu];
    
    if (!cur || !cur->next || cur == cur->next) 
        return current_rsp;
        
    // Save context
    cur->rsp = current_rsp;

    // Switch to next ready process assigned to this CPU
    extern uint32_t wm_get_ticks(void);
    uint32_t now = wm_get_ticks();
    
    process_t *start = cur;
    process_t *next_proc = cur->next;
    
    while (next_proc != start) {
        // Only consider processes assigned to our CPU and not terminated
        if (next_proc->cpu_affinity == my_cpu && next_proc->pid != 0xFFFFFFFF) {
            if (next_proc->pid == 0 || next_proc->sleep_until == 0 || next_proc->sleep_until <= now) {
                break;
            }
        }
        next_proc = next_proc->next;
    }
    
    // If we didn't find a ready process for our CPU, stay on current (unless we are terminated)
    if (next_proc->cpu_affinity != my_cpu || next_proc->pid == 0xFFFFFFFF) {
        // Fallback to idle if current is terminated
        if (cur && cur->pid == 0xFFFFFFFF) {
            // Find the idle process for this CPU
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (processes[i].pid == 0 || (processes[i].cpu_affinity == my_cpu && processes[i].is_user == false)) {
                    next_proc = &processes[i];
                    break;
                }
            }
        } else {
            return current_rsp;
        }
    }
    
    current_process[my_cpu] = next_proc;
    
    // Update Kernel Stack for User Mode interrupts and System Calls
    if (current_process[my_cpu]->is_user && current_process[my_cpu]->kernel_stack) {
        tss_set_stack_cpu(my_cpu, current_process[my_cpu]->kernel_stack);
        if (my_cpu == 0) {
            extern uint64_t kernel_syscall_stack;
            kernel_syscall_stack = current_process[my_cpu]->kernel_stack;
        }
    }
    
    // Switch page table
    paging_switch_directory(current_process[my_cpu]->pml4_phys);
    
    current_process[my_cpu]->ticks++;

    return current_process[my_cpu]->rsp;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}

static void process_cleanup_inner(process_t *proc) {
    if (!proc || proc->pid == 0xFFFFFFFF) return;

    // 1. Cleanup side effects
    extern Window win_cmd;
    if (proc->ui_window && (proc->ui_window != &win_cmd)) {
        wm_remove_window((Window *)proc->ui_window);
        proc->ui_window = NULL;
    }
    
    extern void fat32_close(struct FAT32_FileHandle *fh);
    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (proc->fds[i]) {
            fat32_close(proc->fds[i]);
            proc->fds[i] = NULL;
        }
    }
    
    extern void cmd_process_finished(void);
    cmd_process_finished();
    
    extern void network_cleanup(void);
    network_cleanup();
    
    extern void network_cleanup_pcb(void *pcb);
    // TODO: We need per-process PCB tracking to call this safely
    // For now, let's NOT call global network_cleanup
}

void process_terminate(process_t *to_delete) {
    if (!to_delete || to_delete->pid == 0xFFFFFFFF || to_delete->pid == 0) return;

    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);

    process_cleanup_inner(to_delete);

    // 2. Find previous process in circular list
    process_t *prev = to_delete;
    while (prev->next != to_delete) {
        prev = prev->next;
    }

    if (prev == to_delete) {
        // Only one process (should be kernel), cannot terminate.
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return;
    }

    // 3. Remove current from list
    prev->next = to_delete->next;
    
    // Update per-CPU current_process if this was the current on any CPU
    uint32_t cpu_count = smp_cpu_count();
    for (uint32_t c = 0; c < cpu_count && c < MAX_CPUS_SCHED; c++) {
        if (current_process[c] == to_delete) {
            process_t *np = to_delete->next;
            while (np != to_delete) {
                if (np->cpu_affinity == c && np->pid != 0xFFFFFFFF) break;
                np = np->next;
            }
            if (np == to_delete || np->cpu_affinity != c) {
                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processes[i].pid == 0 || (processes[i].cpu_affinity == c && processes[i].is_user == false)) {
                        np = &processes[i]; break;
                    }
                }
            }
            current_process[c] = np;
        }
    }

    // Mark slot as free
    to_delete->pid = 0xFFFFFFFF; 
    to_delete->cpu_affinity = 0xFFFFFFFF;

    if (to_delete->user_stack_alloc) kfree(to_delete->user_stack_alloc);
    if (to_delete->kernel_stack_alloc) {
        kfree(to_delete->kernel_stack_alloc);
    }
    
    extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
    if (to_delete->pml4_phys && to_delete->is_user) {
        paging_destroy_user_pml4_phys(to_delete->pml4_phys);
    }
    
    to_delete->user_stack_alloc = NULL;
    to_delete->kernel_stack_alloc = NULL;
    to_delete->pml4_phys = 0;

    spinlock_release_irqrestore(&runqueue_lock, rflags);
}

uint64_t process_terminate_current(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);

    uint32_t my_cpu = smp_this_cpu_id();
    process_t *cur = current_process[my_cpu];

    if (!cur || cur->pid == 0) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return 0;
    }
    
    process_cleanup_inner(cur);

    // 2. Find previous process in circular list
    process_t *prev = cur;
    while (prev->next != cur) {
        prev = prev->next;
    }

    // 3. Remove current from list
    process_t *to_delete = cur;
    
    if (prev == cur) {
        // Only one process (should be kernel), cannot terminate.
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return to_delete->rsp;
    }

    prev->next = to_delete->next;
    
    process_t *next_proc = to_delete->next;
    while (next_proc != to_delete) {
        if (next_proc->cpu_affinity == my_cpu && next_proc->pid != 0xFFFFFFFF) break;
        next_proc = next_proc->next;
    }
    
    if (next_proc == to_delete || next_proc->cpu_affinity != my_cpu) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == 0 || (processes[i].cpu_affinity == my_cpu && processes[i].is_user == false)) {
                next_proc = &processes[i]; break;
            }
        }
    }
    
    current_process[my_cpu] = next_proc;
    
    // Mark slot as free
    to_delete->pid = 0xFFFFFFFF; 
    to_delete->cpu_affinity = 0xFFFFFFFF;

    // 4. Load context for the NEXT process
    if (current_process[my_cpu]->is_user && current_process[my_cpu]->kernel_stack) {
        tss_set_stack_cpu(my_cpu, current_process[my_cpu]->kernel_stack);
        if (my_cpu == 0) {
            extern uint64_t kernel_syscall_stack;
            kernel_syscall_stack = current_process[my_cpu]->kernel_stack;
        }
    }
    
    paging_switch_directory(current_process[my_cpu]->pml4_phys);

    // 5. Free memory
    if (to_delete->user_stack_alloc) kfree(to_delete->user_stack_alloc);
    
    extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
    if (to_delete->pml4_phys && to_delete->is_user) {
        paging_destroy_user_pml4_phys(to_delete->pml4_phys);
    }
    
    to_delete->user_stack_alloc = NULL;
    free_kernel_stack_later = to_delete->kernel_stack_alloc;
    to_delete->kernel_stack_alloc = NULL;
    to_delete->pml4_phys = 0;
    
    uint64_t next_rsp = current_process[my_cpu]->rsp;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    return next_rsp;
}

// SMP: IPI handler called on AP cores when BSP broadcasts scheduling IPI
uint64_t sched_ipi_handler(registers_t *regs) {
    lapic_eoi(); // Acknowledge the IPI
    
    // Run the scheduler for this CPU
    return process_schedule((uint64_t)regs);
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

process_t* process_get_by_ui_window(void *win) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid != 0xFFFFFFFF && processes[i].ui_window == win) {
            return &processes[i];
        }
    }
    return NULL;
}

