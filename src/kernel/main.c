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
#include "memory_manager.h"
#include "platform.h"
#include "wallpaper.h"
#include "viewer.h"

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

__attribute__((used, section(".requests_start")))
static volatile struct limine_request *const requests_start_marker[] = {
    (struct limine_request *)&framebuffer_request,
    (struct limine_request *)&memmap_request,
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

// Simple serial port initialization and output for debugging
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

// Kernel Entry Point
void kmain(void) {
    init_serial();
    serial_write("\n[DEBUG] Entering kmain...\n");

    platform_init();
    serial_write("[DEBUG] platform_init OK\n");

    // 1. Graphics Init
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        serial_write("[DEBUG] No framebuffer! Halting.\n");
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    graphics_init(fb);
    serial_write("[DEBUG] graphics_init OK\n");

    // 1.5 GDT & TSS Init
    gdt_init();
    serial_write("[DEBUG] gdt_init OK\n");

    // 1.6 Paging Init
    paging_init();
    serial_write("[DEBUG] paging_init OK\n");

    // 1.7 Syscall Init
    syscall_init();
    serial_write("[DEBUG] syscall_init OK\n");

    // Set up a user page and jump to user space
    // 2. Interrupts Init
    idt_init();
    idt_register_interrupts();
    idt_load();
    serial_write("[DEBUG] idt_init OK\n");

    process_init();
    int ENABLE_USER_TEST = 1; // Set to 1 to test User Mode ring 3 jump
    if (ENABLE_USER_TEST) {
        // Create an isolated PML4 table for this "process"
        uint64_t user_pml4_phys = paging_create_user_pml4_phys();
        serial_write("[DEBUG] user_pml4 created OK\n");
        if (user_pml4_phys) {
            
            // Debug verify we can allocate
            void* code_page = kmalloc_aligned(4096, 4096);
            if (code_page) {
                extern void user_test_function(void);
                process_create(user_test_function, true);
                serial_write("[DEBUG] User Process 1 Created.\n");
            }
        }
    }

    serial_write("[DEBUG] Skipping user mode test, proceeding with normal boot.\n");
    
    // 2.5 Memory Manager Init - Calculate available RAM from Limine
    size_t total_usable_memory = 0;
    if (memmap_request.response != NULL) {
        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_request.response->entries[i];
            
            // Count usable memory regions
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                total_usable_memory += entry->length;
            }
        }
    }
    
    // Initialize memory manager with available memory (cap at 2GB for practical reasons)
    size_t pool_size = total_usable_memory > (2 * 1024 * 1024 * 1024) ? 
                       (2 * 1024 * 1024 * 1024) : total_usable_memory;
    
    if (pool_size == 0) {
        pool_size = 512 * 1024 * 1024;  // Fallback to 512MB if detection fails
    }
    
    memory_manager_init_with_size(pool_size);

    // 3. PS/2 Init (Mouse/Keyboard)
    asm("cli");
    ps2_init();
    asm("sti");

    // 4. Window Manager Init (Draws initial desktop)
    wm_init();

    // Re-enable interrupts since we removed sti from idt_load
    asm volatile("sti");

    // 5. Main loop - just wait for interrupts
    // Timer interrupt will drive the redraw system
    while (1) {
        wm_process_input();
        wallpaper_process_pending();
        viewer_process_pending();
        asm("hlt");
    }
}
