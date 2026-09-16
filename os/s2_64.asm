# Sokil OS 64-bit - long mode
# Stage2 at 0x8000: A20 -> GDT -> PAE page tables -> EFER.LME -> paging -> 64-bit
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
    ljmp $0x08, $(0x8000 + (pm32 - _start))
.code32
pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x6000, %esp
    movl $0x9000, %edi
    xorl %eax, %eax
    movl $0x1000, %ecx
    cld
    rep stosl
    movl $0x9000, %eax
    movl $0xa003, (%eax)
    movl $0xa000, %eax
    movl $0xb003, (%eax)
    movl $0xb000, %eax
    movl $0x83, (%eax)
    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4
    movl $0x9000, %eax
    movl %eax, %cr3
    movl $0xc0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr
    movl %cr0, %eax
    orl  $0x80000001, %eax
    movl %eax, %cr0
    ljmp $0x08, $(0x8000 + (c64 - _start))
.code64
c64:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movabs $(0x8000 + (msg - _start)), %rsi
    movabs $0xb8000, %rdi
1:
    lodsb
    testb %al, %al
    jz  2f
    movb %al, (%rdi)
    movb $0x07, 1(%rdi)
    addq $2, %rdi
    jmp 1b
2:
    cli
    hlt
    jmp 2b
gdt:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdtdesc:
    .word gdtend - gdt - 1
    .long 0x8000 + (gdt - _start)
gdtend:
msg:
    .asciz "Sokil OS 64-bit - long mode\r\n"