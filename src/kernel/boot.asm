; 64-bit Entry Point for BoredOS

section .text
global _start
extern kmain

bits 64

_start:
    cli
    

    call kmain

    hlt
.loop:
    jmp .loop
