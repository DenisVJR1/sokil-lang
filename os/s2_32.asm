# Sokil OS 32-bit - protected mode
# Stage2 at 0x8000: A20 -> GDT -> PE -> VGA banner
.code16
.text
.global _start
_start:
    movw $0x800, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0xfffe, %sp
    movb $0x92, %al
    outb %al, $0x92
    lgdt gdtdesc
    movl %cr0, %eax
    orl  $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $(0x8000 + (pm - _start))
.code32
pm:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x6000, %esp
    movl $(0x8000 + (msg - _start)), %esi
    movl $0xb8000, %edi
1:
    lodsb
    testb %al, %al
    jz  2f
    movb %al, (%edi)
    movb $0x07, 1(%edi)
    addl $2, %edi
    jmp 1b
2:
    cli
    hlt
    jmp 2b
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdtdesc:
    .word gdtend - gdt - 1
    .long 0x8000 + (gdt - _start)
gdtend:
msg:
    .asciz "Sokil OS 32-bit - protected mode\r\n"