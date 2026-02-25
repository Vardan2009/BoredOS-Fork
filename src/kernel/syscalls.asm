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
    ; We arrived here from Ring 3 via `syscall`.
    ; RAX = syscall_num
    ; RDI, RSI, RDX, R10, R8, R9 = args
    ; RCX = User RIP
    ; R11 = User RFLAGS
    ; Current RSP = User RSP
    
    ; 1. Save User RSP
    mov [rel user_rsp_scratch], rsp
    
    ; 2. Switch to Kernel Stack
    mov rsp, [rel kernel_syscall_stack]

    ; 3. Save preserved registers (System V ABI)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    
    ; We also need to save RCX (RIP) and R11 (RFLAGS) because C functions might clobber them
    push rcx
    push r11

    ; Syscall convention: argument 4 is passed in R10, but C expects it in RCX
    mov rcx, r10

    ; The syscall number is in RAX, let's put it in RDI (arg 0 for C)
    ; But wait, the ABI expects arg1 in RDI!
    ; Let's change our C handler signature or adapt here.
    ; C handler: void syscall_handler_c(uint64_t syscall_num, uint64_t arg1, ...)
    ; So:
    ; syscall_num -> RDI
    ; arg1 (was RDI) -> RSI
    ; arg2 (was RSI) -> RDX
    ; arg3 (was RDX) -> RCX
    ; arg4 (was R10) -> R8
    ; arg5 (was R8) -> R9
    ; arg6 (was R9) -> stack (if needed, but we have 6 regs)
    
    ; This shuffling is messy. Let's just push everything and call a struct-based handler,
    ; or carefully shuffle.
    ; For now, let's just make sure RAX goes to RDI, RDI to RSI, RSI to RDX, RDX to RCX, R10 to R8.
    
    ; Shuffling for SYS V C ABI:
    ; R9 is arg6 -> no room in registers, need to push to stack (but our handler takes 6 args total)
    mov r9, r8   ; arg5
    mov r8, r10  ; arg4
    mov rcx, rdx ; arg3
    mov rdx, rsi ; arg2
    mov rsi, rdi ; arg1
    mov rdi, rax ; syscall_num

    ; 4. Call C handler
    call syscall_handler_c

    ; 5. Restore RCX and R11
    pop r11
    pop rcx

    ; 6. Restore preserved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; 7. Restore User RSP
    mov rsp, [rel user_rsp_scratch]

    ; 8. Return to User Mode
    ; NASM syntax for 64-bit sysret requires the o64 prefix
    ; Force IF=1 (bit 9) in R11 (restored to RFLAGS) to ensure interrupts stay enabled!
    or r11, 0x200
    
    o64 sysret

section .bss
global kernel_syscall_stack
global user_rsp_scratch
kernel_syscall_stack: resq 1
user_rsp_scratch: resq 1
