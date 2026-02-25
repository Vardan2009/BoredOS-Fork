#include "syscall.h"
#include "gdt.h"
#include "memory_manager.h"

// Read MSR
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// Implemented in assembly
extern void syscall_entry(void);

extern uint64_t kernel_syscall_stack;

void syscall_init(void) {
    void* stack = kmalloc(16384);
    kernel_syscall_stack = (uint64_t)stack + 16384;
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1; // SCE bit is bit 0
    wrmsr(MSR_EFER, efer);

    // STAR MSR setup:
    // Bits 32-47: Syscall CS and SS. CS = STAR[47:32], SS = STAR[47:32] + 8 (Kernel CS = 0x08)
    // Bits 48-63: Sysret CS and SS. CS = STAR[63:48] + 16, SS = STAR[63:48] + 8.
    // User Data must be Base+8, User Code must be Base+16.
    // Our GDT: User Data = 0x1B, User Code = 0x23. 
    // Therefore Base = 0x13.
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // Mask Interrupts on SYSCALL (Clear IF bit in RFLAGS during syscall execution)
    wrmsr(MSR_FMASK, 0x200);
}

void syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    extern void cmd_write(const char *str);
    extern void serial_write(const char *str);
    
    if (syscall_num == 1) { // SYS_WRITE
        // arg2 is the buffer based on our user_test logic
        cmd_write((const char*)arg2);
        serial_write((const char*)arg2);
    }
}
