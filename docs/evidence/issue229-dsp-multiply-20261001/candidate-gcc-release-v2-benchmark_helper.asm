
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000001c240 <benchmark_helper>:
   1c240:	49 89 f0             	mov    %rsi,%r8
   1c243:	48 85 f6             	test   %rsi,%rsi
   1c246:	0f 84 94 00 00 00    	je     1c2e0 <benchmark_helper+0xa0>
   1c24c:	31 c9                	xor    %ecx,%ecx
   1c24e:	31 f6                	xor    %esi,%esi
   1c250:	48 89 c8             	mov    %rcx,%rax
   1c253:	25 ff 0f 00 00       	and    $0xfff,%eax
   1c258:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   1c25c:	4c 8d 14 87          	lea    (%rdi,%rax,4),%r10
   1c260:	45 8b 4a 04          	mov    0x4(%r10),%r9d
   1c264:	41 8b 12             	mov    (%r10),%edx
   1c267:	44 89 c8             	mov    %r9d,%eax
   1c26a:	41 81 e1 00 00 80 00 	and    $0x800000,%r9d
   1c271:	25 ff ff 7f 00       	and    $0x7fffff,%eax
   1c276:	4c 29 c8             	sub    %r9,%rax
   1c279:	41 89 d1             	mov    %edx,%r9d
   1c27c:	81 e2 00 00 80 00    	and    $0x800000,%edx
   1c282:	41 81 e1 ff ff 7f 00 	and    $0x7fffff,%r9d
   1c289:	49 29 d1             	sub    %rdx,%r9
   1c28c:	49 0f af c1          	imul   %r9,%rax
   1c290:	41 80 7a 08 00       	cmpb   $0x0,0x8(%r10)
   1c295:	74 03                	je     1c29a <benchmark_helper+0x5a>
   1c297:	48 f7 d8             	neg    %rax
   1c29a:	48 01 c0             	add    %rax,%rax
   1c29d:	48 83 c1 01          	add    $0x1,%rcx
   1c2a1:	48 89 c2             	mov    %rax,%rdx
   1c2a4:	49 89 c1             	mov    %rax,%r9
   1c2a7:	25 ff ff ff 00       	and    $0xffffff,%eax
   1c2ac:	48 c1 ea 18          	shr    $0x18,%rdx
   1c2b0:	49 c1 e9 30          	shr    $0x30,%r9
   1c2b4:	81 e2 ff ff ff 00    	and    $0xffffff,%edx
   1c2ba:	45 0f b6 c9          	movzbl %r9b,%r9d
   1c2be:	44 01 ca             	add    %r9d,%edx
   1c2c1:	01 d0                	add    %edx,%eax
   1c2c3:	48 01 c6             	add    %rax,%rsi
   1c2c6:	49 39 c8             	cmp    %rcx,%r8
   1c2c9:	75 85                	jne    1c250 <benchmark_helper+0x10>
   1c2cb:	48 89 f0             	mov    %rsi,%rax
   1c2ce:	31 d2                	xor    %edx,%edx
   1c2d0:	31 c9                	xor    %ecx,%ecx
   1c2d2:	31 f6                	xor    %esi,%esi
   1c2d4:	31 ff                	xor    %edi,%edi
   1c2d6:	45 31 c0             	xor    %r8d,%r8d
   1c2d9:	45 31 c9             	xor    %r9d,%r9d
   1c2dc:	45 31 d2             	xor    %r10d,%r10d
   1c2df:	c3                   	ret
   1c2e0:	31 f6                	xor    %esi,%esi
   1c2e2:	48 89 f0             	mov    %rsi,%rax
   1c2e5:	31 d2                	xor    %edx,%edx
   1c2e7:	31 c9                	xor    %ecx,%ecx
   1c2e9:	31 f6                	xor    %esi,%esi
   1c2eb:	31 ff                	xor    %edi,%edi
   1c2ed:	45 31 c0             	xor    %r8d,%r8d
   1c2f0:	45 31 c9             	xor    %r9d,%r9d
   1c2f3:	45 31 d2             	xor    %r10d,%r10d
   1c2f6:	c3                   	ret

Disassembly of section .fini:
