
/home/codex/xemu-shader-workbench-handoff/.scratch/tcg-lookup-inline-20261006/before-cc.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000001440 <helper_lookup_tb_ptr_i32>:
    1440:	48 83 ec 28          	sub    $0x28,%rsp
    1444:	01 d6                	add    %edx,%esi
    1446:	89 c8                	mov    %ecx,%eax
    1448:	48 89 34 24          	mov    %rsi,(%rsp)
    144c:	8b b7 74 b5 ff ff    	mov    -0x4a8c(%rdi),%esi
    1452:	48 8d 8f a0 b4 ff ff 	lea    -0x4b60(%rdi),%rcx
    1459:	89 44 24 08          	mov    %eax,0x8(%rsp)
    145d:	8b 87 68 b7 ff ff    	mov    -0x4898(%rdi),%eax
    1463:	85 f6                	test   %esi,%esi
    1465:	75 59                	jne    14c0 <helper_lookup_tb_ptr_i32+0x80>
    1467:	0f b6 35 00 00 00 00 	movzbl 0x0(%rip),%esi        # 146e <helper_lookup_tb_ptr_i32+0x2e>
			146a: R_X86_64_PC32	one_insn_per_tb-0x4
    146e:	40 84 f6             	test   %sil,%sil
    1471:	75 3d                	jne    14b0 <helper_lookup_tb_ptr_i32+0x70>
    1473:	89 c6                	mov    %eax,%esi
    1475:	81 ce 00 02 00 00    	or     $0x200,%esi
    147b:	f6 05 00 00 00 00 20 	testb  $0x20,0x0(%rip)        # 1482 <helper_lookup_tb_ptr_i32+0x42>
			147d: R_X86_64_PC32	qemu_loglevel-0x4
    1482:	0f 45 c6             	cmovne %esi,%eax
    1485:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    1489:	48 83 ec 20          	sub    $0x20,%rsp
    148d:	48 89 cf             	mov    %rcx,%rdi
    1490:	66 0f 6f 44 24 20    	movdqa 0x20(%rsp),%xmm0
    1496:	48 89 54 24 10       	mov    %rdx,0x10(%rsp)
    149b:	0f 11 04 24          	movups %xmm0,(%rsp)
    149f:	e8 0c fd ff ff       	call   11b0 <lookup_tb_ptr_common>
    14a4:	48 83 c4 48          	add    $0x48,%rsp
    14a8:	c3                   	ret
    14a9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
    14b0:	0d 01 02 00 00       	or     $0x201,%eax
    14b5:	eb ce                	jmp    1485 <helper_lookup_tb_ptr_i32+0x45>
    14b7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
    14be:	00 00 
    14c0:	0d 01 0e 00 00       	or     $0xe01,%eax
    14c5:	eb be                	jmp    1485 <helper_lookup_tb_ptr_i32+0x45>

Disassembly of section .text.unlikely:
