; 64-bit Entry Point for BoredOS

section .text
global _start
extern kmain

bits 64

_start:
    ; Ensure interrupts are disabled
    cli
    
    ; (Limine guarantees 16-byte alignment)

    ; Call the C kernel entry point
    call kmain

    ; Halt if kmain returns
    hlt
.loop:
    jmp .loop
