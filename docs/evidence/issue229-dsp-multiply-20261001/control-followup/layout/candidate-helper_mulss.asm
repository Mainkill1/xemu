
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  e2f130:      	pushq	%r14
  e2f132:      	pushq	%rbx
  e2f133:      	pushq	%rax
  e2f134:      	movq	%rdx, %rbx
  e2f137:      	movq	%rsi, %r14
  e2f13a:      	movl	(%rdx), %eax
  e2f13c:      	movl	(%rcx), %esi
  e2f13e:      	leaq	0x272(%rdi), %rdx
  e2f145:      	movl	%eax, %edi
  e2f147:      	callq	0xbd2640 <.text+0x4f4ea0>
  e2f14c:      	movl	%eax, (%r14)
  e2f14f:      	movl	0x4(%rbx), %eax
  e2f152:      	movl	%eax, 0x4(%r14)
  e2f156:      	movl	0x8(%rbx), %eax
  e2f159:      	movl	%eax, 0x8(%r14)
  e2f15d:      	movl	0xc(%rbx), %eax
  e2f160:      	movl	%eax, 0xc(%r14)
  e2f164:      	addq	$0x8, %rsp
  e2f168:      	popq	%rbx
  e2f169:      	popq	%r14
  e2f16b:      	xorl	%eax, %eax
  e2f16d:      	xorl	%ecx, %ecx
  e2f16f:      	xorl	%edi, %edi
  e2f171:      	xorl	%edx, %edx
  e2f173:      	xorl	%esi, %esi
  e2f175:      	retq
