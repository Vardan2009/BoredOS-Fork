global test_syscall
section .text

test_syscall:
    ; syscall number in RDI
    mov rdi, 1
    ; string pointer in RSI
    lea rsi, [rel test_msg]
    
    ; The SYSCALL instruction
    syscall
    
    ret

section .rodata
test_msg: db "Hello from Syscall!", 10, 0
