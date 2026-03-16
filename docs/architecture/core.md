# Core Architecture

BoredOS is a 64-bit hobbyist operating system designed for the x86_64 architecture. While it features kernel-space drivers and a built-in window manager, it supports fully-isolated userspace applications and includes a networking stack.

This document serves as an overview of the core architecture and the layout of the kernel source code.

## Source Code Layout (`src/`)

The OS heavily relies on module separation. The `src/` directory is logically split into several domains:

- **`arch/`**: Contains the assembly routines needed for bootstrapping the system (`boot.asm`) and setting up the CPU state for userland execution (`process_asm.asm`). It also handles architecture-specific mechanisms like the Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT).
- **`core/`**: The initialization sequence of the OS lives here. `main.c` is the entry point from the bootloader. This directory also contains essential kernel utilities (`kutils.c`), panic handlers (`panic.c`), and built-in command parsing logic (`cmd.c`).
- **`dev/`**: Device drivers. This includes the PCI scanner, disk management infrastructure, input drivers (keyboard and mouse), and the Real Time Clock (RTC).
- **`fs/`**: Filesystem implementations. The system uses a Virtual File System (VFS) abstraction alongside an in-memory FAT32 filesystem with support for drives over ATA that are formatted as FAT32 (plain/MBR).
- **`mem/`**: Physical and virtual memory management. It controls page frame allocation, paging, and kernel heap operations.
- **`net/`**: The networking stack. BoredOS relies on `lwIP` for processing IPv4 and TCP/UDP traffic, interacting with a range of NICs via `net/nic/`.
- **`sys/`**: System calls and process management. The ELF loader resides here, parsing userland binaries and setting them up for execution.
- **`wm/`**: The graphical subsystem. It handles drawing primitives, window structures, font rendering, and double-buffering.
- **`userland/`**: Out-of-kernel components. This includes the custom SDK/compiler environment (`libc/`) and user applications (`cli/`, `gui/`, `games/`).

## Boot Process

BoredOS uses **Limine** as its primary bootloader.

1.  **Limine Initialization**: The machine firmware (BIOS or UEFI) loads Limine. Limine parses `limine.conf`, sets up an early graphical framebuffer, and reads the kernel ELF file into memory.
2.  **Multiboot2 Protocol**: The kernel expects the Limine boot protocol (which is compatible with modern Multiboot specifications). Passing a framebuffer and memory map is handled natively by Limine's request structures (defined locally via `limine.h`).
3.  **Kernel Entry (`main.c`)**: The entry point `_start` is called. It immediately initializes the serial port for debugging, sets up core structures (GDT/IDT), initializes the physical memory manager based on the Limine memory map, and starts the virtual memory manager.
4.  **Driver Initialization**: PCI buses are scanned, finding the network card or disk controllers. The filesystem is mounted.
5.  **Window Manager**: The UI is drawn on top of the Limine-provided framebuffer.

## Userland Transition

The OS supports privilege separation (Ring 0 vs. Ring 3). When an application (like `browser.elf` or `viewer.elf`) is launched, the kernel:
1.  Loads the ELF file from the filesystem using the ELF parser in `sys/elf.c`.
2.  Allocates a new virtual address space (Page Directory) for the process.
3.  Maps the executable segments according to the ELF headers.
4.  Switches to User Mode (Ring 3) via the `iretq` instruction, jumping into the application's entry point (`crt0.asm`).

Programs then interact with the core kernel using system calls (`syscall.c`).
