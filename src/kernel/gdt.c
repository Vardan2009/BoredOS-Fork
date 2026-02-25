#include "gdt.h"
#include <stdint.h>
#include <stddef.h>
#include "memory_manager.h"

static void *gdt_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

#define GDT_ENTRIES 7

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdtr;
struct tss_entry tss;

extern void gdt_flush(uint64_t);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

static void gdt_set_tss_gate(int num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    // A TSS entry in x86_64 is 16 bytes (takes up 2 adjacent GDT entries)
    struct {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t  base_middle;
        uint8_t  access;
        uint8_t  granularity;
        uint8_t  base_high;
        uint32_t base_upper;
        uint32_t reserved;
    } __attribute__((packed)) *tss_desc = (void*)&gdt[num];

    tss_desc->base_low = (base & 0xFFFF);
    tss_desc->base_middle = (base >> 16) & 0xFF;
    tss_desc->base_high = (base >> 24) & 0xFF;
    tss_desc->base_upper = (base >> 32);

    tss_desc->limit_low = (limit & 0xFFFF);
    tss_desc->granularity = ((limit >> 16) & 0x0F);

    tss_desc->granularity |= (gran & 0xF0);
    tss_desc->access = access;
    tss_desc->reserved = 0;
}

void tss_set_stack(uint64_t kernel_stack) {
    tss.rsp0 = kernel_stack;
}

void gdt_init(void) {
    gdtr.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtr.base = (uint64_t)&gdt;

    // NULL segment
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel Code segment (Ring 0, 64-bit)
    // 0x9A: Present(1), Ring(0), System(1), Executable(1), DirConforming(0), Readable(1), Accessed(0)
    // 0xAF: Long Mode (64-bit) (L=1, DB=0)
    gdt_set_gate(1, 0, 0, 0x9A, 0xAF);

    // Kernel Data segment (Ring 0)
    // 0x92: Present(1), Ring(0), System(1), Executable(0), DirExpandDown(0), Writable(1), Accessed(0)
    gdt_set_gate(2, 0, 0, 0x92, 0xAF);

    // User Data segment (Ring 3)
    // 0xF2: Present(1), Ring(3), System(1), Executable(0), DirExpandDown(0), Writable(1), Accessed(0)
    gdt_set_gate(3, 0, 0, 0xF2, 0xAF);

    // User Code segment (Ring 3, 64-bit)
    // 0xFA: Present(1), Ring(3), System(1), Executable(1), DirConforming(0), Readable(1), Accessed(0)
    // 0xAF: Long Mode (64-bit)
    gdt_set_gate(4, 0, 0, 0xFA, 0xAF);

    // TSS segment (takes entries 5 and 6 technically because it's 16 bytes)
    gdt_memset(&tss, 0, sizeof(struct tss_entry));
    tss.iopb_offset = sizeof(struct tss_entry);
    
    // Allocate a default Ring 0 interrupt stack in case an interrupt fires early or 
    // the scheduler hasn't set one up yet for a task.
    void* initial_tss_stack = kmalloc_aligned(4096, 4096);
    if (initial_tss_stack) {
        tss.rsp0 = (uint64_t)initial_tss_stack + 4096;
    }
    
    gdt_set_tss_gate(5, (uint64_t)&tss, sizeof(struct tss_entry) - 1, 0x89, 0x00);

    gdt_flush((uint64_t)&gdtr);
    tss_flush();
}
