
/home/codex/xemu-shader-workbench-handoff/.scratch/tcg-lookup-inline-20261006/after-clang.o:     file format elf64-x86-64


Disassembly of section .text:

00000000000004d0 <helper_lookup_tb_ptr_i32>:
     4d0:	41 57                	push   %r15
     4d2:	41 56                	push   %r14
     4d4:	41 54                	push   %r12
     4d6:	53                   	push   %rbx
     4d7:	48 83 ec 58          	sub    $0x58,%rsp
     4db:	89 f0                	mov    %esi,%eax
     4dd:	01 d0                	add    %edx,%eax
     4df:	44 8b 87 68 b7 ff ff 	mov    -0x4898(%rdi),%r8d
     4e6:	83 bf 74 b5 ff ff 00 	cmpl   $0x0,-0x4a8c(%rdi)
     4ed:	0f 85 81 01 00 00    	jne    674 <helper_lookup_tb_ptr_i32+0x1a4>
     4f3:	48 8b 35 00 00 00 00 	mov    0x0(%rip),%rsi        # 4fa <helper_lookup_tb_ptr_i32+0x2a>
			4f6: R_X86_64_REX_GOTPCRELX	one_insn_per_tb-0x4
     4fa:	0f b6 36             	movzbl (%rsi),%esi
     4fd:	40 f6 c6 01          	test   $0x1,%sil
     501:	74 09                	je     50c <helper_lookup_tb_ptr_i32+0x3c>
     503:	41 81 c8 01 02 00 00 	or     $0x201,%r8d
     50a:	eb 15                	jmp    521 <helper_lookup_tb_ptr_i32+0x51>
     50c:	48 8b 35 00 00 00 00 	mov    0x0(%rip),%rsi        # 513 <helper_lookup_tb_ptr_i32+0x43>
			50f: R_X86_64_REX_GOTPCRELX	qemu_loglevel-0x4
     513:	8b 36                	mov    (%rsi),%esi
     515:	c1 ee 04             	shr    $0x4,%esi
     518:	81 e6 00 02 00 00    	and    $0x200,%esi
     51e:	41 09 f0             	or     %esi,%r8d
     521:	48 8d b7 a0 b4 ff ff 	lea    -0x4b60(%rdi),%rsi
     528:	48 89 44 24 40       	mov    %rax,0x40(%rsp)
     52d:	89 4c 24 48          	mov    %ecx,0x48(%rsp)
     531:	44 89 44 24 4c       	mov    %r8d,0x4c(%rsp)
     536:	48 89 54 24 50       	mov    %rdx,0x50(%rsp)
     53b:	c6 47 f4 01          	movb   $0x1,-0xc(%rdi)
     53f:	48 83 bf e0 b6 ff ff 	cmpq   $0x0,-0x4920(%rdi)
     546:	00 
     547:	0f 85 33 01 00 00    	jne    680 <helper_lookup_tb_ptr_i32+0x1b0>
     54d:	48 8b 44 24 40       	mov    0x40(%rsp),%rax
     552:	48 89 44 24 20       	mov    %rax,0x20(%rsp)
     557:	8b 44 24 48          	mov    0x48(%rsp),%eax
     55b:	89 44 24 28          	mov    %eax,0x28(%rsp)
     55f:	8b 44 24 4c          	mov    0x4c(%rsp),%eax
     563:	89 44 24 2c          	mov    %eax,0x2c(%rsp)
     567:	48 8b 44 24 50       	mov    0x50(%rsp),%rax
     56c:	4c 8b 74 24 20       	mov    0x20(%rsp),%r14
     571:	48 8b 0d 00 00 00 00 	mov    0x0(%rip),%rcx        # 578 <helper_lookup_tb_ptr_i32+0xa8>
			574: R_X86_64_REX_GOTPCRELX	target_page-0x4
     578:	0f b6 49 04          	movzbl 0x4(%rcx),%ecx
     57c:	80 c1 fa             	add    $0xfa,%cl
     57f:	4c 89 f2             	mov    %r14,%rdx
     582:	48 d3 ea             	shr    %cl,%rdx
     585:	48 89 44 24 30       	mov    %rax,0x30(%rsp)
     58a:	8b 44 24 2c          	mov    0x2c(%rsp),%eax
     58e:	4c 31 f2             	xor    %r14,%rdx
     591:	49 89 d0             	mov    %rdx,%r8
     594:	49 d3 e8             	shr    %cl,%r8
     597:	41 81 e0 c0 0f 00 00 	and    $0xfc0,%r8d
     59e:	83 e2 3f             	and    $0x3f,%edx
     5a1:	44 09 c2             	or     %r8d,%edx
     5a4:	48 8b 8f b8 b6 ff ff 	mov    -0x4948(%rdi),%rcx
     5ab:	c1 e2 04             	shl    $0x4,%edx
     5ae:	4c 8d 3c 11          	lea    (%rcx,%rdx,1),%r15
     5b2:	49 83 c7 10          	add    $0x10,%r15
     5b6:	48 8b 5c 11 10       	mov    0x10(%rcx,%rdx,1),%rbx
     5bb:	48 85 db             	test   %rbx,%rbx
     5be:	74 65                	je     625 <helper_lookup_tb_ptr_i32+0x155>
     5c0:	4d 39 77 08          	cmp    %r14,0x8(%r15)
     5c4:	75 5f                	jne    625 <helper_lookup_tb_ptr_i32+0x155>
     5c6:	48 8b 4b 08          	mov    0x8(%rbx),%rcx
     5ca:	48 3b 4c 24 30       	cmp    0x30(%rsp),%rcx
     5cf:	75 54                	jne    625 <helper_lookup_tb_ptr_i32+0x155>
     5d1:	8b 4b 10             	mov    0x10(%rbx),%ecx
     5d4:	3b 4c 24 28          	cmp    0x28(%rsp),%ecx
     5d8:	75 4b                	jne    625 <helper_lookup_tb_ptr_i32+0x155>
     5da:	8b 4b 14             	mov    0x14(%rbx),%ecx
     5dd:	39 c1                	cmp    %eax,%ecx
     5df:	75 44                	jne    625 <helper_lookup_tb_ptr_i32+0x155>
     5e1:	8b 43 14             	mov    0x14(%rbx),%eax
     5e4:	a9 00 00 02 00       	test   $0x20000,%eax
     5e9:	75 09                	jne    5f4 <helper_lookup_tb_ptr_i32+0x124>
     5eb:	4c 39 33             	cmp    %r14,(%rbx)
     5ee:	0f 85 b8 00 00 00    	jne    6ac <helper_lookup_tb_ptr_i32+0x1dc>
     5f4:	48 8b 05 00 00 00 00 	mov    0x0(%rip),%rax        # 5fb <helper_lookup_tb_ptr_i32+0x12b>
			5f7: R_X86_64_REX_GOTPCRELX	qemu_loglevel-0x4
     5fb:	0f b7 00             	movzwl (%rax),%eax
     5fe:	a9 20 01 00 00       	test   $0x120,%eax
     603:	74 0d                	je     612 <helper_lookup_tb_ptr_i32+0x142>
     605:	48 8b 7c 24 40       	mov    0x40(%rsp),%rdi
     60a:	48 89 da             	mov    %rbx,%rdx
     60d:	e8 ee 0c 00 00       	call   1300 <log_cpu_exec>
     612:	48 83 c3 28          	add    $0x28,%rbx
     616:	48 8b 03             	mov    (%rbx),%rax
     619:	48 83 c4 58          	add    $0x58,%rsp
     61d:	5b                   	pop    %rbx
     61e:	41 5c                	pop    %r12
     620:	41 5e                	pop    %r14
     622:	41 5f                	pop    %r15
     624:	c3                   	ret
     625:	48 8b 44 24 30       	mov    0x30(%rsp),%rax
     62a:	48 89 44 24 10       	mov    %rax,0x10(%rsp)
     62f:	0f 28 44 24 20       	movaps 0x20(%rsp),%xmm0
     634:	0f 11 04 24          	movups %xmm0,(%rsp)
     638:	48 8b 05 00 00 00 00 	mov    0x0(%rip),%rax        # 63f <helper_lookup_tb_ptr_i32+0x16f>
			63b: R_X86_64_REX_GOTPCRELX	tb_ctx-0x4
     63f:	48 8d 15 3a 0b 00 00 	lea    0xb3a(%rip),%rdx        # 1180 <tb_lookup_cmp>
     646:	49 89 f4             	mov    %rsi,%r12
     649:	48 89 f7             	mov    %rsi,%rdi
     64c:	48 89 c6             	mov    %rax,%rsi
     64f:	e8 cc f9 ff ff       	call   20 <tb_htable_lookup_common>
     654:	48 85 c0             	test   %rax,%rax
     657:	74 12                	je     66b <helper_lookup_tb_ptr_i32+0x19b>
     659:	48 89 c3             	mov    %rax,%rbx
     65c:	4d 89 77 08          	mov    %r14,0x8(%r15)
     660:	49 89 07             	mov    %rax,(%r15)
     663:	4c 89 e6             	mov    %r12,%rsi
     666:	e9 76 ff ff ff       	jmp    5e1 <helper_lookup_tb_ptr_i32+0x111>
     66b:	48 8b 1d 00 00 00 00 	mov    0x0(%rip),%rbx        # 672 <helper_lookup_tb_ptr_i32+0x1a2>
			66e: R_X86_64_REX_GOTPCRELX	tcg_code_gen_epilogue-0x4
     672:	eb a2                	jmp    616 <helper_lookup_tb_ptr_i32+0x146>
     674:	41 81 c8 01 0e 00 00 	or     $0xe01,%r8d
     67b:	e9 a1 fe ff ff       	jmp    521 <helper_lookup_tb_ptr_i32+0x51>
     680:	48 8d 54 24 4c       	lea    0x4c(%rsp),%rdx
     685:	48 89 fb             	mov    %rdi,%rbx
     688:	48 89 f7             	mov    %rsi,%rdi
     68b:	49 89 f6             	mov    %rsi,%r14
     68e:	48 89 c6             	mov    %rax,%rsi
     691:	e8 8a 0d 00 00       	call   1420 <check_for_breakpoints_slow>
     696:	48 89 df             	mov    %rbx,%rdi
     699:	4c 89 f6             	mov    %r14,%rsi
     69c:	84 c0                	test   %al,%al
     69e:	0f 84 a9 fe ff ff    	je     54d <helper_lookup_tb_ptr_i32+0x7d>
     6a4:	48 89 f7             	mov    %rsi,%rdi
     6a7:	e8 00 00 00 00       	call   6ac <helper_lookup_tb_ptr_i32+0x1dc>
			6a8: R_X86_64_PLT32	cpu_loop_exit-0x4
     6ac:	48 8d 3d 00 00 00 00 	lea    0x0(%rip),%rdi        # 6b3 <helper_lookup_tb_ptr_i32+0x1e3>
			6af: R_X86_64_PC32	.L.str.15-0x4
     6b3:	48 8d 35 00 00 00 00 	lea    0x0(%rip),%rsi        # 6ba <helper_lookup_tb_ptr_i32+0x1ea>
			6b6: R_X86_64_PC32	.L.str-0x4
     6ba:	48 8d 0d 00 00 00 00 	lea    0x0(%rip),%rcx        # 6c1 <helper_lookup_tb_ptr_i32+0x1f1>
			6bd: R_X86_64_PC32	.L__PRETTY_FUNCTION__.tb_lookup-0x4
     6c1:	ba 1b 01 00 00       	mov    $0x11b,%edx
     6c6:	e8 00 00 00 00       	call   6cb <helper_lookup_tb_ptr_i32+0x1fb>
			6c7: R_X86_64_PLT32	__assert_fail-0x4
