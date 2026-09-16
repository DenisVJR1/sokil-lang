# Sokil OS 16-bit - real mode shell
# Stage2 at 0x8000, segment 0x800
.code16
.text
.global _start
_start:
    movw $0x800, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0xfffe, %sp
    movw $msg, %si
1:
    lodsb
    testb %al, %al
    jz  2f
    movb $0x0e, %ah
    movb $0x07, %bl
    int  $0x10
    jmp 1b
2:
    movb $0x00, %ah
    int  $0x16
    cmpb $0x1b, %al
    je  3f
    movb $0x0e, %ah
    int  $0x10
    jmp 2b
3:
    cli
    hlt
    jmp 3b
msg:
    .asciz "Sokil OS 16-bit - real mode\r\nType text, ESC to exit\r\nSokil> "