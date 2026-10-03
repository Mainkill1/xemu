
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

000000000005cd10 <benchmark_instructions>:
   5cd10:	41 57                	push   %r15
   5cd12:	41 56                	push   %r14
   5cd14:	53                   	push   %rbx
   5cd15:	48 85 f6             	test   %rsi,%rsi
   5cd18:	74 4f                	je     5cd69 <benchmark_instructions+0x59>
   5cd1a:	49 89 f6             	mov    %rsi,%r14
   5cd1d:	49 89 ff             	mov    %rdi,%r15
   5cd20:	31 db                	xor    %ebx,%ebx
   5cd22:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   5cd29:	1f 84 00 00 00 00 00 
   5cd30:	41 81 67 0c ff 0f 00 	andl   $0xfff,0xc(%r15)
   5cd37:	00 
   5cd38:	4c 89 ff             	mov    %r15,%rdi
   5cd3b:	e8 70 c5 fe ff       	call   492b0 <dsp56k_execute_instruction>
   5cd40:	41 8b 47 40          	mov    0x40(%r15),%eax
   5cd44:	41 03 47 30          	add    0x30(%r15),%eax
   5cd48:	41 03 47 38          	add    0x38(%r15),%eax
   5cd4c:	41 03 47 34          	add    0x34(%r15),%eax
   5cd50:	41 03 47 44          	add    0x44(%r15),%eax
   5cd54:	41 03 47 3c          	add    0x3c(%r15),%eax
   5cd58:	41 03 87 f4 00 00 00 	add    0xf4(%r15),%eax
   5cd5f:	48 01 c3             	add    %rax,%rbx
   5cd62:	49 ff ce             	dec    %r14
   5cd65:	75 c9                	jne    5cd30 <benchmark_instructions+0x20>
   5cd67:	eb 02                	jmp    5cd6b <benchmark_instructions+0x5b>
   5cd69:	31 db                	xor    %ebx,%ebx
   5cd6b:	48 89 d8             	mov    %rbx,%rax
   5cd6e:	5b                   	pop    %rbx
   5cd6f:	41 5e                	pop    %r14
   5cd71:	41 5f                	pop    %r15
   5cd73:	31 ff                	xor    %edi,%edi
   5cd75:	31 f6                	xor    %esi,%esi
   5cd77:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
