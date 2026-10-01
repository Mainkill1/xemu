
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2ee50:      	pushq	%r14
  e2ee52:      	pushq	%rbx
  e2ee53:      	pushq	%rax
  e2ee54:      	movq	%rdx, %rbx
  e2ee57:      	movq	%rsi, %r14
  e2ee5a:      	movl	(%rdx), %eax
  e2ee5c:      	movl	(%rcx), %esi
  e2ee5e:      	leaq	0x272(%rdi), %rdx
  e2ee65:      	movl	%eax, %edi
  e2ee67:      	callq	0xbd1a70 <.text+0x4f42d0>
  e2ee6c:      	movl	%eax, (%r14)
  e2ee6f:      	movl	0x4(%rbx), %eax
  e2ee72:      	movl	%eax, 0x4(%r14)
  e2ee76:      	movl	0x8(%rbx), %eax
  e2ee79:      	movl	%eax, 0x8(%r14)
  e2ee7d:      	movl	0xc(%rbx), %eax
  e2ee80:      	movl	%eax, 0xc(%r14)
  e2ee84:      	addq	$0x8, %rsp
  e2ee88:      	popq	%rbx
  e2ee89:      	popq	%r14
  e2ee8b:      	xorl	%eax, %eax
  e2ee8d:      	xorl	%ecx, %ecx
  e2ee8f:      	xorl	%edi, %edi
  e2ee91:      	xorl	%edx, %edx
  e2ee93:      	xorl	%esi, %esi
  e2ee95:      	retq
