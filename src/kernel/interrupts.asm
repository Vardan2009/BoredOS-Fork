section .text
global isr0_wrapper
global isr1_wrapper
global isr8_wrapper
global isr12_wrapper
global isr14_wrapper
extern timer_handler
extern keyboard_handler
extern mouse_handler
extern exception_handler_c

; Helper to send EOI (End of Interrupt) to PIC
send_eoi:
    push rax
    mov al, 0x20
    out 0x20, al ; Master PIC

    pop rax
    ret

%macro ISR_NOERRCODE 1
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass current RSP as 1st argument
    mov rdi, rsp
    
    call %1
    
    ; Update RSP with return value (task switch)
    mov rsp, rax
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
%endmacro

isr0_wrapper:
    ISR_NOERRCODE timer_handler

isr1_wrapper:
    ISR_NOERRCODE keyboard_handler

isr12_wrapper:
    ISR_NOERRCODE mouse_handler

; Common exception macro
%macro EXCEPTION_ERRCODE 1
isr%1_wrapper:
    push %1
    jmp exception_common
%endmacro

; Exception 8: Double Fault (has error code)
EXCEPTION_ERRCODE 8

; Exception 14: Page Fault (has error code)
EXCEPTION_ERRCODE 14

exception_common:
    ; Save registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Call C handler: void exception_handler_c(uint64_t vector, uint64_t err_code, uint64_t rip, uint64_t cr2)
    ; Stack right now: 15 registers (15*8=120 bytes), then vector (8), then err_code (8), then RIP (8), CS (8), RFLAGS (8), RSP (8), SS (8)
    mov rdi, [rsp + 120] ; vector
    mov rsi, [rsp + 128] ; err_code
    mov rdx, [rsp + 136] ; RIP
    mov rcx, cr2         ; CR2
    
    call exception_handler_c
    
    ; Restore (in case we want to return, but usually we halt)
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16 ; drop vector and error code
    iretq
