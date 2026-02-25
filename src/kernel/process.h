#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>

// Registers saved on the stack by interrupts/exceptions
typedef struct {
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
    struct process *next;
} process_t;

void process_init(void);
void process_create(void* entry_point, bool is_user);
process_t* process_get_current(void);
uint64_t process_schedule(uint64_t current_rsp);

#endif

