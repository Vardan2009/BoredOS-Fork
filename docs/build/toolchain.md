# Build Toolchain

BoredOS is built cross-compiled from a host system (such as macOS or Linux) to target the generic `x86_64-elf` platform.

## Prerequisites

To build BoredOS, you need the following tools:

1.  **x86_64 ELF GCC Cross-Compiler**:
    -   `x86_64-elf-gcc`: The C compiler targeting the freestanding overarching ELF environment.
    -   `x86_64-elf-ld`: The linker to combine object files into the final `boredos.elf` kernel binary and userland variables.

2.  **NASM**:
    -   Required to compile the `.asm` files in `src/arch/` and `src/userland/crt0.asm`. It formats the output as `elf64` objects to be linked alongside the C code.

3.  **xorriso**:
    -   A specialized tool to create ISO 9660 filesystem images.
    -   *Why?* `xorriso` packages the compiled kernel, Limine bootloader, and asset files (fonts, images, userland binaries) into the final bootable `boredos.iso` CD-ROM image.

4.  **QEMU** (Optional but highly recommended for testing):
    -   `qemu-system-x86_64` is used to virtualize the OS for testing or to mess around.
