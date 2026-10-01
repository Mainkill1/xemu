
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
 1049d90:      	pushq	%r14
 1049d92:      	pushq	%rbx
 1049d93:      	pushq	%rax
 1049d94:      	movq	0xb438(%rdi), %r14
 1049d9b:      	addl	0xb428(%rdi), %esi
 1049da1:      	movl	%esi, 0xb428(%rdi)
 1049da7:      	testl	%esi, %esi
 1049da9:      	jle	0x1049ddc <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fdc>
 1049dab:      	movq	%rdi, %rbx
 1049dae:      	nop
 1049db0:      	movq	%r14, %rdi
 1049db3:      	callq	0x104be20 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418020>
 1049db8:      	movzwl	0x8(%r14), %eax
 1049dbd:      	subl	%eax, 0xb428(%rbx)
 1049dc3:      	movzwl	0x8(%r14), %eax
 1049dc8:      	addl	%eax, 0x4(%r14)
 1049dcc:      	cmpb	$0x1, 0x1(%r14)
 1049dd1:      	je	0x1049ddc <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fdc>
 1049dd3:      	cmpl	$0x0, 0xb428(%rbx)
 1049dda:      	jg	0x1049db0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x415fb0>
 1049ddc:      	addq	$0x8, %rsp
 1049de0:      	popq	%rbx
 1049de1:      	popq	%r14
 1049de3:      	xorl	%eax, %eax
 1049de5:      	xorl	%edi, %edi
 1049de7:      	xorl	%esi, %esi
 1049de9:      	retq
