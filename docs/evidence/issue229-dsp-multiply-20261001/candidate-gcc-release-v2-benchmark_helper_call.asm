
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000001c300 <benchmark_helper_call>:
   1c300:	41 56                	push   %r14
   1c302:	48 8d 05 d7 fd ff ff 	lea    -0x229(%rip),%rax        # 1c0e0 <dsp_mul56.lto_priv.0>
   1c309:	41 55                	push   %r13
   1c30b:	41 54                	push   %r12
   1c30d:	55                   	push   %rbp
   1c30e:	53                   	push   %rbx
   1c30f:	48 83 ec 20          	sub    $0x20,%rsp
   1c313:	48 89 44 24 08       	mov    %rax,0x8(%rsp)
   1c318:	48 85 f6             	test   %rsi,%rsi
   1c31b:	0f 84 8f 00 00 00    	je     1c3b0 <benchmark_helper_call+0xb0>
   1c321:	49 89 fc             	mov    %rdi,%r12
   1c324:	48 89 f5             	mov    %rsi,%rbp
   1c327:	45 31 ed             	xor    %r13d,%r13d
   1c32a:	31 db                	xor    %ebx,%ebx
   1c32c:	4c 8d 74 24 14       	lea    0x14(%rsp),%r14
   1c331:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   1c338:	00 00 00 00 
   1c33c:	0f 1f 40 00          	nopl   0x0(%rax)
   1c340:	4c 89 e8             	mov    %r13,%rax
   1c343:	49 c7 06 00 00 00 00 	movq   $0x0,(%r14)
   1c34a:	4c 8b 44 24 08       	mov    0x8(%rsp),%r8
   1c34f:	4c 89 f2             	mov    %r14,%rdx
   1c352:	25 ff 0f 00 00       	and    $0xfff,%eax
   1c357:	41 c7 46 08 00 00 00 	movl   $0x0,0x8(%r14)
   1c35e:	00 
   1c35f:	49 83 c5 01          	add    $0x1,%r13
   1c363:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   1c367:	49 8d 04 84          	lea    (%r12,%rax,4),%rax
   1c36b:	0f b6 48 08          	movzbl 0x8(%rax),%ecx
   1c36f:	8b 70 04             	mov    0x4(%rax),%esi
   1c372:	8b 38                	mov    (%rax),%edi
   1c374:	41 ff d0             	call   *%r8
   1c377:	8b 44 24 18          	mov    0x18(%rsp),%eax
   1c37b:	03 44 24 14          	add    0x14(%rsp),%eax
   1c37f:	03 44 24 1c          	add    0x1c(%rsp),%eax
   1c383:	48 01 c3             	add    %rax,%rbx
   1c386:	4c 39 ed             	cmp    %r13,%rbp
   1c389:	75 b5                	jne    1c340 <benchmark_helper_call+0x40>
   1c38b:	48 83 c4 20          	add    $0x20,%rsp
   1c38f:	48 89 d8             	mov    %rbx,%rax
   1c392:	5b                   	pop    %rbx
   1c393:	5d                   	pop    %rbp
   1c394:	41 5c                	pop    %r12
   1c396:	41 5d                	pop    %r13
   1c398:	41 5e                	pop    %r14
   1c39a:	31 d2                	xor    %edx,%edx
   1c39c:	31 c9                	xor    %ecx,%ecx
   1c39e:	31 f6                	xor    %esi,%esi
   1c3a0:	31 ff                	xor    %edi,%edi
   1c3a2:	45 31 c0             	xor    %r8d,%r8d
   1c3a5:	c3                   	ret
   1c3a6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   1c3ad:	00 00 00 
   1c3b0:	31 db                	xor    %ebx,%ebx
   1c3b2:	48 83 c4 20          	add    $0x20,%rsp
   1c3b6:	48 89 d8             	mov    %rbx,%rax
   1c3b9:	5b                   	pop    %rbx
   1c3ba:	5d                   	pop    %rbp
   1c3bb:	41 5c                	pop    %r12
   1c3bd:	41 5d                	pop    %r13
   1c3bf:	41 5e                	pop    %r14
   1c3c1:	31 d2                	xor    %edx,%edx
   1c3c3:	31 c9                	xor    %ecx,%ecx
   1c3c5:	31 f6                	xor    %esi,%esi
   1c3c7:	31 ff                	xor    %edi,%edi
   1c3c9:	45 31 c0             	xor    %r8d,%r8d
   1c3cc:	c3                   	ret

Disassembly of section .fini:
