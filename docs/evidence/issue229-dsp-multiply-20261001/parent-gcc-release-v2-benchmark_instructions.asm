
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000022030 <benchmark_instructions>:
   22030:	41 55                	push   %r13
   22032:	41 54                	push   %r12
   22034:	49 89 fc             	mov    %rdi,%r12
   22037:	55                   	push   %rbp
   22038:	48 89 f5             	mov    %rsi,%rbp
   2203b:	53                   	push   %rbx
   2203c:	48 83 ec 08          	sub    $0x8,%rsp
   22040:	45 31 ed             	xor    %r13d,%r13d
   22043:	31 db                	xor    %ebx,%ebx
   22045:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   2204c:	00 00 00 00 
   22050:	41 81 64 24 0c ff 0f 	andl   $0xfff,0xc(%r12)
   22057:	00 00 
   22059:	4c 89 e7             	mov    %r12,%rdi
   2205c:	49 83 c5 01          	add    $0x1,%r13
   22060:	e8 3b f4 ff ff       	call   214a0 <dsp56k_execute_instruction>
   22065:	41 8b 44 24 40       	mov    0x40(%r12),%eax
   2206a:	41 03 44 24 30       	add    0x30(%r12),%eax
   2206f:	41 03 44 24 38       	add    0x38(%r12),%eax
   22074:	41 03 44 24 34       	add    0x34(%r12),%eax
   22079:	41 03 44 24 44       	add    0x44(%r12),%eax
   2207e:	41 03 44 24 3c       	add    0x3c(%r12),%eax
   22083:	41 03 84 24 f4 00 00 	add    0xf4(%r12),%eax
   2208a:	00 
   2208b:	48 01 c3             	add    %rax,%rbx
   2208e:	4c 39 ed             	cmp    %r13,%rbp
   22091:	75 bd                	jne    22050 <benchmark_instructions+0x20>
   22093:	48 83 c4 08          	add    $0x8,%rsp
   22097:	48 89 d8             	mov    %rbx,%rax
   2209a:	5b                   	pop    %rbx
   2209b:	5d                   	pop    %rbp
   2209c:	41 5c                	pop    %r12
   2209e:	41 5d                	pop    %r13
   220a0:	31 f6                	xor    %esi,%esi
   220a2:	31 ff                	xor    %edi,%edi
   220a4:	c3                   	ret

Disassembly of section .fini:
