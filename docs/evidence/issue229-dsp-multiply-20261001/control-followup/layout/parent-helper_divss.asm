
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2f300:      	pushq	%r14
  e2f302:      	pushq	%rbx
  e2f303:      	pushq	%rax
  e2f304:      	movq	%rdx, %rbx
  e2f307:      	movq	%rsi, %r14
  e2f30a:      	movl	(%rdx), %eax
  e2f30c:      	movl	(%rcx), %esi
  e2f30e:      	leaq	0x272(%rdi), %rdx
  e2f315:      	movl	%eax, %edi
  e2f317:      	callq	0xbd4d10 <.text+0x4f7510>
  e2f31c:      	movl	%eax, (%r14)
  e2f31f:      	movl	0x4(%rbx), %eax
  e2f322:      	movl	%eax, 0x4(%r14)
  e2f326:      	movl	0x8(%rbx), %eax
  e2f329:      	movl	%eax, 0x8(%r14)
  e2f32d:      	movl	0xc(%rbx), %eax
  e2f330:      	movl	%eax, 0xc(%r14)
  e2f334:      	addq	$0x8, %rsp
  e2f338:      	popq	%rbx
  e2f339:      	popq	%r14
  e2f33b:      	xorl	%eax, %eax
  e2f33d:      	xorl	%ecx, %ecx
  e2f33f:      	xorl	%edi, %edi
  e2f341:      	xorl	%edx, %edx
  e2f343:      	xorl	%esi, %esi
  e2f345:      	retq
