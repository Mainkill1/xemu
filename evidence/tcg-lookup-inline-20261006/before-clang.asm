
/home/codex/xemu-shader-workbench-handoff/.scratch/tcg-lookup-inline-20261006/before-clang.o:     file format elf64-x86-64


Disassembly of section .text:

00000000000004d0 <helper_lookup_tb_ptr_i32>:
     4d0:	48 83 ec 38          	sub    $0x38,%rsp
     4d4:	0f 57 c0             	xorps  %xmm0,%xmm0
     4d7:	0f 11 44 24 28       	movups %xmm0,0x28(%rsp)
     4dc:	01 d6                	add    %edx,%esi
     4de:	48 89 74 24 20       	mov    %rsi,0x20(%rsp)
     4e3:	89 4c 24 28          	mov    %ecx,0x28(%rsp)
     4e7:	8b 87 68 b7 ff ff    	mov    -0x4898(%rdi),%eax
     4ed:	83 bf 74 b5 ff ff 00 	cmpl   $0x0,-0x4a8c(%rdi)
     4f4:	75 52                	jne    548 <helper_lookup_tb_ptr_i32+0x78>
     4f6:	48 8b 0d 00 00 00 00 	mov    0x0(%rip),%rcx        # 4fd <helper_lookup_tb_ptr_i32+0x2d>
			4f9: R_X86_64_REX_GOTPCRELX	one_insn_per_tb-0x4
     4fd:	0f b6 09             	movzbl (%rcx),%ecx
     500:	f6 c1 01             	test   $0x1,%cl
     503:	74 07                	je     50c <helper_lookup_tb_ptr_i32+0x3c>
     505:	0d 01 02 00 00       	or     $0x201,%eax
     50a:	eb 14                	jmp    520 <helper_lookup_tb_ptr_i32+0x50>
     50c:	48 8b 0d 00 00 00 00 	mov    0x0(%rip),%rcx        # 513 <helper_lookup_tb_ptr_i32+0x43>
			50f: R_X86_64_REX_GOTPCRELX	qemu_loglevel-0x4
     513:	8b 09                	mov    (%rcx),%ecx
     515:	c1 e9 04             	shr    $0x4,%ecx
     518:	81 e1 00 02 00 00    	and    $0x200,%ecx
     51e:	09 c8                	or     %ecx,%eax
     520:	48 81 c7 a0 b4 ff ff 	add    $0xffffffffffffb4a0,%rdi
     527:	89 44 24 2c          	mov    %eax,0x2c(%rsp)
     52b:	48 89 54 24 30       	mov    %rdx,0x30(%rsp)
     530:	48 89 54 24 10       	mov    %rdx,0x10(%rsp)
     535:	0f 10 44 24 20       	movups 0x20(%rsp),%xmm0
     53a:	0f 11 04 24          	movups %xmm0,(%rsp)
     53e:	e8 fd fd ff ff       	call   340 <lookup_tb_ptr_common>
     543:	48 83 c4 38          	add    $0x38,%rsp
     547:	c3                   	ret
     548:	0d 01 0e 00 00       	or     $0xe01,%eax
     54d:	eb d1                	jmp    520 <helper_lookup_tb_ptr_i32+0x50>
