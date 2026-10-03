
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/parent-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e60 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
 104be80:      	pushq	%rbp
 104be81:      	pushq	%r15
 104be83:      	pushq	%r14
 104be85:      	pushq	%r13
 104be87:      	pushq	%r12
 104be89:      	pushq	%rbx
 104be8a:      	pushq	%rax
 104be8b:      	movq	%rdi, %rbx
 104be8e:      	movl	0xc(%rdi), %edx
 104be91:      	leaq	0x872d08(%rip), %r14    # 0x18beba0
 104be98:      	cmpl	$0x0, (%r14)
 104be9c:      	jne	0x104c570 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418710>
 104bea2:      	movl	$0x0, 0x13440(%rbx)
 104beac:      	cmpl	$0x1000000, %edx        # imm = 0x1000000
 104beb2:      	jae	0x104c65f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187ff>
 104beb8:      	cmpl	$0x1000, %edx           # imm = 0x1000
 104bebe:      	jae	0x104c621 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187c1>
 104bec4:      	movl	%edx, %eax
 104bec6:      	movl	0x6190(%rbx,%rax,4), %eax
 104becd:      	cmpl	$0x1000000, %eax        # imm = 0x1000000
 104bed2:      	jae	0x104c640 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187e0>
 104bed8:      	movl	%eax, 0x133d8(%rbx)
 104bede:      	movl	$0x1, 0x133d4(%rbx)
 104bee8:      	movw	$0x2, 0x8(%rbx)
 104beee:      	cmpl	$0x0, (%r14)
 104bef2:      	jne	0x104c5ab <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41874b>
 104bef8:      	movl	0x133d8(%rbx), %r14d
 104beff:      	cmpq	$0xfffff, %r14          # imm = 0xFFFFF
 104bf06:      	ja	0x104c00e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181ae>
 104bf0c:      	movl	0xc(%rbx), %edx
 104bf0f:      	movq	0xa190(%rbx,%rdx,8), %r15
 104bf17:      	testq	%r15, %r15
 104bf1a:      	jne	0x104bfb5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418155>
 104bf20:      	movl	%r14d, %eax
 104bf23:      	shrl	$0x10, %eax
 104bf26:      	movl	%r14d, %ecx
 104bf29:      	shrl	$0x8, %ecx
 104bf2c:      	xorl	%eax, %ecx
 104bf2e:      	xorl	%r14d, %ecx
 104bf31:      	movzbl	%cl, %eax
 104bf34:      	shll	$0x4, %eax
 104bf37:      	leaq	0x86f5e2(%rip), %rcx    # 0x18bb520
 104bf3e:      	leaq	(%rcx,%rax), %r12
 104bf42:      	cmpl	%r14d, (%rax,%rcx)
 104bf46:      	jne	0x104bf52 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4180f2>
 104bf48:      	movq	0x8(%r12), %r15
 104bf4d:      	testq	%r15, %r15
 104bf50:      	jne	0x104bfad <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41814d>
 104bf52:      	movl	%r14d, (%r12)
 104bf56:      	leaq	0x7069c3(%rip), %r15    # 0x1752920
 104bf5d:      	xorl	%r13d, %r13d
 104bf60:      	leaq	0x86efd9(%rip), %rbp    # 0x18baf40
 104bf67:      	jmp	0x104bf84 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418124>
 104bf69:      	nopl	(%rax)
 104bf70:      	incq	%r13
 104bf73:      	addq	$0x28, %r15
 104bf77:      	cmpq	$0xbb, %r13
 104bf7e:      	je	0x104c5e2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418782>
 104bf84:      	movl	(%rbp,%r13,8), %eax
 104bf89:      	andl	%r14d, %eax
 104bf8c:      	cmpl	0x4(%rbp,%r13,8), %eax
 104bf91:      	jne	0x104bf70 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418110>
 104bf93:      	movq	0x20(%r15), %rax
 104bf97:      	testq	%rax, %rax
 104bf9a:      	je	0x104bfa5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418145>
 104bf9c:      	movl	%r14d, %edi
 104bf9f:      	callq	*%rax
 104bfa1:      	testb	%al, %al
 104bfa3:      	je	0x104bf70 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418110>
 104bfa5:      	movq	%r15, 0x8(%r12)
 104bfaa:      	movl	0xc(%rbx), %edx
 104bfad:      	movq	%r15, 0xa190(%rbx,%rdx,8)
 104bfb5:      	movq	0x18(%r15), %rax
 104bfb9:      	testq	%rax, %rax
 104bfbc:      	jne	0x104c021 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181c1>
 104bfbe:      	movl	$0x0, 0x133d4(%rbx)
 104bfc8:      	movl	0x133d8(%rbx), %ecx
 104bfce:      	leaq	-0xe1d279(%rip), %rsi   # 0x22ed5c
 104bfd5:      	movl	$0x1, %edi
 104bfda:      	xorl	%eax, %eax
 104bfdc:      	callq	0x16cfaa0 <__printf_chk@plt>
 104bfe1:      	addw	$0x64, 0x8(%rbx)
 104bfe6:      	cmpb	$0x1, 0x13444(%rbx)
 104bfed:      	jne	0x104c026 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181c6>
 104bfef:      	leaq	-0xd9599d(%rip), %rdi   # 0x2b6659
 104bff6:      	leaq	-0xd45bc6(%rip), %rsi   # 0x306437
 104bffd:      	leaq	-0xe12a59(%rip), %rcx   # 0x2395ab
 104c004:      	movl	$0x23, %edx
 104c009:      	callq	0x16cf870 <__assert_fail@plt>
 104c00e:      	shrl	$0x11, %r14d
 104c012:      	andl	$0x78, %r14d
 104c016:      	leaq	0x708643(%rip), %rax    # 0x1754660
 104c01d:      	movq	(%r14,%rax), %rax
 104c021:      	movq	%rbx, %rdi
 104c024:      	callq	*%rax
 104c026:      	movb	$0x1, %al
 104c028:      	cmpl	$0x0, 0x13390(%rbx)
 104c02f:      	je	0x104c090 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418230>
 104c031:      	cmpl	$0x0, 0x13394(%rbx)
 104c038:      	movl	0x10c(%rbx), %ecx
 104c03e:      	je	0x104c05a <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181fa>
 104c040:      	testl	%ecx, %ecx
 104c042:      	jne	0x104c04e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181ee>
 104c044:      	movl	$0x10000, 0x10c(%rbx)   # imm = 0x10000
 104c04e:      	movl	$0x0, 0x13394(%rbx)
 104c058:      	jmp	0x104c08e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41822e>
 104c05a:      	leal	-0x1(%rcx), %edx
 104c05d:      	movzwl	%dx, %edx
 104c060:      	movl	%edx, 0x10c(%rbx)
 104c066:      	cmpw	$0x1, %cx
 104c06a:      	jne	0x104c084 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418224>
 104c06c:      	movl	$0x0, 0x13390(%rbx)
 104c076:      	movl	0xd0(%rbx), %ecx
 104c07c:      	movl	%ecx, 0x10c(%rbx)
 104c082:      	jmp	0x104c090 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418230>
 104c084:      	movl	$0x0, 0x133d4(%rbx)
 104c08e:      	xorl	%eax, %eax
 104c090:      	movl	0xc(%rbx), %esi
 104c093:      	movl	0xf4(%rbx), %edx
 104c099:      	addl	0x133d4(%rbx), %esi
 104c09f:      	movl	%esi, 0xc(%rbx)
 104c0a2:      	testw	%dx, %dx
 104c0a5:      	jns	0x104c201 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a1>
 104c0ab:      	movl	0x108(%rbx), %ecx
 104c0b1:      	incl	%ecx
 104c0b3:      	cmpl	%ecx, %esi
 104c0b5:      	jne	0x104c201 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a1>
 104c0bb:      	movl	0x10c(%rbx), %ecx
 104c0c1:      	leal	-0x1(%rcx), %edi
 104c0c4:      	movzwl	%di, %edi
 104c0c7:      	movl	%edi, 0x10c(%rbx)
 104c0cd:      	cmpw	$0x1, %cx
 104c0d1:      	jne	0x104c2c2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418462>
 104c0d7:      	movl	0xfc(%rbx), %r8d
 104c0de:      	movl	%r8d, %edi
 104c0e1:      	andl	$0x10, %edi
 104c0e4:      	movl	%r8d, %ecx
 104c0e7:      	andl	$0xf, %ecx
 104c0ea:      	decl	%ecx
 104c0ec:      	shrl	$0x4, %edi
 104c0ef:      	testb	$0x10, %cl
 104c0f2:      	sete	%r9b
 104c0f6:      	orb	%dil, %r9b
 104c0f9:      	jne	0x104c12c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182cc>
 104c0fb:      	cmpw	$-0x1, 0x133a8(%rbx)
 104c103:      	je	0x104c11f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182bf>
 104c105:      	cmpw	$0x0, 0x133b0(%rbx)
 104c10d:      	jne	0x104c11f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182bf>
 104c10f:      	movw	$0x1, 0x133b0(%rbx)
 104c118:      	incw	0x1339e(%rbx)
 104c11f:      	cmpb	$0x1, 0x13444(%rbx)
 104c126:      	je	0x104c67e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41881e>
 104c12c:      	andl	$0x30, %r8d
 104c130:      	movl	%ecx, %edi
 104c132:      	andl	$0x3f, %edi
 104c135:      	orl	%r8d, %edi
 104c138:      	movl	%edi, 0xfc(%rbx)
 104c13e:      	andl	$0xf, %ecx
 104c141:      	movl	0x110(%rbx,%rcx,4), %r8d
 104c149:      	movl	%r8d, 0x100(%rbx)
 104c150:      	movl	$0x8000, %r10d          # imm = 0x8000
 104c156:      	andl	0x104(%rbx), %r10d
 104c15d:      	movl	0x150(%rbx,%rcx,4), %r9d
 104c165:      	movl	%r9d, 0x104(%rbx)
 104c16c:      	andl	$0x7f, %edx
 104c16f:      	orl	%r10d, %edx
 104c172:      	movl	%edx, 0xf4(%rbx)
 104c178:      	movl	%edi, %r10d
 104c17b:      	andl	$0x10, %r10d
 104c17f:      	decl	%ecx
 104c181:      	shrl	$0x4, %r10d
 104c185:      	testb	$0x10, %cl
 104c188:      	sete	%r11b
 104c18c:      	orb	%r10b, %r11b
 104c18f:      	jne	0x104c1c2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418362>
 104c191:      	cmpw	$-0x1, 0x133a8(%rbx)
 104c199:      	je	0x104c1b5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418355>
 104c19b:      	cmpw	$0x0, 0x133b0(%rbx)
 104c1a3:      	jne	0x104c1b5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418355>
 104c1a5:      	movw	$0x1, 0x133b0(%rbx)
 104c1ae:      	incw	0x1339e(%rbx)
 104c1b5:      	cmpb	$0x1, 0x13444(%rbx)
 104c1bc:      	je	0x104c67e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41881e>
 104c1c2:      	andl	$-0x10, %edi
 104c1c5:      	movl	%ecx, %r10d
 104c1c8:      	andl	$0x3f, %r10d
 104c1cc:      	orl	%edi, %r10d
 104c1cf:      	movl	%r10d, 0xfc(%rbx)
 104c1d6:      	andl	$0xf, %ecx
 104c1d9:      	movl	%r8d, 0x108(%rbx)
 104c1e0:      	movl	%r9d, 0x10c(%rbx)
 104c1e7:      	movl	0x110(%rbx,%rcx,4), %edi
 104c1ee:      	movl	%edi, 0x100(%rbx)
 104c1f4:      	movl	0x150(%rbx,%rcx,4), %ecx
 104c1fb:      	movl	%ecx, 0x104(%rbx)
 104c201:      	testb	%al, %al
 104c203:      	je	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c209:      	cmpw	$0x1, 0x13398(%rbx)
 104c211:      	jne	0x104c243 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183e3>
 104c213:      	movzwl	0x133a2(%rbx), %eax
 104c21a:      	cmpq	$0x5, %rax
 104c21e:      	ja	0x104c243 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183e3>
 104c220:      	leaq	-0xc479ef(%rip), %rcx   # 0x404838
 104c227:      	movslq	(%rcx,%rax,4), %rax
 104c22b:      	addq	%rcx, %rax
 104c22e:      	jmpq	*%rax
 104c230:      	movw	$0xffff, 0x1339c(%rbx)  # imm = 0xFFFF
 104c239:      	movl	$0xffff0000, 0x13398(%rbx) # imm = 0xFFFF0000
 104c243:      	movzwl	0x1339e(%rbx), %eax
 104c24a:      	testw	%ax, %ax
 104c24d:      	je	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c253:      	shrl	$0x8, %edx
 104c256:      	andl	$0x3, %edx
 104c259:      	cmpw	$0x1, 0x133ac(%rbx)
 104c261:      	jne	0x104c2d8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418478>
 104c263:      	movswl	0x133a4(%rbx), %r8d
 104c26b:      	xorl	%edi, %edi
 104c26d:      	cmpl	$0x3, %r8d
 104c271:      	je	0x104c38b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c277:      	cmpl	%r8d, %edx
 104c27a:      	setle	%cl
 104c27d:      	testw	%r8w, %r8w
 104c281:      	setns	%sil
 104c285:      	xorl	%edi, %edi
 104c287:      	testb	%cl, %sil
 104c28a:      	movl	$0xffff, %ecx           # imm = 0xFFFF
 104c28f:      	cmovnel	%edi, %ecx
 104c292:      	movl	$0xffffffff, %esi       # imm = 0xFFFFFFFF
 104c297:      	cmovnel	%r8d, %esi
 104c29b:      	cmpw	$0x1, 0x133ae(%rbx)
 104c2a3:      	je	0x104c2ec <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41848c>
 104c2a5:      	cmpw	$0x1, 0x133b0(%rbx)
 104c2ad:      	je	0x104c32a <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4184ca>
 104c2af:      	cmpw	$0x1, 0x133b2(%rbx)
 104c2b7:      	je	0x104c364 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418504>
 104c2bd:      	jmp	0x104c381 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c2c2:      	movl	0x100(%rbx), %esi
 104c2c8:      	movl	%esi, 0xc(%rbx)
 104c2cb:      	testb	%al, %al
 104c2cd:      	jne	0x104c209 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a9>
 104c2d3:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c2d8:      	movl	$0xffffffff, %esi       # imm = 0xFFFFFFFF
 104c2dd:      	movl	$0xffff, %ecx           # imm = 0xFFFF
 104c2e2:      	cmpw	$0x1, 0x133ae(%rbx)
 104c2ea:      	jne	0x104c2a5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418445>
 104c2ec:      	movswl	0x133a6(%rbx), %r8d
 104c2f4:      	movl	$0x1, %edi
 104c2f9:      	cmpl	$0x3, %r8d
 104c2fd:      	je	0x104c38b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c303:      	cmpl	%r8d, %edx
 104c306:      	setle	%dil
 104c30a:      	cmpl	%r8d, %esi
 104c30d:      	setl	%r9b
 104c311:      	testb	%r9b, %dil
 104c314:      	movl	$0x1, %edi
 104c319:      	cmovnel	%edi, %ecx
 104c31c:      	cmovnel	%r8d, %esi
 104c320:      	cmpw	$0x1, 0x133b0(%rbx)
 104c328:      	jne	0x104c2af <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41844f>
 104c32a:      	movswl	0x133a8(%rbx), %r8d
 104c332:      	movl	$0x2, %edi
 104c337:      	cmpl	$0x3, %r8d
 104c33b:      	je	0x104c38b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c33d:      	cmpl	%r8d, %edx
 104c340:      	setle	%dil
 104c344:      	cmpl	%r8d, %esi
 104c347:      	setl	%r9b
 104c34b:      	testb	%r9b, %dil
 104c34e:      	movl	$0x2, %edi
 104c353:      	cmovnel	%edi, %ecx
 104c356:      	cmovnel	%r8d, %esi
 104c35a:      	cmpw	$0x1, 0x133b2(%rbx)
 104c362:      	jne	0x104c381 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c364:      	movswl	0x133aa(%rbx), %r8d
 104c36c:      	movl	$0x3, %edi
 104c371:      	cmpl	$0x3, %r8d
 104c375:      	je	0x104c38b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c377:      	cmpl	%r8d, %edx
 104c37a:      	jg	0x104c381 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c37c:      	cmpl	%r8d, %esi
 104c37f:      	jl	0x104c38b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c381:      	movl	%ecx, %edi
 104c383:      	cmpl	$0xffff, %ecx           # imm = 0xFFFF
 104c389:      	je	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c38b:      	movl	%edi, %ecx
 104c38d:      	movw	$0x0, 0x133ac(%rbx,%rcx,2)
 104c397:      	decl	%eax
 104c399:      	movw	%ax, 0x1339e(%rbx)
 104c3a0:      	movzwl	0x133a4(%rbx,%rcx,2), %eax
 104c3a8:      	cmpw	$0x2, %ax
 104c3ac:      	movl	$0x2, %ecx
 104c3b1:      	cmovll	%eax, %ecx
 104c3b4:      	incl	%ecx
 104c3b6:      	shll	$0x4, %edi
 104c3b9:      	leaq	0x708e60(%rip), %rax    # 0x1755220
 104c3c0:      	movzwl	0x2(%rdi,%rax), %eax
 104c3c5:      	movw	%ax, 0x1339a(%rbx)
 104c3cc:      	movw	$0x5, 0x133a2(%rbx)
 104c3d5:      	movw	$0x1, 0x13398(%rbx)
 104c3de:      	movw	%cx, 0x133a0(%rbx)
 104c3e5:      	movzwl	0x8(%rbx), %eax
 104c3e9:      	addl	%eax, 0x133d0(%rbx)
 104c3ef:      	addq	$0x8, %rsp
 104c3f3:      	popq	%rbx
 104c3f4:      	popq	%r12
 104c3f6:      	popq	%r13
 104c3f8:      	popq	%r14
 104c3fa:      	popq	%r15
 104c3fc:      	popq	%rbp
 104c3fd:      	xorl	%eax, %eax
 104c3ff:      	xorl	%ecx, %ecx
 104c401:      	xorl	%edi, %edi
 104c403:      	xorl	%edx, %edx
 104c405:      	xorl	%esi, %esi
 104c407:      	xorl	%r8d, %r8d
 104c40a:      	xorl	%r9d, %r9d
 104c40d:      	xorl	%r10d, %r10d
 104c410:      	xorl	%r11d, %r11d
 104c413:      	retq
 104c414:      	movw	%si, 0x1339c(%rbx)
 104c41b:      	movzwl	0x1339a(%rbx), %eax
 104c422:      	movl	%eax, 0xc(%rbx)
 104c425:      	cmpl	$0x1000, %eax           # imm = 0x1000
 104c42a:      	jae	0x104c621 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187c1>
 104c430:      	movl	0x6190(%rbx,%rax,4), %eax
 104c437:      	cmpl	$0x1000000, %eax        # imm = 0x1000000
 104c43c:      	jae	0x104c640 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187e0>
 104c442:      	movl	%eax, %ecx
 104c444:      	andl	$0xfff000, %ecx         # imm = 0xFFF000
 104c44a:      	cmpl	$0xd0000, %ecx          # imm = 0xD0000
 104c450:      	setne	%cl
 104c453:      	andl	$0xffc0ff, %eax         # imm = 0xFFC0FF
 104c458:      	cmpl	$0xbc080, %eax          # imm = 0xBC080
 104c45d:      	setne	%dil
 104c461:      	movw	$0x3, %ax
 104c465:      	testb	%dil, %cl
 104c468:      	jne	0x104c4a4 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418644>
 104c46a:      	movw	$0x2, 0x13398(%rbx)
 104c473:      	movzwl	%si, %esi
 104c476:      	movq	%rbx, %rdi
 104c479:      	callq	0x10598c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x425a60>
 104c47e:      	movl	$0xffff70ff, %eax       # imm = 0xFFFF70FF
 104c483:      	andl	0xf4(%rbx), %eax
 104c489:      	movzwl	0x133a0(%rbx), %ecx
 104c490:      	shll	$0x8, %ecx
 104c493:      	orl	%eax, %ecx
 104c495:      	movl	%ecx, 0xf4(%rbx)
 104c49b:      	movzwl	0x133a2(%rbx), %eax
 104c4a2:      	decl	%eax
 104c4a4:      	movw	%ax, 0x133a2(%rbx)
 104c4ab:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c4b0:      	movzwl	0x1339a(%rbx), %eax
 104c4b7:      	addl	$0x2, %eax
 104c4ba:      	cmpl	%eax, %esi
 104c4bc:      	jne	0x104c4c8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418668>
 104c4be:      	movzwl	0x1339c(%rbx), %eax
 104c4c5:      	movl	%eax, 0xc(%rbx)
 104c4c8:      	movw	$0x1, 0x133a2(%rbx)
 104c4d1:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c4d6:      	movzwl	0x1339a(%rbx), %eax
 104c4dd:      	incl	%eax
 104c4df:      	cmpl	%eax, %esi
 104c4e1:      	jne	0x104c548 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4186e8>
 104c4e3:      	movq	%rbx, %rdi
 104c4e6:      	callq	0x104c6a0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418840>
 104c4eb:      	movl	%eax, %ecx
 104c4ed:      	andl	$0xfff000, %ecx         # imm = 0xFFF000
 104c4f3:      	cmpl	$0xd0000, %ecx          # imm = 0xD0000
 104c4f9:      	setne	%cl
 104c4fc:      	andl	$0xffc0ff, %eax         # imm = 0xFFC0FF
 104c501:      	cmpl	$0xbc080, %eax          # imm = 0xBC080
 104c506:      	setne	%al
 104c509:      	testb	%al, %cl
 104c50b:      	jne	0x104c548 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4186e8>
 104c50d:      	movw	$0x2, 0x13398(%rbx)
 104c516:      	movzwl	0x1339c(%rbx), %esi
 104c51d:      	movl	0xf4(%rbx), %edx
 104c523:      	movq	%rbx, %rdi
 104c526:      	callq	0x10598c0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x425a60>
 104c52b:      	movl	$0xffff70ff, %eax       # imm = 0xFFFF70FF
 104c530:      	andl	0xf4(%rbx), %eax
 104c536:      	movzwl	0x133a0(%rbx), %ecx
 104c53d:      	shll	$0x8, %ecx
 104c540:      	orl	%eax, %ecx
 104c542:      	movl	%ecx, 0xf4(%rbx)
 104c548:      	decw	0x133a2(%rbx)
 104c54f:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c554:      	movw	$0x0, 0x133a2(%rbx)
 104c55d:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c562:      	movw	$0x4, 0x133a2(%rbx)
 104c56b:      	jmp	0x104c3e5 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c570:      	leaq	0x87196d(%rip), %rax    # 0x18bdee4
 104c577:      	cmpw	$0x0, (%rax)
 104c57b:      	je	0x104bea2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c581:      	leaq	0x8723b0(%rip), %rax    # 0x18be938
 104c588:      	testb	$-0x80, 0x1(%rax)
 104c58c:      	je	0x104bea2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c592:      	movzbl	(%rbx), %esi
 104c595:      	leaq	-0xe34479(%rip), %rdi   # 0x218123
 104c59c:      	xorl	%eax, %eax
 104c59e:      	callq	0x11cd810 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x5999b0>
 104c5a3:      	movl	0xc(%rbx), %edx
 104c5a6:      	jmp	0x104bea2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c5ab:      	leaq	0x871934(%rip), %rax    # 0x18bdee6
 104c5b2:      	cmpw	$0x0, (%rax)
 104c5b6:      	je	0x104bef8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c5bc:      	movq	%rbx, %rdi
 104c5bf:      	callq	0x104c710 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4188b0>
 104c5c4:      	testw	%ax, %ax
 104c5c7:      	je	0x104bef8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c5cd:      	movq	%rbx, %rdi
 104c5d0:      	callq	0x104c9d0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418b70>
 104c5d5:      	movq	%rax, %rdi
 104c5d8:      	callq	0x104cb20 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418cc0>
 104c5dd:      	jmp	0x104bef8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c5e2:      	movq	0x7215cf(%rip), %rax    # 0x176dbb8
 104c5e9:      	movq	(%rax), %rdi
 104c5ec:      	leaq	-0xd08f84(%rip), %rdx   # 0x34366f
 104c5f3:      	movl	$0x1, %esi
 104c5f8:      	movl	%r14d, %ecx
 104c5fb:      	xorl	%eax, %eax
 104c5fd:      	callq	0x16cf960 <__fprintf_chk@plt>
 104c602:      	leaq	-0xe40f29(%rip), %rdi   # 0x20b6e0
 104c609:      	leaq	-0xd090f7(%rip), %rsi   # 0x343519
 104c610:      	leaq	-0xd4612b(%rip), %rcx   # 0x3064ec
 104c617:      	movl	$0x1a3, %edx            # imm = 0x1A3
 104c61c:      	callq	0x16cf870 <__assert_fail@plt>
 104c621:      	leaq	-0xdb1f4b(%rip), %rdi   # 0x29a6dd
 104c628:      	leaq	-0xd09116(%rip), %rsi   # 0x343519
 104c62f:      	leaq	-0xd14747(%rip), %rcx   # 0x337eef
 104c636:      	movl	$0x37b, %edx            # imm = 0x37B
 104c63b:      	callq	0x16cf870 <__assert_fail@plt>
 104c640:      	leaq	-0xe40f49(%rip), %rdi   # 0x20b6fe
 104c647:      	leaq	-0xd09135(%rip), %rsi   # 0x343519
 104c64e:      	leaq	-0xd14766(%rip), %rcx   # 0x337eef
 104c655:      	movl	$0x37d, %edx            # imm = 0x37D
 104c65a:      	callq	0x16cf870 <__assert_fail@plt>
 104c65f:      	leaq	-0xd80eb2(%rip), %rdi   # 0x2cb7b4
 104c666:      	leaq	-0xd09154(%rip), %rsi   # 0x343519
 104c66d:      	leaq	-0xd14785(%rip), %rcx   # 0x337eef
 104c674:      	movl	$0x37a, %edx            # imm = 0x37A
 104c679:      	callq	0x16cf870 <__assert_fail@plt>
 104c67e:      	leaq	-0xddfe0a(%rip), %rdi   # 0x26c87b
 104c685:      	leaq	-0xd09173(%rip), %rsi   # 0x343519
 104c68c:      	leaq	-0xddfe01(%rip), %rcx   # 0x26c892
 104c693:      	movl	$0x455, %edx            # imm = 0x455
 104c698:      	callq	0x16cf870 <__assert_fail@plt>
