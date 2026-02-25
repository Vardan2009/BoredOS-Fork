#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// Standard syscalls available from Kernel mode
#define SYS_EXIT  0
#define SYS_WRITE 1

// Internal assembly entry into Ring 0
extern uint64_t syscall0(uint64_t sys_num);
extern uint64_t syscall1(uint64_t sys_num, uint64_t arg1);
extern uint64_t syscall2(uint64_t sys_num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

// Public API
void sys_exit(int status);
int sys_write(int fd, const char *buf, int len);

#endif
