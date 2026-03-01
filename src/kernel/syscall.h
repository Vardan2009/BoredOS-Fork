// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// Forward declarations
typedef struct Window Window;
typedef struct registers_t registers_t;

// MSRs used for syscalls in x86_64
#define MSR_EFER       0xC0000080
#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_COMPAT_STAR 0xC0000083
#define MSR_FMASK      0xC0000084

// Syscall Numbers
#define SYS_WRITE 1
#define SYS_GUI   3
#define SYS_FS    4
#define SYS_EXIT  60

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
#define FS_CMD_GETCWD 12
#define FS_CMD_CHDIR 13

void syscall_init(void);
uint64_t syscall_handler_c(registers_t *regs);

// Mouse event helpers for WM
void syscall_send_mouse_move_event(Window *win, int x, int y, uint8_t buttons);
void syscall_send_mouse_down_event(Window *win, int x, int y);
void syscall_send_mouse_up_event(Window *win, int x, int y);

#endif // SYSCALL_H
