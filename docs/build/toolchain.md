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
    -   `qemu-system-x86_64` is used for rapid emulation and testing.

## Installation (macOS)

You can easily install the complete toolchain using Homebrew:

```sh
brew install x86_64-elf-binutils x86_64-elf-gcc nasm xorriso qemu
```

## Installation (Linux)

Depending on your distribution, the installation commands vary. Note that some distributions may require you to build the `x86_64-elf` cross-compiler from source if it isn't available in their default repositories.

### Debian / Ubuntu
```sh
sudo apt update
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo nasm xorriso qemu-system-x86
```
*(Note: You will need to build the `x86_64-elf` cross-compiler from source or find a compatible PPA, as it is not in the default Debian/Ubuntu repositories.)*

### Arch Linux
Arch Linux provides the regular tools in its standard repositories and the cross-compiler via the AUR:
```sh
sudo pacman -S nasm xorriso qemu-full
yay -S x86_64-elf-gcc x86_64-elf-binutils
```

### Fedora
```sh
sudo dnf install make gcc gcc-c++ bison flex gmp-devel mpfr-devel libmpc-devel texinfo nasm xorriso qemu
```
