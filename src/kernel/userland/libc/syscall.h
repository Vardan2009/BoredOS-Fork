#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// Standard syscalls available from Kernel mode
#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_GUI   3
#define SYS_FS    4
#define SYS_SYSTEM 5
#define SYS_SBRK  9

// FS Commands
#define FS_CMD_OPEN 1
#define FS_CMD_READ 2
#define FS_CMD_WRITE 3
#define FS_CMD_CLOSE 4
#define FS_CMD_SEEK 5
#define FS_CMD_TELL 6
#define FS_CMD_LIST 7
#define FS_CMD_DELETE 8
#define FS_CMD_SIZE 9
#define FS_CMD_MKDIR 10
#define FS_CMD_EXISTS 11

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
void *sys_sbrk(int incr);
int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

// FS API
int sys_open(const char *path, const char *mode);
int sys_read(int fd, void *buf, uint32_t len);
int sys_write_fs(int fd, const void *buf, uint32_t len);
void sys_close(int fd);
int sys_seek(int fd, int offset, int whence);
uint32_t sys_tell(int fd);
uint32_t sys_size(int fd);
int sys_delete(const char *path);
int sys_mkdir(const char *path);
int sys_exists(const char *path);

struct FAT32_FileInfo;
int sys_list(const char *path, struct FAT32_FileInfo *entries, int max_entries);

#endif
