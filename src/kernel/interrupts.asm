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

%macro ISR_NOERRCODE 2
isr%2_wrapper:
    push 0      ; Dummy error code
    push %2     ; Vector
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
    
    ; Pass current RSP as 1st argument (registers_t*)
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
    add rsp, 16 ; drop dummy vector and error code
    iretq
%endmacro

isr0_wrapper:
    ISR_NOERRCODE timer_handler, 32

isr1_wrapper:
    ISR_NOERRCODE keyboard_handler, 33

isr12_wrapper:
    ISR_NOERRCODE mouse_handler, 44

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
    
    ; Call C handler: uint64_t exception_handler_c(registers_t *regs)
    mov rdi, rsp
    call exception_handler_c
    
    ; Switch stack if needed (for process termination)
    mov rsp, rax
    
    ; Restore (in case we want to return, but usually we halt if kernel)
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
