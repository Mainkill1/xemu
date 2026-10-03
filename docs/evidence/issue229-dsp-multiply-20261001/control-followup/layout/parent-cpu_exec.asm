
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
  dda720:      	pushq	%rbp
  dda721:      	pushq	%r15
  dda723:      	pushq	%r14
  dda725:      	pushq	%rbx
  dda726:      	subq	$0x28, %rsp
  dda72a:      	movq	%rdi, %rbx
  dda72d:      	movq	%fs:0x28, %rax
  dda736:      	movq	%rax, 0x20(%rsp)
  dda73b:      	vxorps	%xmm0, %xmm0, %xmm0
  dda73f:      	vmovaps	%xmm0, (%rsp)
  dda744:      	movq	$0x0, 0x10(%rsp)
  dda74d:      	movq	%fs:0x0, %rax
  dda756:      	leaq	-0x310(%rax), %rax
  dda75d:      	movq	%rbx, (%rax)
  dda760:      	cmpl	$0x0, 0x2cc(%rbx)
  dda767:      	je	0xdda794 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6934>
  dda769:      	movq	0x98(%rbx), %rax
  dda770:      	movq	0x150(%rax), %rax
  dda777:      	movq	%rbx, %rdi
  dda77a:      	callq	*0x68(%rax)
  dda77d:      	movl	$0x10003, %ebp          # imm = 0x10003
  dda782:      	testb	%al, %al
  dda784:      	je	0xdda961 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda78a:      	movl	$0x0, 0x2cc(%rbx)
  dda794:      	callq	0x11cc2f0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x598490>
  dda799:      	movl	0xc(%rax), %ecx
  dda79c:      	leal	0x1(%rcx), %edx
  dda79f:      	movl	%edx, 0xc(%rax)
  dda7a2:      	testl	%ecx, %ecx
  dda7a4:      	jne	0xdda7bb <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a695b>
  dda7a6:      	leaq	0xa6f6a3(%rip), %rcx    # 0x1849e50
  dda7ad:      	movq	(%rcx), %rcx
  dda7b0:      	movl	%ecx, %ecx
  dda7b2:      	movq	%rcx, (%rax)
  dda7b5:      	lock
  dda7b6:      	orl	$0x0, -0x40(%rsp)
  dda7bb:      	movq	0x98(%rbx), %rax
  dda7c2:      	movq	0x150(%rax), %rax
  dda7c9:      	movq	0x30(%rax), %rax
  dda7cd:      	testq	%rax, %rax
  dda7d0:      	je	0xdda7d7 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6977>
  dda7d2:      	movq	%rbx, %rdi
  dda7d5:      	callq	*%rax
  dda7d7:      	leaq	0xac9aa3(%rip), %r15    # 0x18a4281
  dda7de:      	cmpb	$0x1, (%r15)
  dda7e2:      	jne	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda7e8:      	movl	$0x3, %edi
  dda7ed:      	callq	0x11dbc50 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x5a7df0>
  dda7f2:      	movq	%rax, %r14
  dda7f5:      	movq	%rax, 0x10(%rsp)
  dda7fa:      	callq	0xd070e0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0xd3280>
  dda7ff:      	subq	%r14, %rax
  dda802:      	movq	%rax, (%rsp)
  dda806:      	movzwl	0x4b50(%rbx), %ecx
  dda80d:      	addq	0xe0(%rbx), %rcx
  dda814:      	movq	%rcx, 0x8(%rsp)
  dda819:      	cmpq	0xab9988(%rip), %rax    # 0x18941a8
  dda820:      	jl	0xdda836 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69d6>
  dda822:      	cmpq	0xab9987(%rip), %rax    # 0x18941b0
  dda829:      	jg	0xdda846 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69e6>
  dda82b:      	cmpb	$0x1, (%r15)
  dda82f:      	je	0xdda857 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69f7>
  dda831:      	jmp	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda836:      	movq	%rax, 0xab996b(%rip)    # 0x18941a8
  dda83d:      	cmpq	0xab996c(%rip), %rax    # 0x18941b0
  dda844:      	jle	0xdda82b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a69cb>
  dda846:      	movq	%rax, 0xab9963(%rip)    # 0x18941b0
  dda84d:      	cmpb	$0x1, (%r15)
  dda851:      	jne	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda857:      	movq	%r14, %rcx
  dda85a:      	subq	0xab995f(%rip), %rcx    # 0x18941c0
  dda861:      	cmpq	$0x77359400, %rcx       # imm = 0x77359400
  dda868:      	jl	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda86e:      	cmpl	$0x63, 0xab9953(%rip)   # 0x18941c8
  dda875:      	jg	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda87b:      	negq	%rax
  dda87e:      	vxorps	%xmm15, %xmm15, %xmm15
  dda883:      	vcvtsi2ss	%rax, %xmm15, %xmm0
  dda888:      	vdivss	-0xbeeb3c(%rip), %xmm0, %xmm0 # 0x1ebd54
  dda890:      	vmovss	0xab9920(%rip), %xmm1   # 0x18941b8
  dda898:      	vucomiss	%xmm1, %xmm0
  dda89c:      	ja	0xdda8b4 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6a54>
  dda89e:      	vcvtss2sd	%xmm0, %xmm0, %xmm0
  dda8a2:      	vcvtss2sd	%xmm1, %xmm1, %xmm1
  dda8a6:      	vaddsd	-0xa62066(%rip), %xmm1, %xmm1 # 0x378848
  dda8ae:      	vucomisd	%xmm0, %xmm1
  dda8b2:      	jbe	0xdda90f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6aaf>
  dda8b4:      	movabsq	$0x112e0be826d694b3, %rcx # imm = 0x112E0BE826D694B3
  dda8be:      	imulq	%rcx
  dda8c1:      	movq	%rdx, %rax
  dda8c4:      	shrq	$0x3f, %rax
  dda8c8:      	sarq	$0x1a, %rdx
  dda8cc:      	addq	%rdx, %rax
  dda8cf:      	incq	%rax
  dda8d2:      	vxorps	%xmm15, %xmm15, %xmm15
  dda8d7:      	vcvtsi2ss	%rax, %xmm15, %xmm1
  dda8dc:      	vmovss	%xmm1, 0xab98d4(%rip)   # 0x18941b8
  dda8e4:      	vaddss	-0xbef65c(%rip), %xmm1, %xmm0 # 0x1eb290
  dda8ec:      	vcvtss2sd	%xmm0, %xmm0, %xmm0
  dda8f0:      	vcvtss2sd	%xmm1, %xmm1, %xmm1
  dda8f4:      	leaq	-0xbc60f6(%rip), %rdi   # 0x214805
  dda8fb:      	movb	$0x2, %al
  dda8fd:      	callq	0x11c62d0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x592470>
  dda902:      	incl	0xab98c0(%rip)          # 0x18941c8
  dda908:      	movq	%r14, 0xab98b1(%rip)    # 0x18941c0
  dda90f:      	movq	%rsp, %rsi
  dda912:      	movq	%rbx, %rdi
  dda915:      	callq	0xdda9c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b60>
  dda91a:      	movl	%eax, %ebp
  dda91c:      	movq	0x98(%rbx), %rax
  dda923:      	movq	0x150(%rax), %rax
  dda92a:      	movq	0x38(%rax), %rax
  dda92e:      	testq	%rax, %rax
  dda931:      	je	0xdda938 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6ad8>
  dda933:      	movq	%rbx, %rdi
  dda936:      	callq	*%rax
  dda938:      	callq	0x11cc2f0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x598490>
  dda93d:      	movl	0xc(%rax), %ecx
  dda940:      	testl	%ecx, %ecx
  dda942:      	je	0xdda99d <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b3d>
  dda944:      	decl	%ecx
  dda946:      	movl	%ecx, 0xc(%rax)
  dda949:      	jne	0xdda961 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda94b:      	movq	$0x0, (%rax)
  dda952:      	lock
  dda953:      	orl	$0x0, -0x40(%rsp)
  dda958:      	movzbl	0x8(%rax), %ecx
  dda95c:      	cmpb	$0x1, %cl
  dda95f:      	je	0xdda986 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b26>
  dda961:      	movq	%fs:0x28, %rax
  dda96a:      	cmpq	0x20(%rsp), %rax
  dda96f:      	jne	0xdda998 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b38>
  dda971:      	movl	%ebp, %eax
  dda973:      	addq	$0x28, %rsp
  dda977:      	popq	%rbx
  dda978:      	popq	%r14
  dda97a:      	popq	%r15
  dda97c:      	popq	%rbp
  dda97d:      	xorl	%ecx, %ecx
  dda97f:      	xorl	%edi, %edi
  dda981:      	xorl	%edx, %edx
  dda983:      	xorl	%esi, %esi
  dda985:      	retq
  dda986:      	movb	$0x0, 0x8(%rax)
  dda98a:      	leaq	0xae3f4f(%rip), %rdi    # 0x18be8e0
  dda991:      	callq	0x11c34c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x58f660>
  dda996:      	jmp	0xdda961 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x1a6b01>
  dda998:      	callq	0x16d2ef0 <__stack_chk_fail@plt>
  dda99d:      	leaq	-0xb7ec32(%rip), %rdi   # 0x25bd72
  dda9a4:      	leaq	-0xb5c3fe(%rip), %rsi   # 0x27e5ad
  dda9ab:      	leaq	-0xb12edf(%rip), %rcx   # 0x2c7ad3
  dda9b2:      	movl	$0x65, %edx
  dda9b7:      	callq	0x16cf870 <__assert_fail@plt>
