
/home/codex/xemu-shader-workbench-handoff/.scratch/tcg-lookup-inline-20261006/after-cc.o:     file format elf64-x86-64


Disassembly of section .text:

00000000000013f0 <helper_lookup_tb_ptr_i32>:
    13f0:	41 57                	push   %r15
    13f2:	66 0f 6e c1          	movd   %ecx,%xmm0
    13f6:	41 56                	push   %r14
    13f8:	4c 8d b7 a0 b4 ff ff 	lea    -0x4b60(%rdi),%r14
    13ff:	41 55                	push   %r13
    1401:	44 8d 2c 16          	lea    (%rsi,%rdx,1),%r13d
    1405:	41 54                	push   %r12
    1407:	55                   	push   %rbp
    1408:	53                   	push   %rbx
    1409:	48 89 fb             	mov    %rdi,%rbx
    140c:	48 83 ec 48          	sub    $0x48,%rsp
    1410:	8b 8f 74 b5 ff ff    	mov    -0x4a8c(%rdi),%ecx
    1416:	8b 87 68 b7 ff ff    	mov    -0x4898(%rdi),%eax
    141c:	85 c9                	test   %ecx,%ecx
    141e:	0f 85 bc 01 00 00    	jne    15e0 <helper_lookup_tb_ptr_i32+0x1f0>
    1424:	0f b6 0d 00 00 00 00 	movzbl 0x0(%rip),%ecx        # 142b <helper_lookup_tb_ptr_i32+0x3b>
			1427: R_X86_64_PC32	one_insn_per_tb-0x4
    142b:	84 c9                	test   %cl,%cl
    142d:	0f 85 cd 00 00 00    	jne    1500 <helper_lookup_tb_ptr_i32+0x110>
    1433:	89 c1                	mov    %eax,%ecx
    1435:	80 cd 02             	or     $0x2,%ch
    1438:	f6 05 00 00 00 00 20 	testb  $0x20,0x0(%rip)        # 143f <helper_lookup_tb_ptr_i32+0x4f>
			143a: R_X86_64_PC32	qemu_loglevel-0x4
    143f:	0f 45 c1             	cmovne %ecx,%eax
    1442:	66 0f 6f c8          	movdqa %xmm0,%xmm1
    1446:	66 0f 6e d0          	movd   %eax,%xmm2
    144a:	48 83 bb e0 b6 ff ff 	cmpq   $0x0,-0x4920(%rbx)
    1451:	00 
    1452:	c6 43 f4 01          	movb   $0x1,-0xc(%rbx)
    1456:	66 0f 62 ca          	punpckldq %xmm2,%xmm1
    145a:	0f 85 40 01 00 00    	jne    15a0 <helper_lookup_tb_ptr_i32+0x1b0>
    1460:	8b 05 00 00 00 00    	mov    0x0(%rip),%eax        # 1466 <helper_lookup_tb_ptr_i32+0x76>
			1462: R_X86_64_PC32	target_page
    1466:	4c 8b bb b8 b6 ff ff 	mov    -0x4948(%rbx),%r15
    146d:	8d 48 fa             	lea    -0x6(%rax),%ecx
    1470:	4c 89 e8             	mov    %r13,%rax
    1473:	48 d3 e8             	shr    %cl,%rax
    1476:	4c 31 e8             	xor    %r13,%rax
    1479:	48 89 c5             	mov    %rax,%rbp
    147c:	83 e0 3f             	and    $0x3f,%eax
    147f:	48 d3 ed             	shr    %cl,%rbp
    1482:	81 e5 c0 0f 00 00    	and    $0xfc0,%ebp
    1488:	09 c5                	or     %eax,%ebp
    148a:	44 8d 65 01          	lea    0x1(%rbp),%r12d
    148e:	49 c1 e4 04          	shl    $0x4,%r12
    1492:	4d 01 fc             	add    %r15,%r12
    1495:	49 8b 1c 24          	mov    (%r12),%rbx
    1499:	48 85 db             	test   %rbx,%rbx
    149c:	74 72                	je     1510 <helper_lookup_tb_ptr_i32+0x120>
    149e:	89 e8                	mov    %ebp,%eax
    14a0:	48 c1 e0 04          	shl    $0x4,%rax
    14a4:	4e 39 6c 38 18       	cmp    %r13,0x18(%rax,%r15,1)
    14a9:	75 65                	jne    1510 <helper_lookup_tb_ptr_i32+0x120>
    14ab:	48 3b 53 08          	cmp    0x8(%rbx),%rdx
    14af:	75 5f                	jne    1510 <helper_lookup_tb_ptr_i32+0x120>
    14b1:	66 0f 7e c8          	movd   %xmm1,%eax
    14b5:	3b 43 10             	cmp    0x10(%rbx),%eax
    14b8:	75 56                	jne    1510 <helper_lookup_tb_ptr_i32+0x120>
    14ba:	8b 43 14             	mov    0x14(%rbx),%eax
    14bd:	66 0f 70 d9 e5       	pshufd $0xe5,%xmm1,%xmm3
    14c2:	66 0f 7e d9          	movd   %xmm3,%ecx
    14c6:	39 c1                	cmp    %eax,%ecx
    14c8:	75 46                	jne    1510 <helper_lookup_tb_ptr_i32+0x120>
    14ca:	8b 43 14             	mov    0x14(%rbx),%eax
    14cd:	a9 00 00 02 00       	test   $0x20000,%eax
    14d2:	75 09                	jne    14dd <helper_lookup_tb_ptr_i32+0xed>
    14d4:	4c 3b 2b             	cmp    (%rbx),%r13
    14d7:	0f 85 00 00 00 00    	jne    14dd <helper_lookup_tb_ptr_i32+0xed>
			14d9: R_X86_64_PC32	.text.unlikely+0x113
    14dd:	66 f7 05 00 00 00 00 	testw  $0x120,0x0(%rip)        # 14e6 <helper_lookup_tb_ptr_i32+0xf6>
    14e4:	20 01 
			14e0: R_X86_64_PC32	qemu_loglevel-0x6
    14e6:	0f 85 84 00 00 00    	jne    1570 <helper_lookup_tb_ptr_i32+0x180>
    14ec:	48 8b 43 28          	mov    0x28(%rbx),%rax
    14f0:	48 83 c4 48          	add    $0x48,%rsp
    14f4:	5b                   	pop    %rbx
    14f5:	5d                   	pop    %rbp
    14f6:	41 5c                	pop    %r12
    14f8:	41 5d                	pop    %r13
    14fa:	41 5e                	pop    %r14
    14fc:	41 5f                	pop    %r15
    14fe:	c3                   	ret
    14ff:	90                   	nop
    1500:	0d 01 02 00 00       	or     $0x201,%eax
    1505:	e9 38 ff ff ff       	jmp    1442 <helper_lookup_tb_ptr_i32+0x52>
    150a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1510:	48 89 54 24 30       	mov    %rdx,0x30(%rsp)
    1515:	48 83 ec 20          	sub    $0x20,%rsp
    1519:	48 8d 35 00 00 00 00 	lea    0x0(%rip),%rsi        # 1520 <helper_lookup_tb_ptr_i32+0x130>
			151c: R_X86_64_PC32	tb_ctx-0x4
    1520:	4c 89 f7             	mov    %r14,%rdi
    1523:	4c 89 6c 24 40       	mov    %r13,0x40(%rsp)
    1528:	66 0f d6 4c 24 48    	movq   %xmm1,0x48(%rsp)
    152e:	66 0f 6f 44 24 40    	movdqa 0x40(%rsp),%xmm0
    1534:	48 89 54 24 10       	mov    %rdx,0x10(%rsp)
    1539:	48 8d 15 c0 ea ff ff 	lea    -0x1540(%rip),%rdx        # 0 <tb_lookup_cmp>
    1540:	0f 11 04 24          	movups %xmm0,(%rsp)
    1544:	e8 57 ec ff ff       	call   1a0 <tb_htable_lookup_common>
    1549:	48 83 c4 20          	add    $0x20,%rsp
    154d:	48 89 c3             	mov    %rax,%rbx
    1550:	48 85 c0             	test   %rax,%rax
    1553:	74 33                	je     1588 <helper_lookup_tb_ptr_i32+0x198>
    1555:	48 c1 e5 04          	shl    $0x4,%rbp
    1559:	4e 89 6c 3d 18       	mov    %r13,0x18(%rbp,%r15,1)
    155e:	49 89 04 24          	mov    %rax,(%r12)
    1562:	e9 63 ff ff ff       	jmp    14ca <helper_lookup_tb_ptr_i32+0xda>
    1567:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
    156e:	00 00 
    1570:	48 89 da             	mov    %rbx,%rdx
    1573:	4c 89 f6             	mov    %r14,%rsi
    1576:	4c 89 ef             	mov    %r13,%rdi
    1579:	e8 c2 ed ff ff       	call   340 <log_cpu_exec>
    157e:	e9 69 ff ff ff       	jmp    14ec <helper_lookup_tb_ptr_i32+0xfc>
    1583:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    1588:	48 8b 05 00 00 00 00 	mov    0x0(%rip),%rax        # 158f <helper_lookup_tb_ptr_i32+0x19f>
			158b: R_X86_64_PC32	tcg_code_gen_epilogue-0x4
    158f:	48 83 c4 48          	add    $0x48,%rsp
    1593:	5b                   	pop    %rbx
    1594:	5d                   	pop    %rbp
    1595:	41 5c                	pop    %r12
    1597:	41 5d                	pop    %r13
    1599:	41 5e                	pop    %r14
    159b:	41 5f                	pop    %r15
    159d:	c3                   	ret
    159e:	66 90                	xchg   %ax,%ax
    15a0:	48 89 54 24 10       	mov    %rdx,0x10(%rsp)
    15a5:	4c 89 ee             	mov    %r13,%rsi
    15a8:	48 8d 54 24 0c       	lea    0xc(%rsp),%rdx
    15ad:	4c 89 f7             	mov    %r14,%rdi
    15b0:	4c 89 2c 24          	mov    %r13,(%rsp)
    15b4:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    15b8:	66 0f 7e 44 24 08    	movd   %xmm0,0x8(%rsp)
    15be:	e8 cd ea ff ff       	call   90 <check_for_breakpoints_slow>
    15c3:	84 c0                	test   %al,%al
    15c5:	75 29                	jne    15f0 <helper_lookup_tb_ptr_i32+0x200>
    15c7:	4c 8b 2c 24          	mov    (%rsp),%r13
    15cb:	f3 0f 7e 4c 24 08    	movq   0x8(%rsp),%xmm1
    15d1:	48 8b 54 24 10       	mov    0x10(%rsp),%rdx
    15d6:	e9 85 fe ff ff       	jmp    1460 <helper_lookup_tb_ptr_i32+0x70>
    15db:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    15e0:	0d 01 0e 00 00       	or     $0xe01,%eax
    15e5:	e9 58 fe ff ff       	jmp    1442 <helper_lookup_tb_ptr_i32+0x52>
    15ea:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    15f0:	4c 89 f7             	mov    %r14,%rdi
    15f3:	e8 00 00 00 00       	call   15f8 <helper_lookup_tb_ptr_i32+0x208>
			15f4: R_X86_64_PLT32	cpu_loop_exit-0x4

Disassembly of section .text.unlikely:
