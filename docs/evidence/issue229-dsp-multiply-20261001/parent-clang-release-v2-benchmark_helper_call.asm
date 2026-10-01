
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

000000000005c6c0 <benchmark_helper_call>:
   5c6c0:	41 57                	push   %r15
   5c6c2:	41 56                	push   %r14
   5c6c4:	41 55                	push   %r13
   5c6c6:	41 54                	push   %r12
   5c6c8:	53                   	push   %rbx
   5c6c9:	48 83 ec 20          	sub    $0x20,%rsp
   5c6cd:	48 8d 05 ac 06 00 00 	lea    0x6ac(%rip),%rax        # 5cd80 <dsp_mul56>
   5c6d4:	48 89 44 24 18       	mov    %rax,0x18(%rsp)
   5c6d9:	48 85 f6             	test   %rsi,%rsi
   5c6dc:	74 62                	je     5c740 <benchmark_helper_call+0x80>
   5c6de:	48 89 f3             	mov    %rsi,%rbx
   5c6e1:	49 89 fe             	mov    %rdi,%r14
   5c6e4:	45 31 ff             	xor    %r15d,%r15d
   5c6e7:	4c 8d 64 24 08       	lea    0x8(%rsp),%r12
   5c6ec:	45 31 ed             	xor    %r13d,%r13d
   5c6ef:	90                   	nop
   5c6f0:	44 89 e8             	mov    %r13d,%eax
   5c6f3:	25 ff 0f 00 00       	and    $0xfff,%eax
   5c6f8:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   5c6fc:	c7 44 24 10 00 00 00 	movl   $0x0,0x10(%rsp)
   5c703:	00 
   5c704:	48 c7 44 24 08 00 00 	movq   $0x0,0x8(%rsp)
   5c70b:	00 00 
   5c70d:	4c 8b 44 24 18       	mov    0x18(%rsp),%r8
   5c712:	41 8b 3c 86          	mov    (%r14,%rax,4),%edi
   5c716:	41 8b 74 86 04       	mov    0x4(%r14,%rax,4),%esi
   5c71b:	41 0f b6 4c 86 08    	movzbl 0x8(%r14,%rax,4),%ecx
   5c721:	4c 89 e2             	mov    %r12,%rdx
   5c724:	41 ff d0             	call   *%r8
   5c727:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   5c72b:	03 44 24 08          	add    0x8(%rsp),%eax
   5c72f:	03 44 24 10          	add    0x10(%rsp),%eax
   5c733:	49 01 c7             	add    %rax,%r15
   5c736:	49 ff c5             	inc    %r13
   5c739:	4c 39 eb             	cmp    %r13,%rbx
   5c73c:	75 b2                	jne    5c6f0 <benchmark_helper_call+0x30>
   5c73e:	eb 03                	jmp    5c743 <benchmark_helper_call+0x83>
   5c740:	45 31 ff             	xor    %r15d,%r15d
   5c743:	4c 89 f8             	mov    %r15,%rax
   5c746:	48 83 c4 20          	add    $0x20,%rsp
   5c74a:	5b                   	pop    %rbx
   5c74b:	41 5c                	pop    %r12
   5c74d:	41 5d                	pop    %r13
   5c74f:	41 5e                	pop    %r14
   5c751:	41 5f                	pop    %r15
   5c753:	31 c9                	xor    %ecx,%ecx
   5c755:	31 ff                	xor    %edi,%edi
   5c757:	31 d2                	xor    %edx,%edx
   5c759:	31 f6                	xor    %esi,%esi
   5c75b:	45 31 c0             	xor    %r8d,%r8d
   5c75e:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
