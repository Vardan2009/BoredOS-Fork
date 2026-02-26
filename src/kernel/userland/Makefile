CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld

CFLAGS = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -Ilibc
LDFLAGS = -m elf_x86_64 -nostdlib -static -no-pie -Ttext=0x40000000 --no-dynamic-linker -z text -z max-page-size=0x1000 -e _start

LIBC_SOURCES = $(wildcard libc/*.c)
LIBC_OBJS = $(LIBC_SOURCES:.c=.o) crt0.o

APP_SOURCES = $(filter-out nanojpeg.c, $(wildcard *.c))
APP_OBJS = $(APP_SOURCES:.c=.o)
APP_ELFS = $(APP_SOURCES:.c=.elf)

all: $(APP_ELFS)

crt0.o: crt0.asm
	$(AS) -f elf64 $< -o $@

libc/%.o: libc/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

viewer.elf: $(LIBC_OBJS) viewer.o nanojpeg.o
	$(LD) $(LDFLAGS) $^ -o $@

%.elf: $(LIBC_OBJS) %.o
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o libc/*.o *.elf
