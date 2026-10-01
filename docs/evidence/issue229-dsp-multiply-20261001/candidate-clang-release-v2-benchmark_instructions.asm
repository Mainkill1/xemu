
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

0000000000058650 <benchmark_instructions>:
   58650:	41 57                	push   %r15
   58652:	41 56                	push   %r14
   58654:	53                   	push   %rbx
   58655:	48 85 f6             	test   %rsi,%rsi
   58658:	74 4f                	je     586a9 <benchmark_instructions+0x59>
   5865a:	49 89 f6             	mov    %rsi,%r14
   5865d:	49 89 ff             	mov    %rdi,%r15
   58660:	31 db                	xor    %ebx,%ebx
   58662:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
   58669:	1f 84 00 00 00 00 00 
   58670:	41 81 67 0c ff 0f 00 	andl   $0xfff,0xc(%r15)
   58677:	00 
   58678:	4c 89 ff             	mov    %r15,%rdi
   5867b:	e8 80 0c ff ff       	call   49300 <dsp56k_execute_instruction>
   58680:	41 8b 47 40          	mov    0x40(%r15),%eax
   58684:	41 03 47 30          	add    0x30(%r15),%eax
   58688:	41 03 47 38          	add    0x38(%r15),%eax
   5868c:	41 03 47 34          	add    0x34(%r15),%eax
   58690:	41 03 47 44          	add    0x44(%r15),%eax
   58694:	41 03 47 3c          	add    0x3c(%r15),%eax
   58698:	41 03 87 f4 00 00 00 	add    0xf4(%r15),%eax
   5869f:	48 01 c3             	add    %rax,%rbx
   586a2:	49 ff ce             	dec    %r14
   586a5:	75 c9                	jne    58670 <benchmark_instructions+0x20>
   586a7:	eb 02                	jmp    586ab <benchmark_instructions+0x5b>
   586a9:	31 db                	xor    %ebx,%ebx
   586ab:	48 89 d8             	mov    %rbx,%rax
   586ae:	5b                   	pop    %rbx
   586af:	41 5e                	pop    %r14
   586b1:	41 5f                	pop    %r15
   586b3:	31 ff                	xor    %edi,%edi
   586b5:	31 f6                	xor    %esi,%esi
   586b7:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
