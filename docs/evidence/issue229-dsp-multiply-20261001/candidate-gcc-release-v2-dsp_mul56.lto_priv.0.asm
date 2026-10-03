
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-gcc-release-v2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000001c0e0 <dsp_mul56.lto_priv.0>:
   1c0e0:	41 89 c8             	mov    %ecx,%r8d
   1c0e3:	89 f8                	mov    %edi,%eax
   1c0e5:	89 f1                	mov    %esi,%ecx
   1c0e7:	81 e7 00 00 80 00    	and    $0x800000,%edi
   1c0ed:	25 ff ff 7f 00       	and    $0x7fffff,%eax
   1c0f2:	81 e1 ff ff 7f 00    	and    $0x7fffff,%ecx
   1c0f8:	81 e6 00 00 80 00    	and    $0x800000,%esi
   1c0fe:	48 29 f1             	sub    %rsi,%rcx
   1c101:	48 29 f8             	sub    %rdi,%rax
   1c104:	48 0f af c1          	imul   %rcx,%rax
   1c108:	48 89 c1             	mov    %rax,%rcx
   1c10b:	48 f7 d9             	neg    %rcx
   1c10e:	45 84 c0             	test   %r8b,%r8b
   1c111:	48 0f 45 c1          	cmovne %rcx,%rax
   1c115:	48 01 c0             	add    %rax,%rax
   1c118:	48 89 c1             	mov    %rax,%rcx
   1c11b:	48 c1 e9 30          	shr    $0x30,%rcx
   1c11f:	81 e1 ff 00 00 00    	and    $0xff,%ecx
   1c125:	89 0a                	mov    %ecx,(%rdx)
   1c127:	48 89 c1             	mov    %rax,%rcx
   1c12a:	25 ff ff ff 00       	and    $0xffffff,%eax
   1c12f:	48 c1 e9 18          	shr    $0x18,%rcx
   1c133:	89 42 08             	mov    %eax,0x8(%rdx)
   1c136:	81 e1 ff ff ff 00    	and    $0xffffff,%ecx
   1c13c:	89 4a 04             	mov    %ecx,0x4(%rdx)
   1c13f:	31 c0                	xor    %eax,%eax
   1c141:	31 d2                	xor    %edx,%edx
   1c143:	31 c9                	xor    %ecx,%ecx
   1c145:	31 f6                	xor    %esi,%esi
   1c147:	31 ff                	xor    %edi,%edi
   1c149:	45 31 c0             	xor    %r8d,%r8d
   1c14c:	c3                   	ret

Disassembly of section .fini:
