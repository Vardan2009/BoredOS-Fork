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

void *sys_sbrk(int incr) {
    return (void *)syscall1(SYS_SBRK, (uint64_t)incr);
}

int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    return (int)syscall5(SYS_SYSTEM, (uint64_t)cmd, arg1, arg2, arg3, arg4);
}

int sys_open(const char *path, const char *mode) {
    return (int)syscall3(SYS_FS, FS_CMD_OPEN, (uint64_t)path, (uint64_t)mode);
}

int sys_read(int fd, void *buf, uint32_t len) {
    return (int)syscall4(SYS_FS, FS_CMD_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

int sys_write_fs(int fd, const void *buf, uint32_t len) {
    return (int)syscall4(SYS_FS, FS_CMD_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

void sys_close(int fd) {
    syscall2(SYS_FS, FS_CMD_CLOSE, (uint64_t)fd);
}

int sys_seek(int fd, int offset, int whence) {
    return (int)syscall4(SYS_FS, FS_CMD_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

uint32_t sys_tell(int fd) {
    return (uint32_t)syscall2(SYS_FS, FS_CMD_TELL, (uint64_t)fd);
}

uint32_t sys_size(int fd) {
    return (uint32_t)syscall2(SYS_FS, FS_CMD_SIZE, (uint64_t)fd);
}

int sys_list(const char *path, struct FAT32_FileInfo *entries, int max_entries) {
    return (int)syscall4(SYS_FS, FS_CMD_LIST, (uint64_t)path, (uint64_t)entries, (uint64_t)max_entries);
}

int sys_delete(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_DELETE, (uint64_t)path);
}

int sys_mkdir(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_MKDIR, (uint64_t)path);
}

int sys_exists(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_EXISTS, (uint64_t)path);
}

