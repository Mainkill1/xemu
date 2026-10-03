
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000001b290 <benchmark_helper_call>:
   1b290:	41 56                	push   %r14
   1b292:	48 8d 05 57 94 00 00 	lea    0x9457(%rip),%rax        # 246f0 <dsp_mul56.lto_priv.0>
   1b299:	41 55                	push   %r13
   1b29b:	41 54                	push   %r12
   1b29d:	55                   	push   %rbp
   1b29e:	53                   	push   %rbx
   1b29f:	48 83 ec 20          	sub    $0x20,%rsp
   1b2a3:	48 89 44 24 08       	mov    %rax,0x8(%rsp)
   1b2a8:	48 85 f6             	test   %rsi,%rsi
   1b2ab:	0f 84 8f 00 00 00    	je     1b340 <benchmark_helper_call+0xb0>
   1b2b1:	49 89 fc             	mov    %rdi,%r12
   1b2b4:	48 89 f5             	mov    %rsi,%rbp
   1b2b7:	45 31 ed             	xor    %r13d,%r13d
   1b2ba:	31 db                	xor    %ebx,%ebx
   1b2bc:	4c 8d 74 24 14       	lea    0x14(%rsp),%r14
   1b2c1:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   1b2c8:	00 00 00 00 
   1b2cc:	0f 1f 40 00          	nopl   0x0(%rax)
   1b2d0:	4c 89 e8             	mov    %r13,%rax
   1b2d3:	49 c7 06 00 00 00 00 	movq   $0x0,(%r14)
   1b2da:	4c 8b 44 24 08       	mov    0x8(%rsp),%r8
   1b2df:	4c 89 f2             	mov    %r14,%rdx
   1b2e2:	25 ff 0f 00 00       	and    $0xfff,%eax
   1b2e7:	41 c7 46 08 00 00 00 	movl   $0x0,0x8(%r14)
   1b2ee:	00 
   1b2ef:	49 83 c5 01          	add    $0x1,%r13
   1b2f3:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   1b2f7:	49 8d 04 84          	lea    (%r12,%rax,4),%rax
   1b2fb:	0f b6 48 08          	movzbl 0x8(%rax),%ecx
   1b2ff:	8b 70 04             	mov    0x4(%rax),%esi
   1b302:	8b 38                	mov    (%rax),%edi
   1b304:	41 ff d0             	call   *%r8
   1b307:	8b 44 24 18          	mov    0x18(%rsp),%eax
   1b30b:	03 44 24 14          	add    0x14(%rsp),%eax
   1b30f:	03 44 24 1c          	add    0x1c(%rsp),%eax
   1b313:	48 01 c3             	add    %rax,%rbx
   1b316:	4c 39 ed             	cmp    %r13,%rbp
   1b319:	75 b5                	jne    1b2d0 <benchmark_helper_call+0x40>
   1b31b:	48 83 c4 20          	add    $0x20,%rsp
   1b31f:	48 89 d8             	mov    %rbx,%rax
   1b322:	5b                   	pop    %rbx
   1b323:	5d                   	pop    %rbp
   1b324:	41 5c                	pop    %r12
   1b326:	41 5d                	pop    %r13
   1b328:	41 5e                	pop    %r14
   1b32a:	31 d2                	xor    %edx,%edx
   1b32c:	31 c9                	xor    %ecx,%ecx
   1b32e:	31 f6                	xor    %esi,%esi
   1b330:	31 ff                	xor    %edi,%edi
   1b332:	45 31 c0             	xor    %r8d,%r8d
   1b335:	c3                   	ret
   1b336:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   1b33d:	00 00 00 
   1b340:	31 db                	xor    %ebx,%ebx
   1b342:	48 83 c4 20          	add    $0x20,%rsp
   1b346:	48 89 d8             	mov    %rbx,%rax
   1b349:	5b                   	pop    %rbx
   1b34a:	5d                   	pop    %rbp
   1b34b:	41 5c                	pop    %r12
   1b34d:	41 5d                	pop    %r13
   1b34f:	41 5e                	pop    %r14
   1b351:	31 d2                	xor    %edx,%edx
   1b353:	31 c9                	xor    %ecx,%ecx
   1b355:	31 f6                	xor    %esi,%esi
   1b357:	31 ff                	xor    %edi,%edi
   1b359:	45 31 c0             	xor    %r8d,%r8d
   1b35c:	c3                   	ret

Disassembly of section .fini:
