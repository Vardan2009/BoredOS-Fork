#include "syscall.h"



uint64_t syscall0(uint64_t sys_num) {
    uint64_t ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall1(uint64_t sys_num, uint64_t arg1) {
    uint64_t ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall2(uint64_t sys_num, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3)
                 : "rcx", "r11", "memory", "r10", "r8", "r9");
    return ret;
}

uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
                 : "rcx", "r11", "memory", "r8", "r9");
    return ret;
}

uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8  asm("r8") = arg5;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
                 : "rcx", "r11", "memory", "r9");
    return ret;
}

// C-Friendly Wrappers

void sys_exit(int status) {
    syscall1(SYS_EXIT, (uint64_t)status);
    while (1); // Halt
}

int sys_write(int fd, const char *buf, int len) {
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}
