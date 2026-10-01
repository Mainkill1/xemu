
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

00000000000586c0 <dsp_mul56>:
   586c0:	89 f8                	mov    %edi,%eax
   586c2:	25 ff ff 7f 00       	and    $0x7fffff,%eax
   586c7:	81 e7 00 00 80 00    	and    $0x800000,%edi
   586cd:	48 29 f8             	sub    %rdi,%rax
   586d0:	89 f7                	mov    %esi,%edi
   586d2:	81 e7 ff ff 7f 00    	and    $0x7fffff,%edi
   586d8:	81 e6 00 00 80 00    	and    $0x800000,%esi
   586de:	48 29 f7             	sub    %rsi,%rdi
   586e1:	48 0f af f8          	imul   %rax,%rdi
   586e5:	48 89 f8             	mov    %rdi,%rax
   586e8:	48 f7 d8             	neg    %rax
   586eb:	85 c9                	test   %ecx,%ecx
   586ed:	48 0f 44 c7          	cmove  %rdi,%rax
   586f1:	8d 0c 00             	lea    (%rax,%rax,1),%ecx
   586f4:	48 89 c6             	mov    %rax,%rsi
   586f7:	48 c1 ee 2f          	shr    $0x2f,%rsi
   586fb:	40 0f b6 f6          	movzbl %sil,%esi
   586ff:	89 32                	mov    %esi,(%rdx)
   58701:	48 c1 e8 17          	shr    $0x17,%rax
   58705:	25 ff ff ff 00       	and    $0xffffff,%eax
   5870a:	89 42 04             	mov    %eax,0x4(%rdx)
   5870d:	81 e1 fe ff ff 00    	and    $0xfffffe,%ecx
   58713:	89 4a 08             	mov    %ecx,0x8(%rdx)
   58716:	31 c0                	xor    %eax,%eax
   58718:	31 c9                	xor    %ecx,%ecx
   5871a:	31 ff                	xor    %edi,%edi
   5871c:	31 d2                	xor    %edx,%edx
   5871e:	31 f6                	xor    %esi,%esi
   58720:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
