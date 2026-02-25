#include "idt.h"
#include "io.h"

extern void serial_write(const char *str);

// Simple hex printer for debugging exceptions
static void print_hex(uint64_t val) {
    const char* digits = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        buf[i] = digits[val & 0xF];
        val >>= 4;
    }
    serial_write("0x");
    serial_write(buf);
}

void exception_handler_c(uint64_t vector, uint64_t err_code, uint64_t rip, uint64_t cr2) {
    serial_write("\n*** EXCEPTION ***\nVector: ");
    print_hex(vector);
    serial_write("\nError Code: ");
    print_hex(err_code);
    serial_write("\nRIP: ");
    print_hex(rip);
    serial_write("\nCR2: ");
    print_hex(cr2);
    serial_write("\nCPU HALTED.\n");
    while(1) { asm volatile("cli; hlt"); }
}

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;

void idt_set_gate(uint8_t vector, void *isr, uint16_t cs, uint8_t flags) {
    uint64_t addr = (uint64_t)isr;
    idt[vector].isr_low = addr & 0xFFFF;
    idt[vector].kernel_cs = cs; 
    idt[vector].ist = 0;
    idt[vector].attributes = flags;
    idt[vector].isr_mid = (addr >> 16) & 0xFFFF;
    idt[vector].isr_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

// Remap PIC
static void pic_remap(void) {
    uint8_t a1, a2;
    a1 = inb(0x21);
    a2 = inb(0xA1);

    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); // Master offset 0x20 (32)
    outb(0xA1, 0x28); io_wait(); // Slave offset 0x28 (40)
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // 0xEF = 1110 1111 (IRQ 12 (4 on slave) unmasked)
    
    outb(0x21, 0xF9); // Unmask Keyboard (IRQ1) and Cascade (IRQ2)
    outb(0xA1, 0xEF); // Unmask Mouse (IRQ12)
}

// Set up PIT (Programmable Interval Timer) for ~60Hz (16.67ms intervals)
static void pit_setup(void) {
    uint16_t divisor = 1193182 / 60;  // ~60Hz
    
    // Send command byte
    outb(0x43, 0x36); // Channel 0, lobyte/hibyte, mode 3 (square wave), binary
    
    // Send divisor
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void idt_init(void) {
    uint16_t cs;
    asm volatile ("mov %%cs, %0" : "=r"(cs));

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = (struct idt_entry){0};
    }

    pic_remap();
    
    // Unmask IRQ 0 (Timer) in addition to IRQ 1 and 12
    outb(0x21, 0xF8); // Unmask Timer (IRQ0), Keyboard (IRQ1) and Cascade (IRQ2)
    outb(0xA1, 0xEF); // Unmask Mouse (IRQ12)
    
    pit_setup();
}

void idt_register_interrupts(void) {
    uint16_t cs;
    asm volatile ("mov %%cs, %0" : "=r"(cs));
    
    idt_set_gate(32, isr0_wrapper, cs, 0x8E);  // Timer (IRQ 0)
    idt_set_gate(33, isr1_wrapper, cs, 0x8E);  // Keyboard (IRQ 1)
    idt_set_gate(44, isr12_wrapper, cs, 0x8E); // Mouse (IRQ 12)

    // Exceptions
    extern void isr8_wrapper(void);
    extern void isr14_wrapper(void);
    idt_set_gate(8, isr8_wrapper, cs, 0x8E);  // Double Fault
    idt_set_gate(14, isr14_wrapper, cs, 0x8E); // Page Fault
}

void idt_load(void) {
    idtr.base = (uint64_t)&idt;
    idtr.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    asm volatile ("lidt %0" : : "m"(idtr));
    // Do not sti here! The OS must decide when to enable interrupts
    // after all subsystems (WM, PS/2) are initialized!
}
