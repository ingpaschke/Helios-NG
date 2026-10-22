	.text
	.globl _start
_start:
	xorl %ebp,%ebp
	popl %esi
	movl %esp,%ecx
	andl $-16,%esp
	pushl %eax
	pushl %esp
	pushl %edx
	pushl $0
	pushl $0
	pushl %ecx
	pushl %esi
	pushl $main
	call __libc_start_main
	hlt
	# glibc's i386 libio uses this crt1.o symbol to tell new (2.1) from
	# old (2.0) stdio binaries.
	.section .rodata
	.globl _IO_stdin_used
	.type _IO_stdin_used,@object
	.size _IO_stdin_used,4
_IO_stdin_used:
	.long 0x20001
	.section .note.GNU-stack,"",@progbits
