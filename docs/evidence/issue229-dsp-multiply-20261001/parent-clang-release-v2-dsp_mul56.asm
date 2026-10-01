
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

000000000005cd80 <dsp_mul56>:
   5cd80:	89 f8                	mov    %edi,%eax
   5cd82:	25 00 00 80 00       	and    $0x800000,%eax
   5cd87:	41 b9 00 00 00 01    	mov    $0x1000000,%r9d
   5cd8d:	41 29 f9             	sub    %edi,%r9d
   5cd90:	85 c0                	test   %eax,%eax
   5cd92:	44 0f 44 cf          	cmove  %edi,%r9d
   5cd96:	bf 00 00 00 01       	mov    $0x1000000,%edi
   5cd9b:	c1 e8 17             	shr    $0x17,%eax
   5cd9e:	31 c8                	xor    %ecx,%eax
   5cda0:	89 f1                	mov    %esi,%ecx
   5cda2:	81 e1 00 00 80 00    	and    $0x800000,%ecx
   5cda8:	29 f7                	sub    %esi,%edi
   5cdaa:	85 c9                	test   %ecx,%ecx
   5cdac:	0f 44 fe             	cmove  %esi,%edi
   5cdaf:	c1 e9 17             	shr    $0x17,%ecx
   5cdb2:	45 89 c8             	mov    %r9d,%r8d
   5cdb5:	41 81 e0 ff 0f 00 00 	and    $0xfff,%r8d
   5cdbc:	89 fe                	mov    %edi,%esi
   5cdbe:	81 e6 ff 0f 00 00    	and    $0xfff,%esi
   5cdc4:	41 c1 e9 0c          	shr    $0xc,%r9d
   5cdc8:	41 81 e1 ff 0f 00 00 	and    $0xfff,%r9d
   5cdcf:	45 89 ca             	mov    %r9d,%r10d
   5cdd2:	44 0f af d6          	imul   %esi,%r10d
   5cdd6:	41 0f af f0          	imul   %r8d,%esi
   5cdda:	c1 ef 0c             	shr    $0xc,%edi
   5cddd:	81 e7 ff 0f 00 00    	and    $0xfff,%edi
   5cde3:	44 0f af c7          	imul   %edi,%r8d
   5cde7:	41 0f af f9          	imul   %r9d,%edi
   5cdeb:	45 89 d1             	mov    %r10d,%r9d
   5cdee:	41 c1 e1 0c          	shl    $0xc,%r9d
   5cdf2:	41 81 e1 00 f0 ff 00 	and    $0xfff000,%r9d
   5cdf9:	41 01 f1             	add    %esi,%r9d
   5cdfc:	44 89 c6             	mov    %r8d,%esi
   5cdff:	c1 e6 0c             	shl    $0xc,%esi
   5ce02:	81 e6 00 f0 ff 00    	and    $0xfff000,%esi
   5ce08:	44 01 ce             	add    %r9d,%esi
   5ce0b:	41 c1 ea 0c          	shr    $0xc,%r10d
   5ce0f:	41 01 fa             	add    %edi,%r10d
   5ce12:	41 c1 e8 0c          	shr    $0xc,%r8d
   5ce16:	45 01 d0             	add    %r10d,%r8d
   5ce19:	89 f7                	mov    %esi,%edi
   5ce1b:	c1 ef 18             	shr    $0x18,%edi
   5ce1e:	44 01 c7             	add    %r8d,%edi
   5ce21:	49 89 f9             	mov    %rdi,%r9
   5ce24:	49 c1 e1 19          	shl    $0x19,%r9
   5ce28:	01 f6                	add    %esi,%esi
   5ce2a:	41 89 f0             	mov    %esi,%r8d
   5ce2d:	41 81 e0 00 00 00 01 	and    $0x1000000,%r8d
   5ce34:	4d 09 c8             	or     %r9,%r8
   5ce37:	81 e6 fe ff ff 00    	and    $0xfffffe,%esi
   5ce3d:	89 72 08             	mov    %esi,0x8(%rdx)
   5ce40:	49 c1 e8 18          	shr    $0x18,%r8
   5ce44:	41 81 e0 ff ff ff 00 	and    $0xffffff,%r8d
   5ce4b:	44 89 42 04          	mov    %r8d,0x4(%rdx)
   5ce4f:	c1 ef 17             	shr    $0x17,%edi
   5ce52:	89 3a                	mov    %edi,(%rdx)
   5ce54:	38 c8                	cmp    %cl,%al
   5ce56:	74 27                	je     5ce7f <dsp_mul56+0xff>
   5ce58:	f7 de                	neg    %esi
   5ce5a:	89 f0                	mov    %esi,%eax
   5ce5c:	c1 f8 1f             	sar    $0x1f,%eax
   5ce5f:	44 29 c0             	sub    %r8d,%eax
   5ce62:	89 c1                	mov    %eax,%ecx
   5ce64:	c1 e9 18             	shr    $0x18,%ecx
   5ce67:	29 f9                	sub    %edi,%ecx
   5ce69:	81 e6 fe ff ff 00    	and    $0xfffffe,%esi
   5ce6f:	25 ff ff ff 00       	and    $0xffffff,%eax
   5ce74:	0f b6 c9             	movzbl %cl,%ecx
   5ce77:	89 0a                	mov    %ecx,(%rdx)
   5ce79:	89 42 04             	mov    %eax,0x4(%rdx)
   5ce7c:	89 72 08             	mov    %esi,0x8(%rdx)
   5ce7f:	31 c0                	xor    %eax,%eax
   5ce81:	31 c9                	xor    %ecx,%ecx
   5ce83:	31 ff                	xor    %edi,%edi
   5ce85:	31 d2                	xor    %edx,%edx
   5ce87:	31 f6                	xor    %esi,%esi
   5ce89:	45 31 c0             	xor    %r8d,%r8d
   5ce8c:	45 31 c9             	xor    %r9d,%r9d
   5ce8f:	45 31 d2             	xor    %r10d,%r10d
   5ce92:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
