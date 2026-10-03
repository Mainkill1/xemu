
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  dda280:      	pushq	%r15
  dda282:      	pushq	%r14
  dda284:      	pushq	%r13
  dda286:      	pushq	%r12
  dda288:      	pushq	%rbx
  dda289:      	movq	%rdx, %r15
  dda28c:      	movq	%rdi, %rbx
  dda28f:      	movq	0x28(%rsi), %r14
  dda293:      	leaq	0xadfefe(%rip), %r13    # 0x18ba198
  dda29a:      	movzwl	(%r13), %eax
  dda29f:      	testl	$0x120, %eax            # imm = 0x120
  dda2a4:      	je	0xdda2d7 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64d7>
  dda2a6:      	movq	%rsi, %r12
  dda2a9:      	movl	0x14(%rsi), %eax
  dda2ac:      	testl	$0x20000, %eax          # imm = 0x20000
  dda2b1:      	jne	0xdda2b9 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64b9>
  dda2b3:      	movq	(%r12), %rdi
  dda2b7:      	jmp	0xdda2cc <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a64cc>
  dda2b9:      	movq	0x98(%rbx), %rax
  dda2c0:      	movq	%rbx, %rdi
  dda2c3:      	callq	*0xf8(%rax)
  dda2c9:      	movq	%rax, %rdi
  dda2cc:      	movq	%rbx, %rsi
  dda2cf:      	movq	%r12, %rdx
  dda2d2:      	callq	0xddaeb0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a70b0>
  dda2d7:      	leaq	0xaab992(%rip), %rax    # 0x1885c70
  dda2de:      	leaq	0x4b60(%rbx), %rdi
  dda2e5:      	movq	%r14, %rsi
  dda2e8:      	callq	*(%rax)
  dda2ea:      	movb	$0x1, 0x4b54(%rbx)
  dda2f1:      	movq	%rax, %r14
  dda2f4:      	andq	$-0x4, %r14
  dda2f8:      	leaq	0xab56d9(%rip), %rcx    # 0x188f9d8
  dda2ff:      	movq	%r14, %r12
  dda302:      	subq	(%rcx), %r12
  dda305:      	testq	%r14, %r14
  dda308:      	cmovneq	%r12, %r14
  dda30c:      	andl	$0x3, %eax
  dda30f:      	movl	%eax, (%r15)
  dda312:      	leaq	0xae00e7(%rip), %rcx    # 0x18ba400
  dda319:      	cmpl	$0x0, (%rcx)
  dda31c:      	jne	0xdda446 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6646>
  dda322:      	cmpl	$0x2, %eax
  dda325:      	jl	0xdda428 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda32b:      	movq	0x98(%rbx), %rax
  dda332:      	movq	0x150(%rax), %rcx
  dda339:      	movq	0x20(%rcx), %rcx
  dda33d:      	testq	%rcx, %rcx
  dda340:      	je	0xdda356 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6556>
  dda342:      	movq	%rbx, %rdi
  dda345:      	movq	%r14, %rsi
  dda348:      	callq	*%rcx
  dda34a:      	testb	$0x20, (%r13)
  dda34f:      	jne	0xdda37e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a657e>
  dda351:      	jmp	0xdda428 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda356:      	movl	0x14(%r14), %ecx
  dda35a:      	movq	0xf0(%rax), %rax
  dda361:      	testq	%rax, %rax
  dda364:      	je	0xdda498 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6698>
  dda36a:      	movq	(%r12), %rsi
  dda36e:      	movq	%rbx, %rdi
  dda371:      	callq	*%rax
  dda373:      	testb	$0x20, (%r13)
  dda378:      	je	0xdda428 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda37e:      	movl	0x14(%r14), %eax
  dda382:      	testl	$0x20000, %eax          # imm = 0x20000
  dda387:      	jne	0xdda38f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a658f>
  dda389:      	movq	(%r12), %r15
  dda38d:      	jmp	0xdda3a2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65a2>
  dda38f:      	movq	0x98(%rbx), %rax
  dda396:      	movq	%rbx, %rdi
  dda399:      	callq	*0xf8(%rax)
  dda39f:      	movq	%rax, %r15
  dda3a2:      	movq	0xadfddf(%rip), %rcx    # 0x18ba188
  dda3a9:      	testq	%rcx, %rcx
  dda3ac:      	je	0xdda3dc <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65dc>
  dda3ae:      	movl	0x8(%rcx), %eax
  dda3b1:      	testl	%eax, %eax
  dda3b3:      	je	0xdda428 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda3b5:      	movq	(%rcx), %rcx
  dda3b8:      	xorl	%edx, %edx
  dda3ba:      	jmp	0xdda3c6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c6>
  dda3bc:      	nopl	(%rax)
  dda3c0:      	incl	%edx
  dda3c2:      	cmpl	%edx, %eax
  dda3c4:      	je	0xdda428 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6628>
  dda3c6:      	movslq	%edx, %rsi
  dda3c9:      	shlq	$0x4, %rsi
  dda3cd:      	cmpq	(%rcx,%rsi), %r15
  dda3d1:      	jb	0xdda3c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c0>
  dda3d3:      	addq	%rcx, %rsi
  dda3d6:      	cmpq	0x8(%rsi), %r15
  dda3da:      	ja	0xdda3c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65c0>
  dda3dc:      	movq	0x28(%r12), %r12
  dda3e1:      	leaq	0xa7a538(%rip), %rax    # 0x1854920
  dda3e8:      	movq	(%rax), %r13
  dda3eb:      	testq	%r13, %r13
  dda3ee:      	je	0xdda40a <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a660a>
  dda3f0:      	movq	%r13, %rdi
  dda3f3:      	movq	%r15, %rsi
  dda3f6:      	callq	*(%r13)
  dda3fa:      	cmpb	$0x0, (%rax)
  dda3fd:      	jne	0xdda411 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6611>
  dda3ff:      	movq	0x20(%r13), %r13
  dda403:      	testq	%r13, %r13
  dda406:      	jne	0xdda3f0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a65f0>
  dda408:      	jmp	0xdda411 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6611>
  dda40a:      	leaq	-0xb99af6(%rip), %rax   # 0x24091b
  dda411:      	leaq	-0xbdd329(%rip), %rdi   # 0x1fd0ef
  dda418:      	movq	%r12, %rsi
  dda41b:      	movq	%r15, %rdx
  dda41e:      	movq	%rax, %rcx
  dda421:      	xorl	%eax, %eax
  dda423:      	callq	0x11c9080 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x595280>
  dda428:      	cmpl	$0x0, 0xd4(%rbx)
  dda42f:      	jne	0xdda47d <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a667d>
  dda431:      	movq	%r14, %rax
  dda434:      	popq	%rbx
  dda435:      	popq	%r12
  dda437:      	popq	%r13
  dda439:      	popq	%r14
  dda43b:      	popq	%r15
  dda43d:      	xorl	%ecx, %ecx
  dda43f:      	xorl	%edi, %edi
  dda441:      	xorl	%edx, %edx
  dda443:      	xorl	%esi, %esi
  dda445:      	retq
  dda446:      	leaq	0xadf32d(%rip), %rcx    # 0x18b977a
  dda44d:      	cmpw	$0x0, (%rcx)
  dda451:      	je	0xdda322 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda457:      	testb	$-0x80, 0x1(%r13)
  dda45c:      	je	0xdda322 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda462:      	leaq	-0xaeebed(%rip), %rdi   # 0x2eb87c
  dda469:      	movq	%r14, %rsi
  dda46c:      	movl	%eax, %edx
  dda46e:      	xorl	%eax, %eax
  dda470:      	callq	0x11c9080 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x595280>
  dda475:      	movl	(%r15), %eax
  dda478:      	jmp	0xdda322 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6522>
  dda47d:      	cmpl	$-0x1, 0x2d0(%rbx)
  dda484:      	jne	0xdda431 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6631>
  dda486:      	movl	$0x10002, 0x2d0(%rbx)   # imm = 0x10002
  dda490:      	movq	%rbx, %rdi
  dda493:      	callq	0xddbd80 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a7f80>
  dda498:      	leaq	-0xa72225(%rip), %rdi   # 0x36827a
  dda49f:      	leaq	-0xb70968(%rip), %rsi   # 0x269b3e
  dda4a6:      	leaq	-0xbba114(%rip), %rcx   # 0x220399
  dda4ad:      	movl	$0x202, %edx            # imm = 0x202
  dda4b2:      	callq	0x16cb0e0 <__assert_fail@plt>
