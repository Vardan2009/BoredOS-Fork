// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "limine.h"
#include "graphics.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "syscall.h"
#include "process.h"
#include "ps2.h"
#include "wm.h"
#include "io.h"
#include "fat32.h"
#include "memory_manager.h"
#include "platform.h"
#include "wallpaper.h"

// --- Limine Requests ---
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 1
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start")))
static volatile struct limine_request *const requests_start_marker[] = {
    (struct limine_request *)&framebuffer_request,
    (struct limine_request *)&memmap_request,
    (struct limine_request *)&module_request,
    NULL
};

__attribute__((used, section(".requests_end")))
static volatile struct limine_request *const requests_end_marker[] = {
    NULL
};

static void hcf(void) {
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}

static void init_serial() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

void serial_write(const char *str) {
    while (*str) {
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, *str++);
    }
}

void serial_write_num(uint32_t n) {
    if (n >= 10) serial_write_num(n / 10);
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, '0' + (n % 10));
}

void serial_write_hex(uint64_t n) {
    char *hex = "0123456789ABCDEF";
    if (n >= 16) serial_write_hex(n / 16);
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, hex[n % 16]);
}

// Kernel Entry Point
void kmain(void) {
    init_serial();
    serial_write("\n[DEBUG] Entering kmain...\n");

    platform_init();
    serial_write("[DEBUG] platform_init OK\n");
    
    extern uint64_t hhdm_offset;
    extern uint64_t kernel_phys_base;
    extern uint64_t kernel_virt_base;
    
    serial_write("[DEBUG] HHDM Offset: 0x");
    serial_write_hex(hhdm_offset);
    serial_write("\n");
    serial_write("[DEBUG] Kernel Phys: 0x");
    serial_write_hex(kernel_phys_base);
    serial_write("\n");
    serial_write("[DEBUG] Kernel Virt: 0x");
    serial_write_hex(kernel_virt_base);
    serial_write("\n");

    if (memmap_request.response != NULL) {
        // The memory manager will now scan the memory map and manage all usable regions.
        memory_manager_init_from_memmap(memmap_request.response);
        serial_write("[DEBUG] memory_manager_init OK\n");
    } else {
        serial_write("[DEBUG] ERROR: No usable memory for heap! Check Limine memmap.\n");
        hcf();
    }

    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        serial_write("[DEBUG] No framebuffer! Halting.\n");
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    graphics_init(fb);
    serial_write("[DEBUG] graphics_init OK\n");

    gdt_init();
    serial_write("[DEBUG] gdt_init OK\n");

    paging_init();
    serial_write("[DEBUG] paging_init OK\n");

    syscall_init();
    serial_write("[DEBUG] syscall_init OK\n");

    idt_init();
    idt_register_interrupts();
    idt_load();
    serial_write("[DEBUG] idt_init OK\n");

    process_init();

    serial_write("[DEBUG] Skipping user mode test, proceeding with normal boot.\n");
    
    fat32_init();
    serial_write("[DEBUG] fat32_init OK\n");
    fat32_mkdir("/bin");
    serial_write("[DEBUG] /bin directory created/checked\n");

    if (module_request.response == NULL) {
        serial_write("[DEBUG] ERROR: Limine Module Response is NULL!\n");
    } else {
        serial_write("[DEBUG] Limine Module Response found. Count: ");
        serial_write_num(module_request.response->module_count);
        serial_write("\n");
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *mod = module_request.response->modules[i];


            const char *clean_path = mod->path;
            // Strip boot():/ or boot:/// prefixes common in different Limine versions
            if (fs_starts_with(clean_path, "boot():")) clean_path += 7;
            else if (fs_starts_with(clean_path, "boot:///")) clean_path += 8;
            


            FAT32_FileHandle *fh = fat32_open(clean_path, "w");
            if (fh && fh->valid) {
                int written = fat32_write(fh, mod->address, mod->size);
                fat32_close(fh);
                serial_write("[DEBUG] Module successfully copied to RAMFS. Written bytes: ");
                serial_write_num(written);
                serial_write("\n");
            } else {
                serial_write("[DEBUG] ERROR: Failed to create file in RAMFS for module: ");
                serial_write(clean_path);
                serial_write("\n");
            }
        }
    }
    
    asm("cli");
    ps2_init();
    asm("sti");

    wm_init();

    asm volatile("sti");


    while (1) {
        wm_process_input();
        wm_process_deferred_thumbs();
        wallpaper_process_pending();
        asm("hlt");
    }
}
