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

void process_create_elf(const char* filepath) {
    if (process_count >= MAX_PROCESSES) return;

    process_t *new_proc = &processes[process_count];
    new_proc->pid = next_pid++;
    new_proc->is_user = true;
    
    // 1. Setup Page Table
    new_proc->pml4_phys = paging_create_user_pml4_phys();
    if (!new_proc->pml4_phys) return;

    // 2. Load ELF executable
    uint64_t entry_point = elf_load(filepath, new_proc->pml4_phys);
    if (entry_point == 0) {
        serial_write("[PROCESS] Failed to load ELF: ");
        serial_write(filepath);
        serial_write("\n");
        // We technically leak the page table here, but let's ignore cleanup for now
        return;
    }

    // 3. Allocate generic User stack and Kernel stack for interrupts
    void* stack = kmalloc_aligned(4096, 4096);
    void* kernel_stack = kmalloc_aligned(16384, 16384); 
    
    // Map User stack to 0x800000 -> Note: ELFs might overwrite this if they load there!
    // But our ELF loader defaults 0x400000 for standard code.
    paging_map_page(new_proc->pml4_phys, 0x800000, v2p((uint64_t)stack), PT_PRESENT | PT_RW | PT_USER);

    // 4. Build Stack Frame
    uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 16384);
    *(--stack_ptr) = 0x1B;            // SS (User Mode Data)
    *(--stack_ptr) = 0x800000 + 4096;   // RSP 
    *(--stack_ptr) = 0x202;           // RFLAGS (Interrupts Enabled)
    *(--stack_ptr) = 0x23;            // CS (User Mode Code)
    *(--stack_ptr) = entry_point;       // RIP
    *(--stack_ptr) = 0;               // int_no
    *(--stack_ptr) = 0;               // err_code

    // 15 General purpose registers
    for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;

    new_proc->kernel_stack = (uint64_t)kernel_stack + 16384;
    new_proc->rsp = (uint64_t)stack_ptr;

    // We only increment process_count after success
    process_count++;

    // Add to linked list
    new_proc->next = current_process->next;
    current_process->next = new_proc;
    
    serial_write("[PROCESS] Spawned ELF Executable: ");
    serial_write(filepath);
    serial_write("\n");
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
    if (!current_process) return 0;
    
    // 1. Cleanup side effects
    if (current_process->ui_window) {
        wm_remove_window((Window *)current_process->ui_window);
        current_process->ui_window = NULL;
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
        return to_delete->rsp;
    }

    prev->next = to_delete->next;
    current_process = to_delete->next;
    
    // Mark slot as freeish (simple version)
    to_delete->pid = 0xFFFFFFFF; 

    // 4. Load context for the NEXT process
    if (current_process->is_user && current_process->kernel_stack) {
        tss_set_stack(current_process->kernel_stack);
        extern uint64_t kernel_syscall_stack;
        kernel_syscall_stack = current_process->kernel_stack;
    }
    
    paging_switch_directory(current_process->pml4_phys);

    return current_process->rsp;
}

void process_push_gui_event(process_t *proc, gui_event_t *ev) {
    if (!proc) return;
    int next_tail = (proc->gui_event_tail + 1) % MAX_GUI_EVENTS;
    // Drop event if queue is full
    if (next_tail == proc->gui_event_head) {
        return;
    }
    proc->gui_events[proc->gui_event_tail] = *ev;
    proc->gui_event_tail = next_tail;
}

