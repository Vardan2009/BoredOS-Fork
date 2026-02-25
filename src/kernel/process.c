#include "process.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "io.h"
#include "platform.h"
#include "memory_manager.h"

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
    void* kernel_stack = kmalloc_aligned(4096, 4096); // Needed for when user interrupts to Ring 0
    
    if (is_user) {
        // Map user stack to 0x800000
        paging_map_page(new_proc->pml4_phys, 0x800000, v2p((uint64_t)stack), PT_PRESENT | PT_RW | PT_USER);
        
        // Allocate code page aligned and copy code
        void* code = kmalloc_aligned(4096, 4096);
        for(int i=0; i<128; i++) ((uint8_t*)code)[i] = ((uint8_t*)entry_point)[i];
        
        paging_map_page(new_proc->pml4_phys, 0x400000, v2p((uint64_t)code), PT_PRESENT | PT_RW | PT_USER);
        
        // Build initial stack frame for iretq
        // Stack grows down, start at top
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 4096);
        
        *(--stack_ptr) = 0x1B;          // SS (User Data)
        *(--stack_ptr) = 0x800000 + 4096; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS (IF=1)
        *(--stack_ptr) = 0x23;          // CS (User Code)
        *(--stack_ptr) = 0x400000;      // RIP
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        new_proc->kernel_stack = (uint64_t)kernel_stack + 4096;
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
        
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        new_proc->kernel_stack = 0;
        new_proc->rsp = (uint64_t)stack_ptr;
    }
    
    // Add to linked list
    new_proc->next = current_process->next;
    current_process->next = new_proc;
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
    
    // Update Kernel Stack for User Mode interrupts
    if (current_process->is_user && current_process->kernel_stack) {
        tss_set_stack(current_process->kernel_stack);
    }
    
    // Switch page table
    paging_switch_directory(current_process->pml4_phys);
    
    return current_process->rsp;
}

