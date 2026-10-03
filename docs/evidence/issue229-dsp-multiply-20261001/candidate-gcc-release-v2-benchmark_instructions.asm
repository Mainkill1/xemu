
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000023560 <benchmark_instructions>:
   23560:	41 55                	push   %r13
   23562:	41 54                	push   %r12
   23564:	49 89 fc             	mov    %rdi,%r12
   23567:	55                   	push   %rbp
   23568:	48 89 f5             	mov    %rsi,%rbp
   2356b:	53                   	push   %rbx
   2356c:	48 83 ec 08          	sub    $0x8,%rsp
   23570:	45 31 ed             	xor    %r13d,%r13d
   23573:	31 db                	xor    %ebx,%ebx
   23575:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   2357c:	00 00 00 00 
   23580:	41 81 64 24 0c ff 0f 	andl   $0xfff,0xc(%r12)
   23587:	00 00 
   23589:	4c 89 e7             	mov    %r12,%rdi
   2358c:	49 83 c5 01          	add    $0x1,%r13
   23590:	e8 3b f4 ff ff       	call   229d0 <dsp56k_execute_instruction>
   23595:	41 8b 44 24 40       	mov    0x40(%r12),%eax
   2359a:	41 03 44 24 30       	add    0x30(%r12),%eax
   2359f:	41 03 44 24 38       	add    0x38(%r12),%eax
   235a4:	41 03 44 24 34       	add    0x34(%r12),%eax
   235a9:	41 03 44 24 44       	add    0x44(%r12),%eax
   235ae:	41 03 44 24 3c       	add    0x3c(%r12),%eax
   235b3:	41 03 84 24 f4 00 00 	add    0xf4(%r12),%eax
   235ba:	00 
   235bb:	48 01 c3             	add    %rax,%rbx
   235be:	4c 39 ed             	cmp    %r13,%rbp
   235c1:	75 bd                	jne    23580 <benchmark_instructions+0x20>
   235c3:	48 83 c4 08          	add    $0x8,%rsp
   235c7:	48 89 d8             	mov    %rbx,%rax
   235ca:	5b                   	pop    %rbx
   235cb:	5d                   	pop    %rbp
   235cc:	41 5c                	pop    %r12
   235ce:	41 5d                	pop    %r13
   235d0:	31 f6                	xor    %esi,%esi
   235d2:	31 ff                	xor    %edi,%edi
   235d4:	c3                   	ret

Disassembly of section .fini:
