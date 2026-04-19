# Copyright (c) 2023-2026 Chris (boreddevnl)
# This software is released under the GNU General Public License v3.0. See LICENSE file for details.
# This header needs to maintain in any file it is present in, as per the GPL license terms.
CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld

CFLAGS = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone -I. -Ilibc
LDFLAGS = -m elf_x86_64 -nostdlib -static -no-pie -Ttext=0x40000000 --no-dynamic-linker -z text -z max-page-size=0x1000 -e _start

BIN_DIR = bin

LIBC_SOURCES = $(wildcard libc/*.c)
LIBC_OBJS = $(patsubst libc/%.c, $(BIN_DIR)/%.o, $(LIBC_SOURCES)) $(BIN_DIR)/crt0.o $(BIN_DIR)/libwidget.o

VPATH = cli gui sys games libc net cli/third_party
vpath %.c cli gui sys games libc net cli/third_party

APP_SOURCES_FULL = $(wildcard cli/*.c gui/*.c sys/*.c games/*.c *.c net/*.c cli/third_party/*.c)
APP_SOURCES = $(filter-out stb_image.c, $(APP_SOURCES_FULL))
APP_ELFS = $(patsubst %.c, $(BIN_DIR)/%.elf, $(notdir $(APP_SOURCES)))

DOOM_SOURCES = $(wildcard games/doom/*.c)
DOOM_OBJS = $(patsubst games/doom/%.c, $(BIN_DIR)/%.o, $(DOOM_SOURCES))

LUA_DIR = cli/third_party/lua
LUA_CFLAGS = -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone -isystem $(LUA_DIR)/sysinclude -I. -Ilibc -I$(LUA_DIR) -DLUA_USE_C89 -Wno-conversion -Wno-sign-conversion -Wno-double-promotion -Wno-unused-parameter -Wno-missing-declarations -Wno-shadow -Wno-undef -Wno-redundant-decls -Wno-old-style-definition -Wno-missing-prototypes -Wno-implicit-fallthrough -Wno-type-limits

all: $(BIN_DIR) $(APP_ELFS) $(BIN_DIR)/doom.elf $(BIN_DIR)/lua.elf

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/crt0.o: crt0.asm
	$(AS) -f elf64 $< -o $@

$(BIN_DIR)/%.o: libc/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/libwidget.o: ../wm/libwidget.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/stb_image.o: stb_image.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%.o: %.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/viewer.elf: $(LIBC_OBJS) $(BIN_DIR)/viewer.o $(BIN_DIR)/stb_image.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/settings.elf: $(LIBC_OBJS) $(BIN_DIR)/settings.o $(BIN_DIR)/stb_image.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/browser.elf: $(LIBC_OBJS) $(BIN_DIR)/browser.o $(BIN_DIR)/stb_image.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/screenshot.elf: $(LIBC_OBJS) $(BIN_DIR)/screenshot.o $(BIN_DIR)/stb_image.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/%.o: games/doom/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wno-error -Igames/doom -c $< -o $@

$(BIN_DIR)/doom.elf: $(LIBC_OBJS) $(DOOM_OBJS) $(BIN_DIR)/stb_image.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/%.elf: $(LIBC_OBJS) $(BIN_DIR)/%.o
	$(LD) $(LDFLAGS) $^ -o $@

# Lua 5.5.0 - compiled as a single translation unit
$(BIN_DIR)/lua_onelua.o: $(LUA_DIR)/boredos_onelua.c | $(BIN_DIR)
	$(CC) $(LUA_CFLAGS) -c $< -o $@

$(BIN_DIR)/lua.elf: $(LIBC_OBJS) $(BIN_DIR)/lua_onelua.o
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BIN_DIR)
