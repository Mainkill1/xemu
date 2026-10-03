
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/native-builds/candidate-linux/squashfs-root/usr/bin/xemu:	file format elf64-x86-64

Disassembly of section .text:

0000000000c33e00 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc>:
 104be20:      	pushq	%rbp
 104be21:      	pushq	%r15
 104be23:      	pushq	%r14
 104be25:      	pushq	%r13
 104be27:      	pushq	%r12
 104be29:      	pushq	%rbx
 104be2a:      	pushq	%rax
 104be2b:      	movq	%rdi, %rbx
 104be2e:      	movl	0xc(%rdi), %edx
 104be31:      	leaq	0x86e5c8(%rip), %r14    # 0x18ba400
 104be38:      	cmpl	$0x0, (%r14)
 104be3c:      	jne	0x104c510 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418710>
 104be42:      	movl	$0x0, 0x13440(%rbx)
 104be4c:      	cmpl	$0x1000000, %edx        # imm = 0x1000000
 104be52:      	jae	0x104c5ff <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187ff>
 104be58:      	cmpl	$0x1000, %edx           # imm = 0x1000
 104be5e:      	jae	0x104c5c1 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187c1>
 104be64:      	movl	%edx, %eax
 104be66:      	movl	0x6190(%rbx,%rax,4), %eax
 104be6d:      	cmpl	$0x1000000, %eax        # imm = 0x1000000
 104be72:      	jae	0x104c5e0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187e0>
 104be78:      	movl	%eax, 0x133d8(%rbx)
 104be7e:      	movl	$0x1, 0x133d4(%rbx)
 104be88:      	movw	$0x2, 0x8(%rbx)
 104be8e:      	cmpl	$0x0, (%r14)
 104be92:      	jne	0x104c54b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41874b>
 104be98:      	movl	0x133d8(%rbx), %r14d
 104be9f:      	cmpq	$0xfffff, %r14          # imm = 0xFFFFF
 104bea6:      	ja	0x104bfae <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181ae>
 104beac:      	movl	0xc(%rbx), %edx
 104beaf:      	movq	0xa190(%rbx,%rdx,8), %r15
 104beb7:      	testq	%r15, %r15
 104beba:      	jne	0x104bf55 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418155>
 104bec0:      	movl	%r14d, %eax
 104bec3:      	shrl	$0x10, %eax
 104bec6:      	movl	%r14d, %ecx
 104bec9:      	shrl	$0x8, %ecx
 104becc:      	xorl	%eax, %ecx
 104bece:      	xorl	%r14d, %ecx
 104bed1:      	movzbl	%cl, %eax
 104bed4:      	shll	$0x4, %eax
 104bed7:      	leaq	0x86aea2(%rip), %rcx    # 0x18b6d80
 104bede:      	leaq	(%rcx,%rax), %r12
 104bee2:      	cmpl	%r14d, (%rax,%rcx)
 104bee6:      	jne	0x104bef2 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4180f2>
 104bee8:      	movq	0x8(%r12), %r15
 104beed:      	testq	%r15, %r15
 104bef0:      	jne	0x104bf4d <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41814d>
 104bef2:      	movl	%r14d, (%r12)
 104bef6:      	leaq	0x702293(%rip), %r15    # 0x174e190
 104befd:      	xorl	%r13d, %r13d
 104bf00:      	leaq	0x86a899(%rip), %rbp    # 0x18b67a0
 104bf07:      	jmp	0x104bf24 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418124>
 104bf09:      	nopl	(%rax)
 104bf10:      	incq	%r13
 104bf13:      	addq	$0x28, %r15
 104bf17:      	cmpq	$0xbb, %r13
 104bf1e:      	je	0x104c582 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418782>
 104bf24:      	movl	(%rbp,%r13,8), %eax
 104bf29:      	andl	%r14d, %eax
 104bf2c:      	cmpl	0x4(%rbp,%r13,8), %eax
 104bf31:      	jne	0x104bf10 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418110>
 104bf33:      	movq	0x20(%r15), %rax
 104bf37:      	testq	%rax, %rax
 104bf3a:      	je	0x104bf45 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418145>
 104bf3c:      	movl	%r14d, %edi
 104bf3f:      	callq	*%rax
 104bf41:      	testb	%al, %al
 104bf43:      	je	0x104bf10 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418110>
 104bf45:      	movq	%r15, 0x8(%r12)
 104bf4a:      	movl	0xc(%rbx), %edx
 104bf4d:      	movq	%r15, 0xa190(%rbx,%rdx,8)
 104bf55:      	movq	0x18(%r15), %rax
 104bf59:      	testq	%rax, %rax
 104bf5c:      	jne	0x104bfc1 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181c1>
 104bf5e:      	movl	$0x0, 0x133d4(%rbx)
 104bf68:      	movl	0x133d8(%rbx), %ecx
 104bf6e:      	leaq	-0xe1d213(%rip), %rsi   # 0x22ed62
 104bf75:      	movl	$0x1, %edi
 104bf7a:      	xorl	%eax, %eax
 104bf7c:      	callq	0x16cb310 <__printf_chk@plt>
 104bf81:      	addw	$0x64, 0x8(%rbx)
 104bf86:      	cmpb	$0x1, 0x13444(%rbx)
 104bf8d:      	jne	0x104bfc6 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181c6>
 104bf8f:      	leaq	-0xd95948(%rip), %rdi   # 0x2b664e
 104bf96:      	leaq	-0xd45b71(%rip), %rsi   # 0x30642c
 104bf9d:      	leaq	-0xe129f3(%rip), %rcx   # 0x2395b1
 104bfa4:      	movl	$0x23, %edx
 104bfa9:      	callq	0x16cb0e0 <__assert_fail@plt>
 104bfae:      	shrl	$0x11, %r14d
 104bfb2:      	andl	$0x78, %r14d
 104bfb6:      	leaq	0x703f13(%rip), %rax    # 0x174fed0
 104bfbd:      	movq	(%r14,%rax), %rax
 104bfc1:      	movq	%rbx, %rdi
 104bfc4:      	callq	*%rax
 104bfc6:      	movb	$0x1, %al
 104bfc8:      	cmpl	$0x0, 0x13390(%rbx)
 104bfcf:      	je	0x104c030 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418230>
 104bfd1:      	cmpl	$0x0, 0x13394(%rbx)
 104bfd8:      	movl	0x10c(%rbx), %ecx
 104bfde:      	je	0x104bffa <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181fa>
 104bfe0:      	testl	%ecx, %ecx
 104bfe2:      	jne	0x104bfee <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4181ee>
 104bfe4:      	movl	$0x10000, 0x10c(%rbx)   # imm = 0x10000
 104bfee:      	movl	$0x0, 0x13394(%rbx)
 104bff8:      	jmp	0x104c02e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41822e>
 104bffa:      	leal	-0x1(%rcx), %edx
 104bffd:      	movzwl	%dx, %edx
 104c000:      	movl	%edx, 0x10c(%rbx)
 104c006:      	cmpw	$0x1, %cx
 104c00a:      	jne	0x104c024 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418224>
 104c00c:      	movl	$0x0, 0x13390(%rbx)
 104c016:      	movl	0xd0(%rbx), %ecx
 104c01c:      	movl	%ecx, 0x10c(%rbx)
 104c022:      	jmp	0x104c030 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418230>
 104c024:      	movl	$0x0, 0x133d4(%rbx)
 104c02e:      	xorl	%eax, %eax
 104c030:      	movl	0xc(%rbx), %esi
 104c033:      	movl	0xf4(%rbx), %edx
 104c039:      	addl	0x133d4(%rbx), %esi
 104c03f:      	movl	%esi, 0xc(%rbx)
 104c042:      	testw	%dx, %dx
 104c045:      	jns	0x104c1a1 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a1>
 104c04b:      	movl	0x108(%rbx), %ecx
 104c051:      	incl	%ecx
 104c053:      	cmpl	%ecx, %esi
 104c055:      	jne	0x104c1a1 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a1>
 104c05b:      	movl	0x10c(%rbx), %ecx
 104c061:      	leal	-0x1(%rcx), %edi
 104c064:      	movzwl	%di, %edi
 104c067:      	movl	%edi, 0x10c(%rbx)
 104c06d:      	cmpw	$0x1, %cx
 104c071:      	jne	0x104c262 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418462>
 104c077:      	movl	0xfc(%rbx), %r8d
 104c07e:      	movl	%r8d, %edi
 104c081:      	andl	$0x10, %edi
 104c084:      	movl	%r8d, %ecx
 104c087:      	andl	$0xf, %ecx
 104c08a:      	decl	%ecx
 104c08c:      	shrl	$0x4, %edi
 104c08f:      	testb	$0x10, %cl
 104c092:      	sete	%r9b
 104c096:      	orb	%dil, %r9b
 104c099:      	jne	0x104c0cc <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182cc>
 104c09b:      	cmpw	$-0x1, 0x133a8(%rbx)
 104c0a3:      	je	0x104c0bf <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182bf>
 104c0a5:      	cmpw	$0x0, 0x133b0(%rbx)
 104c0ad:      	jne	0x104c0bf <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4182bf>
 104c0af:      	movw	$0x1, 0x133b0(%rbx)
 104c0b8:      	incw	0x1339e(%rbx)
 104c0bf:      	cmpb	$0x1, 0x13444(%rbx)
 104c0c6:      	je	0x104c61e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41881e>
 104c0cc:      	andl	$0x30, %r8d
 104c0d0:      	movl	%ecx, %edi
 104c0d2:      	andl	$0x3f, %edi
 104c0d5:      	orl	%r8d, %edi
 104c0d8:      	movl	%edi, 0xfc(%rbx)
 104c0de:      	andl	$0xf, %ecx
 104c0e1:      	movl	0x110(%rbx,%rcx,4), %r8d
 104c0e9:      	movl	%r8d, 0x100(%rbx)
 104c0f0:      	movl	$0x8000, %r10d          # imm = 0x8000
 104c0f6:      	andl	0x104(%rbx), %r10d
 104c0fd:      	movl	0x150(%rbx,%rcx,4), %r9d
 104c105:      	movl	%r9d, 0x104(%rbx)
 104c10c:      	andl	$0x7f, %edx
 104c10f:      	orl	%r10d, %edx
 104c112:      	movl	%edx, 0xf4(%rbx)
 104c118:      	movl	%edi, %r10d
 104c11b:      	andl	$0x10, %r10d
 104c11f:      	decl	%ecx
 104c121:      	shrl	$0x4, %r10d
 104c125:      	testb	$0x10, %cl
 104c128:      	sete	%r11b
 104c12c:      	orb	%r10b, %r11b
 104c12f:      	jne	0x104c162 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418362>
 104c131:      	cmpw	$-0x1, 0x133a8(%rbx)
 104c139:      	je	0x104c155 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418355>
 104c13b:      	cmpw	$0x0, 0x133b0(%rbx)
 104c143:      	jne	0x104c155 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418355>
 104c145:      	movw	$0x1, 0x133b0(%rbx)
 104c14e:      	incw	0x1339e(%rbx)
 104c155:      	cmpb	$0x1, 0x13444(%rbx)
 104c15c:      	je	0x104c61e <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41881e>
 104c162:      	andl	$-0x10, %edi
 104c165:      	movl	%ecx, %r10d
 104c168:      	andl	$0x3f, %r10d
 104c16c:      	orl	%edi, %r10d
 104c16f:      	movl	%r10d, 0xfc(%rbx)
 104c176:      	andl	$0xf, %ecx
 104c179:      	movl	%r8d, 0x108(%rbx)
 104c180:      	movl	%r9d, 0x10c(%rbx)
 104c187:      	movl	0x110(%rbx,%rcx,4), %edi
 104c18e:      	movl	%edi, 0x100(%rbx)
 104c194:      	movl	0x150(%rbx,%rcx,4), %ecx
 104c19b:      	movl	%ecx, 0x104(%rbx)
 104c1a1:      	testb	%al, %al
 104c1a3:      	je	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c1a9:      	cmpw	$0x1, 0x13398(%rbx)
 104c1b1:      	jne	0x104c1e3 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183e3>
 104c1b3:      	movzwl	0x133a2(%rbx), %eax
 104c1ba:      	cmpq	$0x5, %rax
 104c1be:      	ja	0x104c1e3 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183e3>
 104c1c0:      	leaq	-0xc4798f(%rip), %rcx   # 0x404838
 104c1c7:      	movslq	(%rcx,%rax,4), %rax
 104c1cb:      	addq	%rcx, %rax
 104c1ce:      	jmpq	*%rax
 104c1d0:      	movw	$0xffff, 0x1339c(%rbx)  # imm = 0xFFFF
 104c1d9:      	movl	$0xffff0000, 0x13398(%rbx) # imm = 0xFFFF0000
 104c1e3:      	movzwl	0x1339e(%rbx), %eax
 104c1ea:      	testw	%ax, %ax
 104c1ed:      	je	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c1f3:      	shrl	$0x8, %edx
 104c1f6:      	andl	$0x3, %edx
 104c1f9:      	cmpw	$0x1, 0x133ac(%rbx)
 104c201:      	jne	0x104c278 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418478>
 104c203:      	movswl	0x133a4(%rbx), %r8d
 104c20b:      	xorl	%edi, %edi
 104c20d:      	cmpl	$0x3, %r8d
 104c211:      	je	0x104c32b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c217:      	cmpl	%r8d, %edx
 104c21a:      	setle	%cl
 104c21d:      	testw	%r8w, %r8w
 104c221:      	setns	%sil
 104c225:      	xorl	%edi, %edi
 104c227:      	testb	%cl, %sil
 104c22a:      	movl	$0xffff, %ecx           # imm = 0xFFFF
 104c22f:      	cmovnel	%edi, %ecx
 104c232:      	movl	$0xffffffff, %esi       # imm = 0xFFFFFFFF
 104c237:      	cmovnel	%r8d, %esi
 104c23b:      	cmpw	$0x1, 0x133ae(%rbx)
 104c243:      	je	0x104c28c <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41848c>
 104c245:      	cmpw	$0x1, 0x133b0(%rbx)
 104c24d:      	je	0x104c2ca <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4184ca>
 104c24f:      	cmpw	$0x1, 0x133b2(%rbx)
 104c257:      	je	0x104c304 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418504>
 104c25d:      	jmp	0x104c321 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c262:      	movl	0x100(%rbx), %esi
 104c268:      	movl	%esi, 0xc(%rbx)
 104c26b:      	testb	%al, %al
 104c26d:      	jne	0x104c1a9 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4183a9>
 104c273:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c278:      	movl	$0xffffffff, %esi       # imm = 0xFFFFFFFF
 104c27d:      	movl	$0xffff, %ecx           # imm = 0xFFFF
 104c282:      	cmpw	$0x1, 0x133ae(%rbx)
 104c28a:      	jne	0x104c245 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418445>
 104c28c:      	movswl	0x133a6(%rbx), %r8d
 104c294:      	movl	$0x1, %edi
 104c299:      	cmpl	$0x3, %r8d
 104c29d:      	je	0x104c32b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c2a3:      	cmpl	%r8d, %edx
 104c2a6:      	setle	%dil
 104c2aa:      	cmpl	%r8d, %esi
 104c2ad:      	setl	%r9b
 104c2b1:      	testb	%r9b, %dil
 104c2b4:      	movl	$0x1, %edi
 104c2b9:      	cmovnel	%edi, %ecx
 104c2bc:      	cmovnel	%r8d, %esi
 104c2c0:      	cmpw	$0x1, 0x133b0(%rbx)
 104c2c8:      	jne	0x104c24f <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41844f>
 104c2ca:      	movswl	0x133a8(%rbx), %r8d
 104c2d2:      	movl	$0x2, %edi
 104c2d7:      	cmpl	$0x3, %r8d
 104c2db:      	je	0x104c32b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c2dd:      	cmpl	%r8d, %edx
 104c2e0:      	setle	%dil
 104c2e4:      	cmpl	%r8d, %esi
 104c2e7:      	setl	%r9b
 104c2eb:      	testb	%r9b, %dil
 104c2ee:      	movl	$0x2, %edi
 104c2f3:      	cmovnel	%edi, %ecx
 104c2f6:      	cmovnel	%r8d, %esi
 104c2fa:      	cmpw	$0x1, 0x133b2(%rbx)
 104c302:      	jne	0x104c321 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c304:      	movswl	0x133aa(%rbx), %r8d
 104c30c:      	movl	$0x3, %edi
 104c311:      	cmpl	$0x3, %r8d
 104c315:      	je	0x104c32b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c317:      	cmpl	%r8d, %edx
 104c31a:      	jg	0x104c321 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418521>
 104c31c:      	cmpl	%r8d, %esi
 104c31f:      	jl	0x104c32b <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x41852b>
 104c321:      	movl	%ecx, %edi
 104c323:      	cmpl	$0xffff, %ecx           # imm = 0xFFFF
 104c329:      	je	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c32b:      	movl	%edi, %ecx
 104c32d:      	movw	$0x0, 0x133ac(%rbx,%rcx,2)
 104c337:      	decl	%eax
 104c339:      	movw	%ax, 0x1339e(%rbx)
 104c340:      	movzwl	0x133a4(%rbx,%rcx,2), %eax
 104c348:      	cmpw	$0x2, %ax
 104c34c:      	movl	$0x2, %ecx
 104c351:      	cmovll	%eax, %ecx
 104c354:      	incl	%ecx
 104c356:      	shll	$0x4, %edi
 104c359:      	leaq	0x704730(%rip), %rax    # 0x1750a90
 104c360:      	movzwl	0x2(%rdi,%rax), %eax
 104c365:      	movw	%ax, 0x1339a(%rbx)
 104c36c:      	movw	$0x5, 0x133a2(%rbx)
 104c375:      	movw	$0x1, 0x13398(%rbx)
 104c37e:      	movw	%cx, 0x133a0(%rbx)
 104c385:      	movzwl	0x8(%rbx), %eax
 104c389:      	addl	%eax, 0x133d0(%rbx)
 104c38f:      	addq	$0x8, %rsp
 104c393:      	popq	%rbx
 104c394:      	popq	%r12
 104c396:      	popq	%r13
 104c398:      	popq	%r14
 104c39a:      	popq	%r15
 104c39c:      	popq	%rbp
 104c39d:      	xorl	%eax, %eax
 104c39f:      	xorl	%ecx, %ecx
 104c3a1:      	xorl	%edi, %edi
 104c3a3:      	xorl	%edx, %edx
 104c3a5:      	xorl	%esi, %esi
 104c3a7:      	xorl	%r8d, %r8d
 104c3aa:      	xorl	%r9d, %r9d
 104c3ad:      	xorl	%r10d, %r10d
 104c3b0:      	xorl	%r11d, %r11d
 104c3b3:      	retq
 104c3b4:      	movw	%si, 0x1339c(%rbx)
 104c3bb:      	movzwl	0x1339a(%rbx), %eax
 104c3c2:      	movl	%eax, 0xc(%rbx)
 104c3c5:      	cmpl	$0x1000, %eax           # imm = 0x1000
 104c3ca:      	jae	0x104c5c1 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187c1>
 104c3d0:      	movl	0x6190(%rbx,%rax,4), %eax
 104c3d7:      	cmpl	$0x1000000, %eax        # imm = 0x1000000
 104c3dc:      	jae	0x104c5e0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4187e0>
 104c3e2:      	movl	%eax, %ecx
 104c3e4:      	andl	$0xfff000, %ecx         # imm = 0xFFF000
 104c3ea:      	cmpl	$0xd0000, %ecx          # imm = 0xD0000
 104c3f0:      	setne	%cl
 104c3f3:      	andl	$0xffc0ff, %eax         # imm = 0xFFC0FF
 104c3f8:      	cmpl	$0xbc080, %eax          # imm = 0xBC080
 104c3fd:      	setne	%dil
 104c401:      	movw	$0x3, %ax
 104c405:      	testb	%dil, %cl
 104c408:      	jne	0x104c444 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418644>
 104c40a:      	movw	$0x2, 0x13398(%rbx)
 104c413:      	movzwl	%si, %esi
 104c416:      	movq	%rbx, %rdi
 104c419:      	callq	0x10597b0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4259b0>
 104c41e:      	movl	$0xffff70ff, %eax       # imm = 0xFFFF70FF
 104c423:      	andl	0xf4(%rbx), %eax
 104c429:      	movzwl	0x133a0(%rbx), %ecx
 104c430:      	shll	$0x8, %ecx
 104c433:      	orl	%eax, %ecx
 104c435:      	movl	%ecx, 0xf4(%rbx)
 104c43b:      	movzwl	0x133a2(%rbx), %eax
 104c442:      	decl	%eax
 104c444:      	movw	%ax, 0x133a2(%rbx)
 104c44b:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c450:      	movzwl	0x1339a(%rbx), %eax
 104c457:      	addl	$0x2, %eax
 104c45a:      	cmpl	%eax, %esi
 104c45c:      	jne	0x104c468 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418668>
 104c45e:      	movzwl	0x1339c(%rbx), %eax
 104c465:      	movl	%eax, 0xc(%rbx)
 104c468:      	movw	$0x1, 0x133a2(%rbx)
 104c471:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c476:      	movzwl	0x1339a(%rbx), %eax
 104c47d:      	incl	%eax
 104c47f:      	cmpl	%eax, %esi
 104c481:      	jne	0x104c4e8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4186e8>
 104c483:      	movq	%rbx, %rdi
 104c486:      	callq	0x104c640 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418840>
 104c48b:      	movl	%eax, %ecx
 104c48d:      	andl	$0xfff000, %ecx         # imm = 0xFFF000
 104c493:      	cmpl	$0xd0000, %ecx          # imm = 0xD0000
 104c499:      	setne	%cl
 104c49c:      	andl	$0xffc0ff, %eax         # imm = 0xFFC0FF
 104c4a1:      	cmpl	$0xbc080, %eax          # imm = 0xBC080
 104c4a6:      	setne	%al
 104c4a9:      	testb	%al, %cl
 104c4ab:      	jne	0x104c4e8 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4186e8>
 104c4ad:      	movw	$0x2, 0x13398(%rbx)
 104c4b6:      	movzwl	0x1339c(%rbx), %esi
 104c4bd:      	movl	0xf4(%rbx), %edx
 104c4c3:      	movq	%rbx, %rdi
 104c4c6:      	callq	0x10597b0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4259b0>
 104c4cb:      	movl	$0xffff70ff, %eax       # imm = 0xFFFF70FF
 104c4d0:      	andl	0xf4(%rbx), %eax
 104c4d6:      	movzwl	0x133a0(%rbx), %ecx
 104c4dd:      	shll	$0x8, %ecx
 104c4e0:      	orl	%eax, %ecx
 104c4e2:      	movl	%ecx, 0xf4(%rbx)
 104c4e8:      	decw	0x133a2(%rbx)
 104c4ef:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c4f4:      	movw	$0x0, 0x133a2(%rbx)
 104c4fd:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c502:      	movw	$0x4, 0x133a2(%rbx)
 104c50b:      	jmp	0x104c385 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418585>
 104c510:      	leaq	0x86d22d(%rip), %rax    # 0x18b9744
 104c517:      	cmpw	$0x0, (%rax)
 104c51b:      	je	0x104be42 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c521:      	leaq	0x86dc70(%rip), %rax    # 0x18ba198
 104c528:      	testb	$-0x80, 0x1(%rax)
 104c52c:      	je	0x104be42 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c532:      	movzbl	(%rbx), %esi
 104c535:      	leaq	-0xe3443c(%rip), %rdi   # 0x218100
 104c53c:      	xorl	%eax, %eax
 104c53e:      	callq	0x11c9080 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x595280>
 104c543:      	movl	0xc(%rbx), %edx
 104c546:      	jmp	0x104be42 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418042>
 104c54b:      	leaq	0x86d1f4(%rip), %rax    # 0x18b9746
 104c552:      	cmpw	$0x0, (%rax)
 104c556:      	je	0x104be98 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c55c:      	movq	%rbx, %rdi
 104c55f:      	callq	0x104c6b0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x4188b0>
 104c564:      	testw	%ax, %ax
 104c567:      	je	0x104be98 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c56d:      	movq	%rbx, %rdi
 104c570:      	callq	0x104c970 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418b70>
 104c575:      	movq	%rax, %rdi
 104c578:      	callq	0x104cac0 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418cc0>
 104c57d:      	jmp	0x104be98 <_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEmc+0x418098>
 104c582:      	movq	0x71ce9f(%rip), %rax    # 0x1769428
 104c589:      	movq	(%rax), %rdi
 104c58c:      	leaq	-0xd08f2f(%rip), %rdx   # 0x343664
 104c593:      	movl	$0x1, %esi
 104c598:      	movl	%r14d, %ecx
 104c59b:      	xorl	%eax, %eax
 104c59d:      	callq	0x16cb1d0 <__fprintf_chk@plt>
 104c5a2:      	leaq	-0xe40eec(%rip), %rdi   # 0x20b6bd
 104c5a9:      	leaq	-0xd090a2(%rip), %rsi   # 0x34350e
 104c5b0:      	leaq	-0xd460d6(%rip), %rcx   # 0x3064e1
 104c5b7:      	movl	$0x1a3, %edx            # imm = 0x1A3
 104c5bc:      	callq	0x16cb0e0 <__assert_fail@plt>
 104c5c1:      	leaq	-0xdb1eb0(%rip), %rdi   # 0x29a718
 104c5c8:      	leaq	-0xd090c1(%rip), %rsi   # 0x34350e
 104c5cf:      	leaq	-0xd146f2(%rip), %rcx   # 0x337ee4
 104c5d6:      	movl	$0x37b, %edx            # imm = 0x37B
 104c5db:      	callq	0x16cb0e0 <__assert_fail@plt>
 104c5e0:      	leaq	-0xe40f0c(%rip), %rdi   # 0x20b6db
 104c5e7:      	leaq	-0xd090e0(%rip), %rsi   # 0x34350e
 104c5ee:      	leaq	-0xd14711(%rip), %rcx   # 0x337ee4
 104c5f5:      	movl	$0x37d, %edx            # imm = 0x37D
 104c5fa:      	callq	0x16cb0e0 <__assert_fail@plt>
 104c5ff:      	leaq	-0xd80e5d(%rip), %rdi   # 0x2cb7a9
 104c606:      	leaq	-0xd090ff(%rip), %rsi   # 0x34350e
 104c60d:      	leaq	-0xd14730(%rip), %rcx   # 0x337ee4
 104c614:      	movl	$0x37a, %edx            # imm = 0x37A
 104c619:      	callq	0x16cb0e0 <__assert_fail@plt>
 104c61e:      	leaq	-0xddfda4(%rip), %rdi   # 0x26c881
 104c625:      	leaq	-0xd0911e(%rip), %rsi   # 0x34350e
 104c62c:      	leaq	-0xddfd9b(%rip), %rcx   # 0x26c898
 104c633:      	movl	$0x455, %edx            # imm = 0x455
 104c638:      	callq	0x16cb0e0 <__assert_fail@plt>
