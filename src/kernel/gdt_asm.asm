global gdt_flush
global tss_flush

section .text

gdt_flush:
    lgdt [rdi]      ; Load GDT from the pointer passed in RDI

    mov ax, 0x10    ; 0x10 is our offset in the GDT to our data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to update CS
    push 0x08       ; 0x08 is our offset to the code segment
    lea rax, [rel .flush]
    push rax
    retfq

.flush:
    ret

tss_flush:
    mov ax, 0x28    ; 0x28 is our offset in the GDT to the TSS
    ltr ax
    ret
