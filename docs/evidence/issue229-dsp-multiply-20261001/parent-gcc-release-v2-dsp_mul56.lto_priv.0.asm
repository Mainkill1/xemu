
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

00000000000246f0 <dsp_mul56.lto_priv.0>:
   246f0:	53                   	push   %rbx
   246f1:	89 f0                	mov    %esi,%eax
   246f3:	49 89 d2             	mov    %rdx,%r10
   246f6:	41 89 c8             	mov    %ecx,%r8d
   246f9:	48 83 ec 10          	sub    $0x10,%rsp
   246fd:	f7 c7 00 00 80 00    	test   $0x800000,%edi
   24703:	74 0d                	je     24712 <dsp_mul56.lto_priv.0+0x22>
   24705:	ba 00 00 00 01       	mov    $0x1000000,%edx
   2470a:	41 83 f0 01          	xor    $0x1,%r8d
   2470e:	29 fa                	sub    %edi,%edx
   24710:	89 d7                	mov    %edx,%edi
   24712:	a9 00 00 80 00       	test   $0x800000,%eax
   24717:	74 0d                	je     24726 <dsp_mul56.lto_priv.0+0x36>
   24719:	ba 00 00 00 01       	mov    $0x1000000,%edx
   2471e:	41 83 f0 01          	xor    $0x1,%r8d
   24722:	29 c2                	sub    %eax,%edx
   24724:	89 d0                	mov    %edx,%eax
   24726:	89 c3                	mov    %eax,%ebx
   24728:	41 89 fb             	mov    %edi,%r11d
   2472b:	c1 e8 0c             	shr    $0xc,%eax
   2472e:	41 c7 02 00 00 00 00 	movl   $0x0,(%r10)
   24735:	81 e3 ff 0f 00 00    	and    $0xfff,%ebx
   2473b:	c1 ef 0c             	shr    $0xc,%edi
   2473e:	41 81 e3 ff 0f 00 00 	and    $0xfff,%r11d
   24745:	89 c1                	mov    %eax,%ecx
   24747:	81 e7 ff 0f 00 00    	and    $0xfff,%edi
   2474d:	41 89 d9             	mov    %ebx,%r9d
   24750:	81 e1 ff 0f 00 00    	and    $0xfff,%ecx
   24756:	44 89 de             	mov    %r11d,%esi
   24759:	44 0f af cf          	imul   %edi,%r9d
   2475d:	0f af f1             	imul   %ecx,%esi
   24760:	44 0f af db          	imul   %ebx,%r11d
   24764:	44 89 ca             	mov    %r9d,%edx
   24767:	41 c1 e9 0c          	shr    $0xc,%r9d
   2476b:	c1 e2 0c             	shl    $0xc,%edx
   2476e:	89 f0                	mov    %esi,%eax
   24770:	c1 ee 0c             	shr    $0xc,%esi
   24773:	81 e2 00 f0 ff 00    	and    $0xfff000,%edx
   24779:	c1 e0 0c             	shl    $0xc,%eax
   2477c:	25 00 f0 ff 00       	and    $0xfff000,%eax
   24781:	44 01 da             	add    %r11d,%edx
   24784:	01 c2                	add    %eax,%edx
   24786:	89 f8                	mov    %edi,%eax
   24788:	0f af c1             	imul   %ecx,%eax
   2478b:	89 d1                	mov    %edx,%ecx
   2478d:	41 89 52 08          	mov    %edx,0x8(%r10)
   24791:	44 01 c8             	add    %r9d,%eax
   24794:	01 f0                	add    %esi,%eax
   24796:	c1 e9 18             	shr    $0x18,%ecx
   24799:	41 89 42 04          	mov    %eax,0x4(%r10)
   2479d:	74 10                	je     247af <dsp_mul56.lto_priv.0+0xbf>
   2479f:	01 c8                	add    %ecx,%eax
   247a1:	81 e2 ff ff ff 00    	and    $0xffffff,%edx
   247a7:	41 89 42 04          	mov    %eax,0x4(%r10)
   247ab:	41 89 52 08          	mov    %edx,0x8(%r10)
   247af:	be 01 00 00 00       	mov    $0x1,%esi
   247b4:	4c 89 d7             	mov    %r10,%rdi
   247b7:	e8 e4 fb ff ff       	call   243a0 <dsp_asl56.lto_priv.0>
   247bc:	45 84 c0             	test   %r8b,%r8b
   247bf:	75 1f                	jne    247e0 <dsp_mul56.lto_priv.0+0xf0>
   247c1:	48 83 c4 10          	add    $0x10,%rsp
   247c5:	5b                   	pop    %rbx
   247c6:	31 c0                	xor    %eax,%eax
   247c8:	31 d2                	xor    %edx,%edx
   247ca:	31 c9                	xor    %ecx,%ecx
   247cc:	31 f6                	xor    %esi,%esi
   247ce:	31 ff                	xor    %edi,%edi
   247d0:	45 31 c0             	xor    %r8d,%r8d
   247d3:	45 31 c9             	xor    %r9d,%r9d
   247d6:	45 31 d2             	xor    %r10d,%r10d
   247d9:	45 31 db             	xor    %r11d,%r11d
   247dc:	c3                   	ret
   247dd:	0f 1f 00             	nopl   (%rax)
   247e0:	48 89 e6             	mov    %rsp,%rsi
   247e3:	4c 89 d7             	mov    %r10,%rdi
   247e6:	48 c7 04 24 00 00 00 	movq   $0x0,(%rsp)
   247ed:	00 
   247ee:	c7 44 24 08 00 00 00 	movl   $0x0,0x8(%rsp)
   247f5:	00 
   247f6:	e8 b5 e7 ff ff       	call   22fb0 <dsp_sub56.lto_priv.0>
   247fb:	48 8b 04 24          	mov    (%rsp),%rax
   247ff:	49 89 02             	mov    %rax,(%r10)
   24802:	8b 44 24 08          	mov    0x8(%rsp),%eax
   24806:	41 89 42 08          	mov    %eax,0x8(%r10)
   2480a:	48 83 c4 10          	add    $0x10,%rsp
   2480e:	5b                   	pop    %rbx
   2480f:	31 c0                	xor    %eax,%eax
   24811:	31 d2                	xor    %edx,%edx
   24813:	31 c9                	xor    %ecx,%ecx
   24815:	31 f6                	xor    %esi,%esi
   24817:	31 ff                	xor    %edi,%edi
   24819:	45 31 c0             	xor    %r8d,%r8d
   2481c:	45 31 c9             	xor    %r9d,%r9d
   2481f:	45 31 d2             	xor    %r10d,%r10d
   24822:	45 31 db             	xor    %r11d,%r11d
   24825:	c3                   	ret

Disassembly of section .fini:
