section .data
    msg db "Hello, ASM z poziomu NASM!", 10    ; 10 to znak nowej linii
    msg_len equ $ - msg

section .text
    global _start

_start:
    ; Wywołanie systemowe write (sys_write = 1)
    mov rax, 1          ; numer syscall (write)
    mov rdi, 1          ; fd = 1 (stdout)
    mov rsi, msg        ; wskaźnik na tekst
    mov rdx, msg_len    ; długość tekstu
    syscall

    ; Wywołanie systemowe exit (sys_exit = 60)
    mov rax, 60         ; numer syscall (exit)
    mov rdi, 0          ; kod wyjścia 0
    syscall