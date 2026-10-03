
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2f190:      	pushq	%r14
  e2f192:      	pushq	%rbx
  e2f193:      	pushq	%rax
  e2f194:      	movq	%rdx, %rbx
  e2f197:      	movq	%rsi, %r14
  e2f19a:      	movl	(%rdx), %eax
  e2f19c:      	movl	(%rcx), %esi
  e2f19e:      	leaq	0x272(%rdi), %rdx
  e2f1a5:      	movl	%eax, %edi
  e2f1a7:      	callq	0xbd26a0 <.text+0x4f4ea0>
  e2f1ac:      	movl	%eax, (%r14)
  e2f1af:      	movl	0x4(%rbx), %eax
  e2f1b2:      	movl	%eax, 0x4(%r14)
  e2f1b6:      	movl	0x8(%rbx), %eax
  e2f1b9:      	movl	%eax, 0x8(%r14)
  e2f1bd:      	movl	0xc(%rbx), %eax
  e2f1c0:      	movl	%eax, 0xc(%r14)
  e2f1c4:      	addq	$0x8, %rsp
  e2f1c8:      	popq	%rbx
  e2f1c9:      	popq	%r14
  e2f1cb:      	xorl	%eax, %eax
  e2f1cd:      	xorl	%ecx, %ecx
  e2f1cf:      	xorl	%edi, %edi
  e2f1d1:      	xorl	%edx, %edx
  e2f1d3:      	xorl	%esi, %esi
  e2f1d5:      	retq
