
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2f2a0:      	pushq	%r14
  e2f2a2:      	pushq	%rbx
  e2f2a3:      	pushq	%rax
  e2f2a4:      	movq	%rdx, %rbx
  e2f2a7:      	movq	%rsi, %r14
  e2f2aa:      	movl	(%rdx), %eax
  e2f2ac:      	movl	(%rcx), %esi
  e2f2ae:      	leaq	0x272(%rdi), %rdx
  e2f2b5:      	movl	%eax, %edi
  e2f2b7:      	callq	0xbd4cb0 <.text+0x4f7510>
  e2f2bc:      	movl	%eax, (%r14)
  e2f2bf:      	movl	0x4(%rbx), %eax
  e2f2c2:      	movl	%eax, 0x4(%r14)
  e2f2c6:      	movl	0x8(%rbx), %eax
  e2f2c9:      	movl	%eax, 0x8(%r14)
  e2f2cd:      	movl	0xc(%rbx), %eax
  e2f2d0:      	movl	%eax, 0xc(%r14)
  e2f2d4:      	addq	$0x8, %rsp
  e2f2d8:      	popq	%rbx
  e2f2d9:      	popq	%r14
  e2f2db:      	xorl	%eax, %eax
  e2f2dd:      	xorl	%ecx, %ecx
  e2f2df:      	xorl	%edi, %edi
  e2f2e1:      	xorl	%edx, %edx
  e2f2e3:      	xorl	%esi, %esi
  e2f2e5:      	retq
