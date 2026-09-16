/* Sokil OS loader - shared boot sector (16-bit real mode) */
/* Loads stage2 (3 sectors, track 0) into 0x8000 and jumps */
.code16
.text
.global _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    movb $0x00, %ah
    int $0x13
    movb %dl, drv
    movw $0x8000, %bx
    movb $0x00, %ch
    movb $0x02, %cl
    movb $0x00, %dh
    movb $0x03, %al
    movb $0x02, %ah
    movb drv, %dl
    int $0x13
    jc  halt
    ljmp $0x0000, $0x8000
halt:
    cli
    hlt
    jmp halt
drv:
    .byte 0
    .org 510
    .byte 0x55, 0xaa