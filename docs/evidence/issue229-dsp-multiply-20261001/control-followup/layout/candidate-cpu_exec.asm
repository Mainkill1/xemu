
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  dda6c0:      	pushq	%rbp
  dda6c1:      	pushq	%r15
  dda6c3:      	pushq	%r14
  dda6c5:      	pushq	%rbx
  dda6c6:      	subq	$0x28, %rsp
  dda6ca:      	movq	%rdi, %rbx
  dda6cd:      	movq	%fs:0x28, %rax
  dda6d6:      	movq	%rax, 0x20(%rsp)
  dda6db:      	vxorps	%xmm0, %xmm0, %xmm0
  dda6df:      	vmovaps	%xmm0, (%rsp)
  dda6e4:      	movq	$0x0, 0x10(%rsp)
  dda6ed:      	movq	%fs:0x0, %rax
  dda6f6:      	leaq	-0x310(%rax), %rax
  dda6fd:      	movq	%rbx, (%rax)
  dda700:      	cmpl	$0x0, 0x2cc(%rbx)
  dda707:      	je	0xdda734 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6934>
  dda709:      	movq	0x98(%rbx), %rax
  dda710:      	movq	0x150(%rax), %rax
  dda717:      	movq	%rbx, %rdi
  dda71a:      	callq	*0x68(%rax)
  dda71d:      	movl	$0x10003, %ebp          # imm = 0x10003
  dda722:      	testb	%al, %al
  dda724:      	je	0xdda901 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda72a:      	movl	$0x0, 0x2cc(%rbx)
  dda734:      	callq	0x11c7b60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x593d60>
  dda739:      	movl	0xc(%rax), %ecx
  dda73c:      	leal	0x1(%rcx), %edx
  dda73f:      	movl	%edx, 0xc(%rax)
  dda742:      	testl	%ecx, %ecx
  dda744:      	jne	0xdda75b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a695b>
  dda746:      	leaq	0xa6af73(%rip), %rcx    # 0x18456c0
  dda74d:      	movq	(%rcx), %rcx
  dda750:      	movl	%ecx, %ecx
  dda752:      	movq	%rcx, (%rax)
  dda755:      	lock
  dda756:      	orl	$0x0, -0x40(%rsp)
  dda75b:      	movq	0x98(%rbx), %rax
  dda762:      	movq	0x150(%rax), %rax
  dda769:      	movq	0x30(%rax), %rax
  dda76d:      	testq	%rax, %rax
  dda770:      	je	0xdda777 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6977>
  dda772:      	movq	%rbx, %rdi
  dda775:      	callq	*%rax
  dda777:      	leaq	0xac5363(%rip), %r15    # 0x189fae1
  dda77e:      	cmpb	$0x1, (%r15)
  dda782:      	jne	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda788:      	movl	$0x3, %edi
  dda78d:      	callq	0x11d74c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x5a36c0>
  dda792:      	movq	%rax, %r14
  dda795:      	movq	%rax, 0x10(%rsp)
  dda79a:      	callq	0xd07080 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0xd3280>
  dda79f:      	subq	%r14, %rax
  dda7a2:      	movq	%rax, (%rsp)
  dda7a6:      	movzwl	0x4b50(%rbx), %ecx
  dda7ad:      	addq	0xe0(%rbx), %rcx
  dda7b4:      	movq	%rcx, 0x8(%rsp)
  dda7b9:      	cmpq	0xab5248(%rip), %rax    # 0x188fa08
  dda7c0:      	jl	0xdda7d6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69d6>
  dda7c2:      	cmpq	0xab5247(%rip), %rax    # 0x188fa10
  dda7c9:      	jg	0xdda7e6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69e6>
  dda7cb:      	cmpb	$0x1, (%r15)
  dda7cf:      	je	0xdda7f7 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69f7>
  dda7d1:      	jmp	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda7d6:      	movq	%rax, 0xab522b(%rip)    # 0x188fa08
  dda7dd:      	cmpq	0xab522c(%rip), %rax    # 0x188fa10
  dda7e4:      	jle	0xdda7cb <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69cb>
  dda7e6:      	movq	%rax, 0xab5223(%rip)    # 0x188fa10
  dda7ed:      	cmpb	$0x1, (%r15)
  dda7f1:      	jne	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda7f7:      	movq	%r14, %rcx
  dda7fa:      	subq	0xab521f(%rip), %rcx    # 0x188fa20
  dda801:      	cmpq	$0x77359400, %rcx       # imm = 0x77359400
  dda808:      	jl	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda80e:      	cmpl	$0x63, 0xab5213(%rip)   # 0x188fa28
  dda815:      	jg	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda81b:      	negq	%rax
  dda81e:      	vxorps	%xmm15, %xmm15, %xmm15
  dda823:      	vcvtsi2ss	%rax, %xmm15, %xmm0
  dda828:      	vdivss	-0xbeeadc(%rip), %xmm0, %xmm0 # 0x1ebd54
  dda830:      	vmovss	0xab51e0(%rip), %xmm1   # 0x188fa18
  dda838:      	vucomiss	%xmm1, %xmm0
  dda83c:      	ja	0xdda854 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6a54>
  dda83e:      	vcvtss2sd	%xmm0, %xmm0, %xmm0
  dda842:      	vcvtss2sd	%xmm1, %xmm1, %xmm1
  dda846:      	vaddsd	-0xa62006(%rip), %xmm1, %xmm1 # 0x378848
  dda84e:      	vucomisd	%xmm0, %xmm1
  dda852:      	jbe	0xdda8af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda854:      	movabsq	$0x112e0be826d694b3, %rcx # imm = 0x112E0BE826D694B3
  dda85e:      	imulq	%rcx
  dda861:      	movq	%rdx, %rax
  dda864:      	shrq	$0x3f, %rax
  dda868:      	sarq	$0x1a, %rdx
  dda86c:      	addq	%rdx, %rax
  dda86f:      	incq	%rax
  dda872:      	vxorps	%xmm15, %xmm15, %xmm15
  dda877:      	vcvtsi2ss	%rax, %xmm15, %xmm1
  dda87c:      	vmovss	%xmm1, 0xab5194(%rip)   # 0x188fa18
  dda884:      	vaddss	-0xbef5fc(%rip), %xmm1, %xmm0 # 0x1eb290
  dda88c:      	vcvtss2sd	%xmm0, %xmm0, %xmm0
  dda890:      	vcvtss2sd	%xmm1, %xmm1, %xmm1
  dda894:      	leaq	-0xbc60b9(%rip), %rdi   # 0x2147e2
  dda89b:      	movb	$0x2, %al
  dda89d:      	callq	0x11c1b40 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x58dd40>
  dda8a2:      	incl	0xab5180(%rip)          # 0x188fa28
  dda8a8:      	movq	%r14, 0xab5171(%rip)    # 0x188fa20
  dda8af:      	movq	%rsp, %rsi
  dda8b2:      	movq	%rbx, %rdi
  dda8b5:      	callq	0xdda960 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b60>
  dda8ba:      	movl	%eax, %ebp
  dda8bc:      	movq	0x98(%rbx), %rax
  dda8c3:      	movq	0x150(%rax), %rax
  dda8ca:      	movq	0x38(%rax), %rax
  dda8ce:      	testq	%rax, %rax
  dda8d1:      	je	0xdda8d8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6ad8>
  dda8d3:      	movq	%rbx, %rdi
  dda8d6:      	callq	*%rax
  dda8d8:      	callq	0x11c7b60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x593d60>
  dda8dd:      	movl	0xc(%rax), %ecx
  dda8e0:      	testl	%ecx, %ecx
  dda8e2:      	je	0xdda93d <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b3d>
  dda8e4:      	decl	%ecx
  dda8e6:      	movl	%ecx, 0xc(%rax)
  dda8e9:      	jne	0xdda901 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda8eb:      	movq	$0x0, (%rax)
  dda8f2:      	lock
  dda8f3:      	orl	$0x0, -0x40(%rsp)
  dda8f8:      	movzbl	0x8(%rax), %ecx
  dda8fc:      	cmpb	$0x1, %cl
  dda8ff:      	je	0xdda926 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b26>
  dda901:      	movq	%fs:0x28, %rax
  dda90a:      	cmpq	0x20(%rsp), %rax
  dda90f:      	jne	0xdda938 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b38>
  dda911:      	movl	%ebp, %eax
  dda913:      	addq	$0x28, %rsp
  dda917:      	popq	%rbx
  dda918:      	popq	%r14
  dda91a:      	popq	%r15
  dda91c:      	popq	%rbp
  dda91d:      	xorl	%ecx, %ecx
  dda91f:      	xorl	%edi, %edi
  dda921:      	xorl	%edx, %edx
  dda923:      	xorl	%esi, %esi
  dda925:      	retq
  dda926:      	movb	$0x0, 0x8(%rax)
  dda92a:      	leaq	0xadf80f(%rip), %rdi    # 0x18ba140
  dda931:      	callq	0x11bed30 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x58af30>
  dda936:      	jmp	0xdda901 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda938:      	callq	0x16ce760 <__stack_chk_fail@plt>
  dda93d:      	leaq	-0xb7ebcc(%rip), %rdi   # 0x25bd78
  dda944:      	leaq	-0xb5c380(%rip), %rsi   # 0x27e5cb
  dda94b:      	leaq	-0xb12e8a(%rip), %rcx   # 0x2c7ac8
  dda952:      	movl	$0x65, %edx
  dda957:      	callq	0x16cb0e0 <__assert_fail@plt>
