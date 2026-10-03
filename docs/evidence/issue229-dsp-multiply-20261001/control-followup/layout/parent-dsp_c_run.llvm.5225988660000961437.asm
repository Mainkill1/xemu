
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
 1049df0:      	pushq	%r14
 1049df2:      	pushq	%rbx
 1049df3:      	pushq	%rax
 1049df4:      	movq	0xb438(%rdi), %r14
 1049dfb:      	addl	0xb428(%rdi), %esi
 1049e01:      	movl	%esi, 0xb428(%rdi)
 1049e07:      	testl	%esi, %esi
 1049e09:      	jle	0x1049e3c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fdc>
 1049e0b:      	movq	%rdi, %rbx
 1049e0e:      	nop
 1049e10:      	movq	%r14, %rdi
 1049e13:      	callq	0x104be80 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418020>
 1049e18:      	movzwl	0x8(%r14), %eax
 1049e1d:      	subl	%eax, 0xb428(%rbx)
 1049e23:      	movzwl	0x8(%r14), %eax
 1049e28:      	addl	%eax, 0x4(%r14)
 1049e2c:      	cmpb	$0x1, 0x1(%r14)
 1049e31:      	je	0x1049e3c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fdc>
 1049e33:      	cmpl	$0x0, 0xb428(%rbx)
 1049e3a:      	jg	0x1049e10 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fb0>
 1049e3c:      	addq	$0x8, %rsp
 1049e40:      	popq	%rbx
 1049e41:      	popq	%r14
 1049e43:      	xorl	%eax, %eax
 1049e45:      	xorl	%edi, %edi
 1049e47:      	xorl	%esi, %esi
 1049e49:      	retq
