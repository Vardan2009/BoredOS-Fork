# Userland SDK Reference

BoredOS provides a custom `libc` implementation necessary for writing userland applications (`.elf` binaries). By avoiding a full-blown standard library like `glibc`, the OS ensures a minimal executable footprint tailored strictly to the existing kernel features.

## The Custom libc Structure (`src/userland/libc/`)

The SDK comprises a few key files containing wrappers around kernel system calls:

-   `stdlib.h` / `stdlib.c`: Memory allocation (`malloc`, `free`), integer conversion (`itoa`, `atoi`), printing (`printf`, `sprintf`), and random numbers (`rand`, `srand`).
-   `string.h` / `string.c`: String manipulation utilities (`strlen`, `strcpy`, `strcmp`, `memset`, `memcpy`).
-   `syscall.h` / `syscall.c`: The raw interface to issue `syscall` assembly instructions, routing requests to the kernel.
-   `libui.h` / `libui.c`: Graphical interface commands (creating windows, drawing pixels, events).

## System Calls Overview

When a userland application wants to interact with the hardware (print to screen, read a file, create a window), it must ask the kernel via a **System Call**.

In BoredOS (`x86_64`), system calls are issued using the `syscall` instruction. The kernel intercepts this instruction and inspects the processor's RAX register to figure out *what* the application wants to do.

The custom `libc` provides `syscallX` wrapper functions that abstract the assembly details:
```c
// Example: Performing a minimal system call from userland
int sys_write(int fd, const char *buf, int len) {
    return syscall3(SYS_WRITE, fd, (uint64_t)buf, len);
}
```

### Notable System Calls

-   **`SYS_WRITE` (1)**: Currently acts as a generic output mechanism for `printf`, typically routing text to the kernel's serial output for debugging, or to an active text-mode console.
-   **`SYS_GUI` (3)**: The primary multiplexer for all window manager operations. The arguments define subcommands (like `UI_CREATE_WINDOW`, `UI_FILL_RECT`).
-   **`SYS_FS` (4)**: Interacts with the virtual filesystem (e.g., `FS_CMD_OPEN`, `FS_CMD_READ`). Under the hood, this reads from the loaded RAMFS or an attached physical ATA disk via the native FAT32 driver.
-   **`SYS_EXIT` (60)**: Terminates the current process and returns control to the kernel.
-   **`SYSTEM_CMD_YIELD` (43)**: Instructs the process scheduler to pause the current process and let another process run.

If you are developing a new application, **do not invoke syscalls manually**. Instead, include `stdlib.h` and use the C functions provided.
