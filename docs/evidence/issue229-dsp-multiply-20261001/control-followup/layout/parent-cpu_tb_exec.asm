
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  dda2e0:      	pushq	%r15
  dda2e2:      	pushq	%r14
  dda2e4:      	pushq	%r13
  dda2e6:      	pushq	%r12
  dda2e8:      	pushq	%rbx
  dda2e9:      	movq	%rdx, %r15
  dda2ec:      	movq	%rdi, %rbx
  dda2ef:      	movq	0x28(%rsi), %r14
  dda2f3:      	leaq	0xae463e(%rip), %r13    # 0x18be938
  dda2fa:      	movzwl	(%r13), %eax
  dda2ff:      	testl	$0x120, %eax            # imm = 0x120
  dda304:      	je	0xdda337 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64d7>
  dda306:      	movq	%rsi, %r12
  dda309:      	movl	0x14(%rsi), %eax
  dda30c:      	testl	$0x20000, %eax          # imm = 0x20000
  dda311:      	jne	0xdda319 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64b9>
  dda313:      	movq	(%r12), %rdi
  dda317:      	jmp	0xdda32c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64cc>
  dda319:      	movq	0x98(%rbx), %rax
  dda320:      	movq	%rbx, %rdi
  dda323:      	callq	*0xf8(%rax)
  dda329:      	movq	%rax, %rdi
  dda32c:      	movq	%rbx, %rsi
  dda32f:      	movq	%r12, %rdx
  dda332:      	callq	0xddaf10 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a70b0>
  dda337:      	leaq	0xab00d2(%rip), %rax    # 0x188a410
  dda33e:      	leaq	0x4b60(%rbx), %rdi
  dda345:      	movq	%r14, %rsi
  dda348:      	callq	*(%rax)
  dda34a:      	movb	$0x1, 0x4b54(%rbx)
  dda351:      	movq	%rax, %r14
  dda354:      	andq	$-0x4, %r14
  dda358:      	leaq	0xab9e19(%rip), %rcx    # 0x1894178
  dda35f:      	movq	%r14, %r12
  dda362:      	subq	(%rcx), %r12
  dda365:      	testq	%r14, %r14
  dda368:      	cmovneq	%r12, %r14
  dda36c:      	andl	$0x3, %eax
  dda36f:      	movl	%eax, (%r15)
  dda372:      	leaq	0xae4827(%rip), %rcx    # 0x18beba0
  dda379:      	cmpl	$0x0, (%rcx)
  dda37c:      	jne	0xdda4a6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6646>
  dda382:      	cmpl	$0x2, %eax
  dda385:      	jl	0xdda488 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda38b:      	movq	0x98(%rbx), %rax
  dda392:      	movq	0x150(%rax), %rcx
  dda399:      	movq	0x20(%rcx), %rcx
  dda39d:      	testq	%rcx, %rcx
  dda3a0:      	je	0xdda3b6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6556>
  dda3a2:      	movq	%rbx, %rdi
  dda3a5:      	movq	%r14, %rsi
  dda3a8:      	callq	*%rcx
  dda3aa:      	testb	$0x20, (%r13)
  dda3af:      	jne	0xdda3de <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a657e>
  dda3b1:      	jmp	0xdda488 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda3b6:      	movl	0x14(%r14), %ecx
  dda3ba:      	movq	0xf0(%rax), %rax
  dda3c1:      	testq	%rax, %rax
  dda3c4:      	je	0xdda4f8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6698>
  dda3ca:      	movq	(%r12), %rsi
  dda3ce:      	movq	%rbx, %rdi
  dda3d1:      	callq	*%rax
  dda3d3:      	testb	$0x20, (%r13)
  dda3d8:      	je	0xdda488 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda3de:      	movl	0x14(%r14), %eax
  dda3e2:      	testl	$0x20000, %eax          # imm = 0x20000
  dda3e7:      	jne	0xdda3ef <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a658f>
  dda3e9:      	movq	(%r12), %r15
  dda3ed:      	jmp	0xdda402 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65a2>
  dda3ef:      	movq	0x98(%rbx), %rax
  dda3f6:      	movq	%rbx, %rdi
  dda3f9:      	callq	*0xf8(%rax)
  dda3ff:      	movq	%rax, %r15
  dda402:      	movq	0xae451f(%rip), %rcx    # 0x18be928
  dda409:      	testq	%rcx, %rcx
  dda40c:      	je	0xdda43c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65dc>
  dda40e:      	movl	0x8(%rcx), %eax
  dda411:      	testl	%eax, %eax
  dda413:      	je	0xdda488 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda415:      	movq	(%rcx), %rcx
  dda418:      	xorl	%edx, %edx
  dda41a:      	jmp	0xdda426 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c6>
  dda41c:      	nopl	(%rax)
  dda420:      	incl	%edx
  dda422:      	cmpl	%edx, %eax
  dda424:      	je	0xdda488 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda426:      	movslq	%edx, %rsi
  dda429:      	shlq	$0x4, %rsi
  dda42d:      	cmpq	(%rcx,%rsi), %r15
  dda431:      	jb	0xdda420 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c0>
  dda433:      	addq	%rcx, %rsi
  dda436:      	cmpq	0x8(%rsi), %r15
  dda43a:      	ja	0xdda420 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c0>
  dda43c:      	movq	0x28(%r12), %r12
  dda441:      	leaq	0xa7ec78(%rip), %rax    # 0x18590c0
  dda448:      	movq	(%rax), %r13
  dda44b:      	testq	%r13, %r13
  dda44e:      	je	0xdda46a <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a660a>
  dda450:      	movq	%r13, %rdi
  dda453:      	movq	%r15, %rsi
  dda456:      	callq	*(%r13)
  dda45a:      	cmpb	$0x0, (%rax)
  dda45d:      	jne	0xdda471 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6611>
  dda45f:      	movq	0x20(%r13), %r13
  dda463:      	testq	%r13, %r13
  dda466:      	jne	0xdda450 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65f0>
  dda468:      	jmp	0xdda471 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6611>
  dda46a:      	leaq	-0xb99b5c(%rip), %rax   # 0x240915
  dda471:      	leaq	-0xbdd389(%rip), %rdi   # 0x1fd0ef
  dda478:      	movq	%r12, %rsi
  dda47b:      	movq	%r15, %rdx
  dda47e:      	movq	%rax, %rcx
  dda481:      	xorl	%eax, %eax
  dda483:      	callq	0x11cd810 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x5999b0>
  dda488:      	cmpl	$0x0, 0xd4(%rbx)
  dda48f:      	jne	0xdda4dd <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a667d>
  dda491:      	movq	%r14, %rax
  dda494:      	popq	%rbx
  dda495:      	popq	%r12
  dda497:      	popq	%r13
  dda499:      	popq	%r14
  dda49b:      	popq	%r15
  dda49d:      	xorl	%ecx, %ecx
  dda49f:      	xorl	%edi, %edi
  dda4a1:      	xorl	%edx, %edx
  dda4a3:      	xorl	%esi, %esi
  dda4a5:      	retq
  dda4a6:      	leaq	0xae3a6d(%rip), %rcx    # 0x18bdf1a
  dda4ad:      	cmpw	$0x0, (%rcx)
  dda4b1:      	je	0xdda382 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda4b7:      	testb	$-0x80, 0x1(%r13)
  dda4bc:      	je	0xdda382 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda4c2:      	leaq	-0xaeec42(%rip), %rdi   # 0x2eb887
  dda4c9:      	movq	%r14, %rsi
  dda4cc:      	movl	%eax, %edx
  dda4ce:      	xorl	%eax, %eax
  dda4d0:      	callq	0x11cd810 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x5999b0>
  dda4d5:      	movl	(%r15), %eax
  dda4d8:      	jmp	0xdda382 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda4dd:      	cmpl	$-0x1, 0x2d0(%rbx)
  dda4e4:      	jne	0xdda491 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6631>
  dda4e6:      	movl	$0x10002, 0x2d0(%rbx)   # imm = 0x10002
  dda4f0:      	movq	%rbx, %rdi
  dda4f3:      	callq	0xddbde0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a7f80>
  dda4f8:      	leaq	-0xa72262(%rip), %rdi   # 0x36829d
  dda4ff:      	leaq	-0xb709ce(%rip), %rsi   # 0x269b38
  dda506:      	leaq	-0xbba151(%rip), %rcx   # 0x2203bc
  dda50d:      	movl	$0x202, %edx            # imm = 0x202
  dda512:      	callq	0x16cf870 <__assert_fail@plt>
