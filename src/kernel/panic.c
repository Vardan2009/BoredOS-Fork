#include "process.h"
#include "graphics.h"
#include "io.h"

extern void serial_write(const char *str);

static void draw_string_centered(int y, const char *s, uint32_t color) {
    if (!s) return;
    int len = 0;
    while (s[len]) len++;
    int x = (get_screen_width() - (len * 8)) / 2;
    draw_string(x, y, s, color);
}

void kernel_panic(registers_t *regs, const char *error_name) {
    // Disable interrupts to prevent nested panics
    asm volatile("cli");

    // Clear back buffer to black
    graphics_clear_back_buffer(0x00000000);

    int sh = get_screen_height();
    int cy = sh / 2;

    // Draw header
    draw_string_centered(cy - 150, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", 0xFFFF0000);
    draw_string_centered(cy - 130, "KERNEL EXCEPTION OCCURRED", 0xFFFFFFFF);
    draw_string_centered(cy - 110, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", 0xFFFF0000);

    // Error name
    char err_buf[256];
    int pos = 0;
    const char *prefix = "Exception: ";
    while(prefix[pos]) { err_buf[pos] = prefix[pos]; pos++; }
    int i = 0;
    while(error_name[i]) { err_buf[pos++] = error_name[i++]; }
    err_buf[pos] = 0;
    draw_string_centered(cy - 70, err_buf, 0xFFFFCC00);

    // Details - simplified centering by drawing them as a block or individually centered
    char info_buf[64];
    
    // Vector
    pos = 0;
    prefix = "Vector: ";
    while(prefix[pos]) { info_buf[pos] = prefix[pos]; pos++; }
    uint64_t v = regs->int_no;
    const char* digits = "0123456789ABCDEF";
    info_buf[pos++] = '0'; info_buf[pos++] = 'x';
    for (int i = 15; i >= 0; i--) {
        info_buf[pos + i] = digits[v & 0xF];
        v >>= 4;
    }
    info_buf[pos + 16] = 0;
    draw_string_centered(cy - 40, info_buf, 0xFFFFFFFF);

    // Error Code
    pos = 0;
    prefix = "Error Code: ";
    while(prefix[pos]) { info_buf[pos] = prefix[pos]; pos++; }
    v = regs->err_code;
    info_buf[pos++] = '0'; info_buf[pos++] = 'x';
    for (int i = 15; i >= 0; i--) {
        info_buf[pos + i] = digits[v & 0xF];
        v >>= 4;
    }
    info_buf[pos + 16] = 0;
    draw_string_centered(cy - 20, info_buf, 0xFFFFFFFF);

    // RIP
    pos = 0;
    prefix = "RIP: ";
    while(prefix[pos]) { info_buf[pos] = prefix[pos]; pos++; }
    v = regs->rip;
    info_buf[pos++] = '0'; info_buf[pos++] = 'x';
    for (int i = 15; i >= 0; i--) {
        info_buf[pos + i] = digits[v & 0xF];
        v >>= 4;
    }
    info_buf[pos + 16] = 0;
    draw_string_centered(cy, info_buf, 0xFFFFFFFF);

    // CR2 for page faults
    if (regs->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        pos = 0;
        prefix = "CR2: ";
        while(prefix[pos]) { info_buf[pos] = prefix[pos]; pos++; }
        info_buf[pos++] = '0'; info_buf[pos++] = 'x';
        for (int i = 15; i >= 0; i--) {
            info_buf[pos + i] = digits[cr2 & 0xF];
            cr2 >>= 4;
        }
        info_buf[pos + 16] = 0;
        draw_string_centered(cy + 20, info_buf, 0xFFFF5555);
    }

    // Message
    draw_string_centered(cy + 100, "The system has been halted to prevent damage.", 0xFFFFFFFF);
    draw_string_centered(cy + 120, "Please restart your computer.", 0xFFAAAAAA);


    // Flip buffer to screen
    graphics_mark_screen_dirty();
    graphics_flip_buffer();

    serial_write("\n*** KERNEL PANIC ***\n");
    serial_write(error_name);
    serial_write("\n");

    // Halt
    while(1) {
        asm volatile("cli; hlt");
    }
}
