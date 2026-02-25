global process_jump_usermode

section .text

; void process_jump_usermode(uint64_t entry_point, uint64_t user_stack)
; System V AMD64 ABI:
; RDI = entry_point
; RSI = user_stack
process_jump_usermode:
    cli

    ; Load user data segment (0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build the IRETQ stack frame
    ; 1. SS (User Data Segment)
    push 0x23
    
    ; 2. RSP (User Stack)
    push rsi
    
    ; 3. RFLAGS (Enable Interrupts: IF = 0x200 | Reserved bit 1 = 0x2 -> 0x202)
    push 0x202
    
    ; 4. CS (User Code Segment)
    push 0x1B
    
    ; 5. RIP (Entry Point)
    push rdi

    ; Jump to Ring 3!
    iretq
