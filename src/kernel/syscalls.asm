global syscall_entry
extern syscall_handler_c

section .text

; Syscall ABI:
; RDI = syscall_num
; RSI = arg1
; RDX = arg2
; R10 = arg3
; R8  = arg4
; R9  = arg5

syscall_entry:
    ; 1. Switch to Kernel Stack
    ; Use scratch temporarily to pivot (Risk: Task switch here is rare but possible)
    mov [rel user_rsp_scratch], rsp
    mov rsp, [rel kernel_syscall_stack]

    ; 2. Save User RSP on per-process kernel stack
    push qword [rel user_rsp_scratch]

    ; 3. Save preserved registers (System V ABI)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    
    ; 4. Save RCX (RIP) and R11 (RFLAGS)
    push rcx
    push r11

    ; Shuffling for SYS V C ABI:
    ; arg5: R9 (remains R9 as 6th arg in C)
    ; arg4: R8 (was R9)
    ; arg3: RCX (was R10)
    ; arg2: RDX (was RSI)
    ; arg1: RSI (was RDI)
    ; num: RDI (was RAX)
    
    mov r9, r8   ; arg5
    mov r8, r10  ; arg4
    mov rcx, rdx ; arg3
    mov rdx, rsi ; arg2
    mov rsi, rdi ; arg1
    mov rdi, rax ; syscall_num

    ; 5. Call C handler
    call syscall_handler_c

    ; 6. Restore RCX and R11
    pop r11
    pop rcx

    ; 7. Restore preserved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; 8. Restore User RSP from kernel stack
    pop rsp

    ; 9. Return to User Mode (sysret)
    or r11, 0x200 ; Force Interrupts enabled
    o64 sysret

section .bss
global kernel_syscall_stack
global user_rsp_scratch
kernel_syscall_stack: resq 1
user_rsp_scratch: resq 1
