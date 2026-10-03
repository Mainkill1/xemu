
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000024d10 <benchmark_helper>:
   24d10:	41 56                	push   %r14
   24d12:	41 55                	push   %r13
   24d14:	41 54                	push   %r12
   24d16:	55                   	push   %rbp
   24d17:	53                   	push   %rbx
   24d18:	48 83 ec 10          	sub    $0x10,%rsp
   24d1c:	48 85 f6             	test   %rsi,%rsi
   24d1f:	74 7f                	je     24da0 <benchmark_helper+0x90>
   24d21:	49 89 fc             	mov    %rdi,%r12
   24d24:	48 89 f5             	mov    %rsi,%rbp
   24d27:	45 31 ed             	xor    %r13d,%r13d
   24d2a:	31 db                	xor    %ebx,%ebx
   24d2c:	4c 8d 74 24 04       	lea    0x4(%rsp),%r14
   24d31:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   24d38:	00 00 00 00 
   24d3c:	0f 1f 40 00          	nopl   0x0(%rax)
   24d40:	4c 89 e8             	mov    %r13,%rax
   24d43:	4c 89 f2             	mov    %r14,%rdx
   24d46:	49 83 c5 01          	add    $0x1,%r13
   24d4a:	49 c7 06 00 00 00 00 	movq   $0x0,(%r14)
   24d51:	25 ff 0f 00 00       	and    $0xfff,%eax
   24d56:	41 c7 46 08 00 00 00 	movl   $0x0,0x8(%r14)
   24d5d:	00 
   24d5e:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   24d62:	49 8d 04 84          	lea    (%r12,%rax,4),%rax
   24d66:	0f b6 48 08          	movzbl 0x8(%rax),%ecx
   24d6a:	8b 70 04             	mov    0x4(%rax),%esi
   24d6d:	8b 38                	mov    (%rax),%edi
   24d6f:	e8 7c f9 ff ff       	call   246f0 <dsp_mul56.lto_priv.0>
   24d74:	8b 44 24 08          	mov    0x8(%rsp),%eax
   24d78:	03 44 24 04          	add    0x4(%rsp),%eax
   24d7c:	03 44 24 0c          	add    0xc(%rsp),%eax
   24d80:	48 01 c3             	add    %rax,%rbx
   24d83:	4c 39 ed             	cmp    %r13,%rbp
   24d86:	75 b8                	jne    24d40 <benchmark_helper+0x30>
   24d88:	48 83 c4 10          	add    $0x10,%rsp
   24d8c:	48 89 d8             	mov    %rbx,%rax
   24d8f:	5b                   	pop    %rbx
   24d90:	5d                   	pop    %rbp
   24d91:	41 5c                	pop    %r12
   24d93:	41 5d                	pop    %r13
   24d95:	41 5e                	pop    %r14
   24d97:	31 d2                	xor    %edx,%edx
   24d99:	31 c9                	xor    %ecx,%ecx
   24d9b:	31 f6                	xor    %esi,%esi
   24d9d:	31 ff                	xor    %edi,%edi
   24d9f:	c3                   	ret
   24da0:	31 db                	xor    %ebx,%ebx
   24da2:	48 83 c4 10          	add    $0x10,%rsp
   24da6:	48 89 d8             	mov    %rbx,%rax
   24da9:	5b                   	pop    %rbx
   24daa:	5d                   	pop    %rbp
   24dab:	41 5c                	pop    %r12
   24dad:	41 5d                	pop    %r13
   24daf:	41 5e                	pop    %r14
   24db1:	31 d2                	xor    %edx,%edx
   24db3:	31 c9                	xor    %ecx,%ecx
   24db5:	31 f6                	xor    %esi,%esi
   24db7:	31 ff                	xor    %edi,%edi
   24db9:	c3                   	ret

Disassembly of section .fini:
