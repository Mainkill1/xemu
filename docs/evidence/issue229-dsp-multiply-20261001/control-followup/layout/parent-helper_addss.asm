
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2eeb0:      	pushq	%r14
  e2eeb2:      	pushq	%rbx
  e2eeb3:      	pushq	%rax
  e2eeb4:      	movq	%rdx, %rbx
  e2eeb7:      	movq	%rsi, %r14
  e2eeba:      	movl	(%rdx), %eax
  e2eebc:      	movl	(%rcx), %esi
  e2eebe:      	leaq	0x272(%rdi), %rdx
  e2eec5:      	movl	%eax, %edi
  e2eec7:      	callq	0xbd1ad0 <.text+0x4f42d0>
  e2eecc:      	movl	%eax, (%r14)
  e2eecf:      	movl	0x4(%rbx), %eax
  e2eed2:      	movl	%eax, 0x4(%r14)
  e2eed6:      	movl	0x8(%rbx), %eax
  e2eed9:      	movl	%eax, 0x8(%r14)
  e2eedd:      	movl	0xc(%rbx), %eax
  e2eee0:      	movl	%eax, 0xc(%r14)
  e2eee4:      	addq	$0x8, %rsp
  e2eee8:      	popq	%rbx
  e2eee9:      	popq	%r14
  e2eeeb:      	xorl	%eax, %eax
  e2eeed:      	xorl	%ecx, %ecx
  e2eeef:      	xorl	%edi, %edi
  e2eef1:      	xorl	%edx, %edx
  e2eef3:      	xorl	%esi, %esi
  e2eef5:      	retq
