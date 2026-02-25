#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// MSRs used for syscalls in x86_64
#define MSR_EFER       0xC0000080
#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_COMPAT_STAR 0xC0000083
#define MSR_FMASK      0xC0000084

// Syscall Numbers
#define SYS_WRITE 1
#define SYS_EXIT  60

void syscall_init(void);
void syscall_handler_c(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif // SYSCALL_H
