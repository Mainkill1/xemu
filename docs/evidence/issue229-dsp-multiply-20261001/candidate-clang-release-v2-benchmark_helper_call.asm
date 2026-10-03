
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

00000000000582c0 <benchmark_helper_call>:
   582c0:	41 57                	push   %r15
   582c2:	41 56                	push   %r14
   582c4:	41 55                	push   %r13
   582c6:	41 54                	push   %r12
   582c8:	53                   	push   %rbx
   582c9:	48 83 ec 20          	sub    $0x20,%rsp
   582cd:	48 8d 05 ec 03 00 00 	lea    0x3ec(%rip),%rax        # 586c0 <dsp_mul56>
   582d4:	48 89 44 24 18       	mov    %rax,0x18(%rsp)
   582d9:	48 85 f6             	test   %rsi,%rsi
   582dc:	74 62                	je     58340 <benchmark_helper_call+0x80>
   582de:	48 89 f3             	mov    %rsi,%rbx
   582e1:	49 89 fe             	mov    %rdi,%r14
   582e4:	45 31 ff             	xor    %r15d,%r15d
   582e7:	4c 8d 64 24 08       	lea    0x8(%rsp),%r12
   582ec:	45 31 ed             	xor    %r13d,%r13d
   582ef:	90                   	nop
   582f0:	44 89 e8             	mov    %r13d,%eax
   582f3:	25 ff 0f 00 00       	and    $0xfff,%eax
   582f8:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   582fc:	c7 44 24 10 00 00 00 	movl   $0x0,0x10(%rsp)
   58303:	00 
   58304:	48 c7 44 24 08 00 00 	movq   $0x0,0x8(%rsp)
   5830b:	00 00 
   5830d:	4c 8b 44 24 18       	mov    0x18(%rsp),%r8
   58312:	41 8b 3c 86          	mov    (%r14,%rax,4),%edi
   58316:	41 8b 74 86 04       	mov    0x4(%r14,%rax,4),%esi
   5831b:	41 0f b6 4c 86 08    	movzbl 0x8(%r14,%rax,4),%ecx
   58321:	4c 89 e2             	mov    %r12,%rdx
   58324:	41 ff d0             	call   *%r8
   58327:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   5832b:	03 44 24 08          	add    0x8(%rsp),%eax
   5832f:	03 44 24 10          	add    0x10(%rsp),%eax
   58333:	49 01 c7             	add    %rax,%r15
   58336:	49 ff c5             	inc    %r13
   58339:	4c 39 eb             	cmp    %r13,%rbx
   5833c:	75 b2                	jne    582f0 <benchmark_helper_call+0x30>
   5833e:	eb 03                	jmp    58343 <benchmark_helper_call+0x83>
   58340:	45 31 ff             	xor    %r15d,%r15d
   58343:	4c 89 f8             	mov    %r15,%rax
   58346:	48 83 c4 20          	add    $0x20,%rsp
   5834a:	5b                   	pop    %rbx
   5834b:	41 5c                	pop    %r12
   5834d:	41 5d                	pop    %r13
   5834f:	41 5e                	pop    %r14
   58351:	41 5f                	pop    %r15
   58353:	31 c9                	xor    %ecx,%ecx
   58355:	31 ff                	xor    %edi,%edi
   58357:	31 d2                	xor    %edx,%edx
   58359:	31 f6                	xor    %esi,%esi
   5835b:	45 31 c0             	xor    %r8d,%r8d
   5835e:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
