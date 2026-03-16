# BoredOS Makefile
# Target Architecture: x86_64
# Host: macOS
# Copyright (c) 2023-2026 Chris (boreddevnl)
# This software is released under the GNU General Public License v3.0. See LICENSE file for details.
# This header needs to maintain in any file it is present in, as per the GPL license terms.

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
NASM = nasm
XORRISO = xorriso

SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso_root

KERNEL_ELF = $(BUILD_DIR)/boredos.elf
ISO_IMAGE = boredos.iso

C_SOURCES = $(wildcard $(SRC_DIR)/core/*.c) \
            $(wildcard $(SRC_DIR)/sys/*.c) \
            $(wildcard $(SRC_DIR)/mem/*.c) \
            $(wildcard $(SRC_DIR)/dev/*.c) \
            $(wildcard $(SRC_DIR)/net/*.c) \
            $(wildcard $(SRC_DIR)/net/nic/*.c) \
            $(wildcard $(SRC_DIR)/fs/*.c) \
            $(wildcard $(SRC_DIR)/wm/*.c) \
            $(wildcard $(SRC_DIR)/net/lwip/core/*.c) \
            $(wildcard $(SRC_DIR)/net/lwip/core/ipv4/*.c) \
			$(SRC_DIR)/net/lwip/netif/ethernet.c \
			$(SRC_DIR)/net/lwip/netif/bridgeif.c

ASM_SOURCES = $(wildcard $(SRC_DIR)/arch/*.asm)
OBJ_FILES = $(patsubst $(SRC_DIR)/core/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/core/*.c)) \
            $(patsubst $(SRC_DIR)/sys/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/sys/*.c)) \
            $(patsubst $(SRC_DIR)/mem/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/mem/*.c)) \
            $(patsubst $(SRC_DIR)/dev/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/dev/*.c)) \
            $(patsubst $(SRC_DIR)/net/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/net/*.c)) \
            $(patsubst $(SRC_DIR)/net/nic/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/net/nic/*.c)) \
            $(patsubst $(SRC_DIR)/fs/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/fs/*.c)) \
            $(patsubst $(SRC_DIR)/wm/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/wm/*.c)) \
            $(patsubst $(SRC_DIR)/net/lwip/%.c, $(BUILD_DIR)/lwip/%.o, $(filter $(SRC_DIR)/net/lwip/%.c, $(C_SOURCES))) \
            $(patsubst $(SRC_DIR)/arch/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

CFLAGS = -g -O2 -pipe -Wall -Wextra -std=gnu11 -ffreestanding \
         -fno-stack-protector -fno-stack-check -fno-lto -fPIE \
         -m64 -march=x86-64 -msse -msse2 -mstackrealign -mno-red-zone \
         -I$(SRC_DIR) -I$(SRC_DIR)/net/lwip -I$(SRC_DIR)/core -I$(SRC_DIR)/sys -I$(SRC_DIR)/mem -I$(SRC_DIR)/dev -I$(SRC_DIR)/net -I$(SRC_DIR)/net/nic -I$(SRC_DIR)/fs -I$(SRC_DIR)/wm

LDFLAGS = -m elf_x86_64 -nostdlib -static -pie --no-dynamic-linker \
          -z text -z max-page-size=0x1000 -T linker.ld

NASMFLAGS = -f elf64

LIMINE_VERSION = 10.8.2
LIMINE_URL_BASE = https://github.com/limine-bootloader/limine/raw/v$(LIMINE_VERSION)

.PHONY: all clean run limine-setup

all: $(ISO_IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

limine-setup:
	@if [ ! -f limine/limine-bios.sys ]; then \
		echo "Limine binaries missing or invalid. Cloning v$(LIMINE_VERSION)-binary..."; \
		rm -rf limine; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v$(LIMINE_VERSION)-binary --depth=1 limine; \
	fi
	@if [ ! -f $(SRC_DIR)/core/limine.h ]; then \
		echo "Copying limine.h..."; \
		cp limine/limine.h $(SRC_DIR)/core/limine.h; \
	fi
	@echo "Building Limine host utility..."; \
	$(MAKE) -C limine

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/core/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/sys/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/mem/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/dev/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/nic/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/fs/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/wm/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lwip/%.o: $(SRC_DIR)/net/lwip/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/arch/%.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/test_syscall.o: $(SRC_DIR)/arch/test_syscall.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/user_test.o: $(SRC_DIR)/arch/user_test.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/process_asm.o: $(SRC_DIR)/arch/process_asm.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)
	$(MAKE) -C $(SRC_DIR)/userland

$(ISO_IMAGE): $(KERNEL_ELF) limine.conf limine-setup
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	
	cp $(KERNEL_ELF) $(ISO_DIR)/
	cp limine.conf $(ISO_DIR)/
	mkdir -p $(ISO_DIR)/bin
	@for f in $(SRC_DIR)/userland/bin/*.elf; do \
		if [ -f "$$f" ]; then \
			basename=$$(basename "$$f"); \
			cp "$$f" $(ISO_DIR)/bin/; \
			echo "    module_path: boot():/bin/$$basename" >> $(ISO_DIR)/limine.conf; \
		fi \
	done
	
	@if [ -f README.md ]; then cp README.md $(ISO_DIR)/; fi
	@if [ -f $(SRC_DIR)/userland/games/doom/doom1.wad ]; then \
		mkdir -p $(ISO_DIR)/Library/DOOM; \
		cp $(SRC_DIR)/userland/games/doom/doom1.wad $(ISO_DIR)/Library/DOOM/; \
		echo "    module_path: boot():/Library/DOOM/doom1.wad" >> $(ISO_DIR)/limine.conf; \
	fi
	
	mkdir -p $(ISO_DIR)/Library/images/Wallpapers
	@for f in $(SRC_DIR)/images/wallpapers/*; do \
		if [ -f "$$f" ]; then \
			basename=$$(basename "$$f"); \
			cp "$$f" $(ISO_DIR)/Library/images/Wallpapers/; \
			echo "    module_path: boot():/Library/images/Wallpapers/$$basename" >> $(ISO_DIR)/limine.conf; \
		fi \
	done
	@if [ -f splash.jpg ]; then cp splash.jpg $(ISO_DIR)/; fi
	
	mkdir -p $(ISO_DIR)/Library/images/gif
	@for f in $(SRC_DIR)/images/gif/*.gif; do \
		if [ -f "$$f" ]; then \
			basename=$$(basename "$$f"); \
			cp "$$f" $(ISO_DIR)/Library/images/gif/; \
			echo "    module_path: boot():/Library/images/gif/$$basename" >> $(ISO_DIR)/limine.conf; \
		fi \
	done
	
	mkdir -p $(ISO_DIR)/docs
	@for f in $$(find docs -name '*.md'); do \
		if [ -f "$$f" ]; then \
			dir=$$(dirname "$$f"); \
			mkdir -p $(ISO_DIR)/"$$dir"; \
			cp "$$f" $(ISO_DIR)/"$$dir"/; \
			echo "    module_path: boot():/$$f" >> $(ISO_DIR)/limine.conf; \
		fi \
	done
	
	cp limine/limine-bios.sys $(ISO_DIR)/
	cp limine/limine-bios-cd.bin $(ISO_DIR)/
	cp limine/limine-uefi-cd.bin $(ISO_DIR)/
	
	cp limine/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	cp limine/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/

	mkdir -p $(ISO_DIR)/Library/Fonts
	@for f in $(SRC_DIR)/fonts/*.ttf; do \
		if [ -f "$$f" ]; then \
			basename=$$(basename "$$f"); \
			cp "$$f" $(ISO_DIR)/Library/Fonts/; \
			echo "    module_path: boot():/Library/Fonts/$$basename" >> $(ISO_DIR)/limine.conf; \
		fi \
	done
	
	@if [ -f README.md ]; then \
		cp README.md $(ISO_DIR)/; \
		echo "    module_path: boot():/README.md" >> $(ISO_DIR)/limine.conf; \
	fi
	
	@if [ -f LICENSE ]; then \
		cp LICENSE $(ISO_DIR)/; \
		echo "    module_path: boot():/LICENSE" >> $(ISO_DIR)/limine.conf; \
	fi
	
	$(XORRISO) -as mkisofs -R -J -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	
	./limine/limine bios-install $(ISO_IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO_IMAGE)
	$(MAKE) -C $(SRC_DIR)/userland clean

run: $(ISO_IMAGE)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
		-netdev user,id=net0,hostfwd=udp::12346-:12345 -device virtio-net-pci,netdev=net0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display cocoa,show-cursor=off \
		-drive file=disk.img,format=raw,file.locking=off \
		-cpu max