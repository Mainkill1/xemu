
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/parent-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

000000000005c760 <benchmark_helper>:
   5c760:	55                   	push   %rbp
   5c761:	41 56                	push   %r14
   5c763:	53                   	push   %rbx
   5c764:	48 83 ec 10          	sub    $0x10,%rsp
   5c768:	48 85 f6             	test   %rsi,%rsi
   5c76b:	74 18                	je     5c785 <benchmark_helper+0x25>
   5c76d:	48 8d 86 ff ef ff ff 	lea    -0x1001(%rsi),%rax
   5c774:	48 3d 06 f0 ff ff    	cmp    $0xfffffffffffff006,%rax
   5c77a:	77 10                	ja     5c78c <benchmark_helper+0x2c>
   5c77c:	31 c0                	xor    %eax,%eax
   5c77e:	31 c9                	xor    %ecx,%ecx
   5c780:	e9 5c 04 00 00       	jmp    5cbe1 <benchmark_helper+0x481>
   5c785:	31 c0                	xor    %eax,%eax
   5c787:	e9 1e 04 00 00       	jmp    5cbaa <benchmark_helper+0x44a>
   5c78c:	89 f1                	mov    %esi,%ecx
   5c78e:	81 e1 f8 1f 00 00    	and    $0x1ff8,%ecx
   5c794:	8d 04 b5 00 00 00 00 	lea    0x0(,%rsi,4),%eax
   5c79b:	25 e0 7f 00 00       	and    $0x7fe0,%eax
   5c7a0:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   5c7a4:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
   5c7a8:	31 d2                	xor    %edx,%edx
   5c7aa:	c4 e2 79 18 0d a1 04 	vbroadcastss -0x4fb5f(%rip),%xmm1        # cc54 <_IO_stdin_used+0x4>
   5c7b1:	fb ff 
   5c7b3:	c5 f8 29 4c 24 a0    	vmovaps %xmm1,-0x60(%rsp)
   5c7b9:	c4 e2 79 18 15 4a 04 	vbroadcastss -0x4fbb6(%rip),%xmm2        # cc0c <__abi_tag+0xc910>
   5c7c0:	fb ff 
   5c7c2:	c5 f8 29 54 24 e0    	vmovaps %xmm2,-0x20(%rsp)
   5c7c8:	c4 e2 79 18 0d 8b 04 	vbroadcastss -0x4fb75(%rip),%xmm1        # cc5c <_IO_stdin_used+0xc>
   5c7cf:	fb ff 
   5c7d1:	c5 f8 29 4c 24 80    	vmovaps %xmm1,-0x80(%rsp)
   5c7d7:	c4 e2 79 18 15 54 04 	vbroadcastss -0x4fbac(%rip),%xmm2        # cc34 <__abi_tag+0xc938>
   5c7de:	fb ff 
   5c7e0:	c5 f8 29 54 24 d0    	vmovaps %xmm2,-0x30(%rsp)
   5c7e6:	c4 e2 79 58 15 35 04 	vpbroadcastd -0x4fbcb(%rip),%xmm2        # cc24 <__abi_tag+0xc928>
   5c7ed:	fb ff 
   5c7ef:	c5 f9 7f 54 24 c0    	vmovdqa %xmm2,-0x40(%rsp)
   5c7f5:	c4 e2 79 18 0d 12 04 	vbroadcastss -0x4fbee(%rip),%xmm1        # cc10 <__abi_tag+0xc914>
   5c7fc:	fb ff 
   5c7fe:	c5 f8 29 4c 24 b0    	vmovaps %xmm1,-0x50(%rsp)
   5c804:	c4 e2 79 58 0d f7 03 	vpbroadcastd -0x4fc09(%rip),%xmm1        # cc04 <__abi_tag+0xc908>
   5c80b:	fb ff 
   5c80d:	c5 f9 7f 4c 24 90    	vmovdqa %xmm1,-0x70(%rsp)
   5c813:	c5 f1 ef c9          	vpxor  %xmm1,%xmm1,%xmm1
   5c817:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
   5c81e:	00 00 
   5c820:	c5 fe 7f 4c 24 f0    	vmovdqu %ymm1,-0x10(%rsp)
   5c826:	c5 f9 6e 14 17       	vmovd  (%rdi,%rdx,1),%xmm2
   5c82b:	c4 e3 69 22 54 17 0c 	vpinsrd $0x1,0xc(%rdi,%rdx,1),%xmm2,%xmm2
   5c832:	01 
   5c833:	c4 e3 69 22 54 17 18 	vpinsrd $0x2,0x18(%rdi,%rdx,1),%xmm2,%xmm2
   5c83a:	02 
   5c83b:	c4 63 69 22 44 17 24 	vpinsrd $0x3,0x24(%rdi,%rdx,1),%xmm2,%xmm8
   5c842:	03 
   5c843:	c5 f9 6e 54 17 30    	vmovd  0x30(%rdi,%rdx,1),%xmm2
   5c849:	c4 e3 69 22 54 17 3c 	vpinsrd $0x1,0x3c(%rdi,%rdx,1),%xmm2,%xmm2
   5c850:	01 
   5c851:	c4 e3 69 22 54 17 48 	vpinsrd $0x2,0x48(%rdi,%rdx,1),%xmm2,%xmm2
   5c858:	02 
   5c859:	c4 63 69 22 64 17 54 	vpinsrd $0x3,0x54(%rdi,%rdx,1),%xmm2,%xmm12
   5c860:	03 
   5c861:	c5 f9 6e 54 17 04    	vmovd  0x4(%rdi,%rdx,1),%xmm2
   5c867:	c4 e3 69 22 54 17 10 	vpinsrd $0x1,0x10(%rdi,%rdx,1),%xmm2,%xmm2
   5c86e:	01 
   5c86f:	c4 e3 69 22 54 17 1c 	vpinsrd $0x2,0x1c(%rdi,%rdx,1),%xmm2,%xmm2
   5c876:	02 
   5c877:	c4 e3 69 22 54 17 28 	vpinsrd $0x3,0x28(%rdi,%rdx,1),%xmm2,%xmm2
   5c87e:	03 
   5c87f:	c5 79 6e 6c 17 34    	vmovd  0x34(%rdi,%rdx,1),%xmm13
   5c885:	c4 63 11 22 6c 17 40 	vpinsrd $0x1,0x40(%rdi,%rdx,1),%xmm13,%xmm13
   5c88c:	01 
   5c88d:	c4 63 11 22 6c 17 4c 	vpinsrd $0x2,0x4c(%rdi,%rdx,1),%xmm13,%xmm13
   5c894:	02 
   5c895:	c4 63 11 22 6c 17 58 	vpinsrd $0x3,0x58(%rdi,%rdx,1),%xmm13,%xmm13
   5c89c:	03 
   5c89d:	c5 79 6e 74 17 08    	vmovd  0x8(%rdi,%rdx,1),%xmm14
   5c8a3:	c4 63 09 20 74 17 14 	vpinsrb $0x1,0x14(%rdi,%rdx,1),%xmm14,%xmm14
   5c8aa:	01 
   5c8ab:	c4 63 09 20 74 17 20 	vpinsrb $0x2,0x20(%rdi,%rdx,1),%xmm14,%xmm14
   5c8b2:	02 
   5c8b3:	c4 63 09 20 7c 17 2c 	vpinsrb $0x3,0x2c(%rdi,%rdx,1),%xmm14,%xmm15
   5c8ba:	03 
   5c8bb:	c5 79 6e 74 17 38    	vmovd  0x38(%rdi,%rdx,1),%xmm14
   5c8c1:	c4 63 09 20 74 17 44 	vpinsrb $0x1,0x44(%rdi,%rdx,1),%xmm14,%xmm14
   5c8c8:	01 
   5c8c9:	c4 63 09 20 74 17 50 	vpinsrb $0x2,0x50(%rdi,%rdx,1),%xmm14,%xmm14
   5c8d0:	02 
   5c8d1:	c4 e3 09 20 64 17 5c 	vpinsrb $0x3,0x5c(%rdi,%rdx,1),%xmm14,%xmm4
   5c8d8:	03 
   5c8d9:	c5 f9 6f 4c 24 a0    	vmovdqa -0x60(%rsp),%xmm1
   5c8df:	c5 39 db d1          	vpand  %xmm1,%xmm8,%xmm10
   5c8e3:	c5 99 db f1          	vpand  %xmm1,%xmm12,%xmm6
   5c8e7:	c5 f9 6f 5c 24 e0    	vmovdqa -0x20(%rsp),%xmm3
   5c8ed:	c4 41 61 fa f0       	vpsubd %xmm8,%xmm3,%xmm14
   5c8f2:	c5 a9 76 f9          	vpcmpeqd %xmm1,%xmm10,%xmm7
   5c8f6:	c4 c3 39 4a fe 70    	vblendvps %xmm7,%xmm14,%xmm8,%xmm7
   5c8fc:	c4 41 61 fa c4       	vpsubd %xmm12,%xmm3,%xmm8
   5c901:	c5 49 76 f1          	vpcmpeqd %xmm1,%xmm6,%xmm14
   5c905:	c4 43 19 4a f0 e0    	vblendvps %xmm14,%xmm8,%xmm12,%xmm14
   5c90b:	c4 c1 39 72 d2 17    	vpsrld $0x17,%xmm10,%xmm8
   5c911:	c5 c9 72 d6 17       	vpsrld $0x17,%xmm6,%xmm6
   5c916:	c4 62 79 58 0d 1d 03 	vpbroadcastd -0x4fce3(%rip),%xmm9        # cc3c <__abi_tag+0xc940>
   5c91d:	fb ff 
   5c91f:	c4 42 39 00 c1       	vpshufb %xmm9,%xmm8,%xmm8
   5c924:	c4 41 01 ef c0       	vpxor  %xmm8,%xmm15,%xmm8
   5c929:	c4 c2 49 00 f1       	vpshufb %xmm9,%xmm6,%xmm6
   5c92e:	c5 d9 ef e6          	vpxor  %xmm6,%xmm4,%xmm4
   5c932:	c5 e9 db f1          	vpand  %xmm1,%xmm2,%xmm6
   5c936:	c5 11 db d1          	vpand  %xmm1,%xmm13,%xmm10
   5c93a:	c5 61 fa e2          	vpsubd %xmm2,%xmm3,%xmm12
   5c93e:	c5 49 76 f9          	vpcmpeqd %xmm1,%xmm6,%xmm15
   5c942:	c4 c3 69 4a d4 f0    	vblendvps %xmm15,%xmm12,%xmm2,%xmm2
   5c948:	c4 41 61 fa e5       	vpsubd %xmm13,%xmm3,%xmm12
   5c94d:	c5 29 76 f9          	vpcmpeqd %xmm1,%xmm10,%xmm15
   5c951:	c4 43 11 4a fc f0    	vblendvps %xmm15,%xmm12,%xmm13,%xmm15
   5c957:	c5 c9 72 d6 17       	vpsrld $0x17,%xmm6,%xmm6
   5c95c:	c4 c1 29 72 d2 17    	vpsrld $0x17,%xmm10,%xmm10
   5c962:	c4 c2 49 00 f1       	vpshufb %xmm9,%xmm6,%xmm6
   5c967:	c5 39 74 ee          	vpcmpeqb %xmm6,%xmm8,%xmm13
   5c96b:	c4 c2 29 00 f1       	vpshufb %xmm9,%xmm10,%xmm6
   5c970:	c5 59 74 e6          	vpcmpeqb %xmm6,%xmm4,%xmm12
   5c974:	c5 f8 28 4c 24 80    	vmovaps -0x80(%rsp),%xmm1
   5c97a:	c5 c0 54 e1          	vandps %xmm1,%xmm7,%xmm4
   5c97e:	c5 88 54 f1          	vandps %xmm1,%xmm14,%xmm6
   5c982:	c5 68 54 c1          	vandps %xmm1,%xmm2,%xmm8
   5c986:	c5 00 54 d1          	vandps %xmm1,%xmm15,%xmm10
   5c98a:	c5 39 f5 cc          	vpmaddwd %xmm4,%xmm8,%xmm9
   5c98e:	c5 c1 72 d7 0c       	vpsrld $0xc,%xmm7,%xmm7
   5c993:	c5 c1 db f9          	vpand  %xmm1,%xmm7,%xmm7
   5c997:	c5 39 f5 c7          	vpmaddwd %xmm7,%xmm8,%xmm8
   5c99b:	c5 e9 72 d2 0c       	vpsrld $0xc,%xmm2,%xmm2
   5c9a0:	c5 e9 db d1          	vpand  %xmm1,%xmm2,%xmm2
   5c9a4:	c5 e9 f5 e4          	vpmaddwd %xmm4,%xmm2,%xmm4
   5c9a8:	c5 e9 f5 d7          	vpmaddwd %xmm7,%xmm2,%xmm2
   5c9ac:	c5 a9 f5 fe          	vpmaddwd %xmm6,%xmm10,%xmm7
   5c9b0:	c4 c1 09 72 d6 0c    	vpsrld $0xc,%xmm14,%xmm14
   5c9b6:	c5 09 db f1          	vpand  %xmm1,%xmm14,%xmm14
   5c9ba:	c4 41 09 f5 d2       	vpmaddwd %xmm10,%xmm14,%xmm10
   5c9bf:	c4 c1 01 72 d7 0c    	vpsrld $0xc,%xmm15,%xmm15
   5c9c5:	c5 01 db f9          	vpand  %xmm1,%xmm15,%xmm15
   5c9c9:	c5 81 f5 f6          	vpmaddwd %xmm6,%xmm15,%xmm6
   5c9cd:	c4 41 01 f5 f6       	vpmaddwd %xmm14,%xmm15,%xmm14
   5c9d2:	c4 c1 01 72 f0 0c    	vpslld $0xc,%xmm8,%xmm15
   5c9d8:	c5 79 6f 5c 24 d0    	vmovdqa -0x30(%rsp),%xmm11
   5c9de:	c4 41 01 db fb       	vpand  %xmm11,%xmm15,%xmm15
   5c9e3:	c4 41 01 fe c9       	vpaddd %xmm9,%xmm15,%xmm9
   5c9e8:	c4 c1 01 72 f2 0c    	vpslld $0xc,%xmm10,%xmm15
   5c9ee:	c4 41 01 db fb       	vpand  %xmm11,%xmm15,%xmm15
   5c9f3:	c5 81 fe ff          	vpaddd %xmm7,%xmm15,%xmm7
   5c9f7:	c5 81 72 f4 0c       	vpslld $0xc,%xmm4,%xmm15
   5c9fc:	c4 41 01 db fb       	vpand  %xmm11,%xmm15,%xmm15
   5ca01:	c4 41 31 fe cf       	vpaddd %xmm15,%xmm9,%xmm9
   5ca06:	c5 81 72 f6 0c       	vpslld $0xc,%xmm6,%xmm15
   5ca0b:	c4 41 01 db fb       	vpand  %xmm11,%xmm15,%xmm15
   5ca10:	c5 81 fe ff          	vpaddd %xmm7,%xmm15,%xmm7
   5ca14:	c4 c1 39 72 d0 0c    	vpsrld $0xc,%xmm8,%xmm8
   5ca1a:	c5 b9 fe d2          	vpaddd %xmm2,%xmm8,%xmm2
   5ca1e:	c4 c1 39 72 d2 0c    	vpsrld $0xc,%xmm10,%xmm8
   5ca24:	c4 41 09 fe c0       	vpaddd %xmm8,%xmm14,%xmm8
   5ca29:	c5 d9 72 d4 0c       	vpsrld $0xc,%xmm4,%xmm4
   5ca2e:	c5 e9 fe d4          	vpaddd %xmm4,%xmm2,%xmm2
   5ca32:	c5 d9 72 d6 0c       	vpsrld $0xc,%xmm6,%xmm4
   5ca37:	c5 b9 fe e4          	vpaddd %xmm4,%xmm8,%xmm4
   5ca3b:	c4 c1 49 72 d1 18    	vpsrld $0x18,%xmm9,%xmm6
   5ca41:	c5 e9 fe f6          	vpaddd %xmm6,%xmm2,%xmm6
   5ca45:	c5 e9 72 d7 18       	vpsrld $0x18,%xmm7,%xmm2
   5ca4a:	c5 59 fe f2          	vpaddd %xmm2,%xmm4,%xmm14
   5ca4e:	c4 e2 7d 35 d6       	vpmovzxdq %xmm6,%ymm2
   5ca53:	c4 c2 7d 35 e6       	vpmovzxdq %xmm14,%ymm4
   5ca58:	c5 bd 73 f2 19       	vpsllq $0x19,%ymm2,%ymm8
   5ca5d:	c5 dd 73 f4 19       	vpsllq $0x19,%ymm4,%ymm4
   5ca62:	c4 41 31 fe c9       	vpaddd %xmm9,%xmm9,%xmm9
   5ca67:	c5 c1 fe d7          	vpaddd %xmm7,%xmm7,%xmm2
   5ca6b:	c5 b1 db fb          	vpand  %xmm3,%xmm9,%xmm7
   5ca6f:	c5 69 db d3          	vpand  %xmm3,%xmm2,%xmm10
   5ca73:	c4 e2 7d 35 ff       	vpmovzxdq %xmm7,%ymm7
   5ca78:	c5 bd eb ff          	vpor   %ymm7,%ymm8,%ymm7
   5ca7c:	c4 42 7d 35 c2       	vpmovzxdq %xmm10,%ymm8
   5ca81:	c5 3d eb c4          	vpor   %ymm4,%ymm8,%ymm8
   5ca85:	c5 79 6f 5c 24 c0    	vmovdqa -0x40(%rsp),%xmm11
   5ca8b:	c4 41 31 db fb       	vpand  %xmm11,%xmm9,%xmm15
   5ca90:	c5 dd 73 d7 18       	vpsrlq $0x18,%ymm7,%ymm4
   5ca95:	c4 e2 7d 21 1d e2 6c 	vpmovsxbd -0x4931e(%rip),%ymm3        # 13780 <__PRETTY_FUNCTION__.14+0x158>
   5ca9c:	fb ff 
   5ca9e:	c4 e2 65 36 e4       	vpermd %ymm4,%ymm3,%ymm4
   5caa3:	c5 f9 6f 4c 24 b0    	vmovdqa -0x50(%rsp),%xmm1
   5caa9:	c5 d9 db e1          	vpand  %xmm1,%xmm4,%xmm4
   5caad:	c5 c9 72 d6 17       	vpsrld $0x17,%xmm6,%xmm6
   5cab2:	c4 c2 79 21 fd       	vpmovsxbd %xmm13,%xmm7
   5cab7:	c5 d1 ef ed          	vpxor  %xmm5,%xmm5,%xmm5
   5cabb:	c4 41 51 fa cf       	vpsubd %xmm15,%xmm5,%xmm9
   5cac0:	c4 c1 29 72 e1 1f    	vpsrad $0x1f,%xmm9,%xmm10
   5cac6:	c5 29 fa d4          	vpsubd %xmm4,%xmm10,%xmm10
   5caca:	c4 c1 11 72 d2 18    	vpsrld $0x18,%xmm10,%xmm13
   5cad0:	c5 11 fa ee          	vpsubd %xmm6,%xmm13,%xmm13
   5cad4:	c5 f9 6f 6c 24 90    	vmovdqa -0x70(%rsp),%xmm5
   5cada:	c5 11 db ed          	vpand  %xmm5,%xmm13,%xmm13
   5cade:	c4 e3 11 4a f6 70    	vblendvps %xmm7,%xmm6,%xmm13,%xmm6
   5cae4:	c5 a1 db d2          	vpand  %xmm2,%xmm11,%xmm2
   5cae8:	c4 c1 3d 73 d0 18    	vpsrlq $0x18,%ymm8,%ymm8
   5caee:	c4 42 65 36 c0       	vpermd %ymm8,%ymm3,%ymm8
   5caf3:	c5 39 db c1          	vpand  %xmm1,%xmm8,%xmm8
   5caf7:	c4 c1 11 72 d6 17    	vpsrld $0x17,%xmm14,%xmm13
   5cafd:	c4 42 79 21 e4       	vpmovsxbd %xmm12,%xmm12
   5cb02:	c4 41 31 db cb       	vpand  %xmm11,%xmm9,%xmm9
   5cb07:	c5 29 db d1          	vpand  %xmm1,%xmm10,%xmm10
   5cb0b:	c4 e3 29 4a e4 70    	vblendvps %xmm7,%xmm4,%xmm10,%xmm4
   5cb11:	c4 c3 31 4a ff 70    	vblendvps %xmm7,%xmm15,%xmm9,%xmm7
   5cb17:	c5 e1 ef db          	vpxor  %xmm3,%xmm3,%xmm3
   5cb1b:	c5 61 fa ca          	vpsubd %xmm2,%xmm3,%xmm9
   5cb1f:	c4 c1 29 72 e1 1f    	vpsrad $0x1f,%xmm9,%xmm10
   5cb25:	c4 41 29 fa d0       	vpsubd %xmm8,%xmm10,%xmm10
   5cb2a:	c4 c1 09 72 d2 18    	vpsrld $0x18,%xmm10,%xmm14
   5cb30:	c4 41 09 fa f5       	vpsubd %xmm13,%xmm14,%xmm14
   5cb35:	c5 09 db f5          	vpand  %xmm5,%xmm14,%xmm14
   5cb39:	c4 43 09 4a ed c0    	vblendvps %xmm12,%xmm13,%xmm14,%xmm13
   5cb3f:	c5 29 db d1          	vpand  %xmm1,%xmm10,%xmm10
   5cb43:	c5 fe 6f 4c 24 f0    	vmovdqu -0x10(%rsp),%ymm1
   5cb49:	c4 43 29 4a c0 c0    	vblendvps %xmm12,%xmm8,%xmm10,%xmm8
   5cb4f:	c4 41 31 db cb       	vpand  %xmm11,%xmm9,%xmm9
   5cb54:	c4 e3 31 4a d2 c0    	vblendvps %xmm12,%xmm2,%xmm9,%xmm2
   5cb5a:	c5 d9 fe e7          	vpaddd %xmm7,%xmm4,%xmm4
   5cb5e:	c5 d9 fe e6          	vpaddd %xmm6,%xmm4,%xmm4
   5cb62:	c5 b9 fe d2          	vpaddd %xmm2,%xmm8,%xmm2
   5cb66:	c5 91 fe d2          	vpaddd %xmm2,%xmm13,%xmm2
   5cb6a:	c4 e2 7d 35 e4       	vpmovzxdq %xmm4,%ymm4
   5cb6f:	c5 fd d4 c4          	vpaddq %ymm4,%ymm0,%ymm0
   5cb73:	c4 e2 7d 35 d2       	vpmovzxdq %xmm2,%ymm2
   5cb78:	c5 f5 d4 ca          	vpaddq %ymm2,%ymm1,%ymm1
   5cb7c:	48 83 c2 60          	add    $0x60,%rdx
   5cb80:	48 39 d0             	cmp    %rdx,%rax
   5cb83:	0f 85 97 fc ff ff    	jne    5c820 <benchmark_helper+0xc0>
   5cb89:	c5 f5 d4 c0          	vpaddq %ymm0,%ymm1,%ymm0
   5cb8d:	c4 e3 7d 39 c1 01    	vextracti128 $0x1,%ymm0,%xmm1
   5cb93:	c5 f9 d4 c1          	vpaddq %xmm1,%xmm0,%xmm0
   5cb97:	c5 f9 70 c8 ee       	vpshufd $0xee,%xmm0,%xmm1
   5cb9c:	c5 f9 d4 c1          	vpaddq %xmm1,%xmm0,%xmm0
   5cba0:	c4 e1 f9 7e c0       	vmovq  %xmm0,%rax
   5cba5:	48 39 f1             	cmp    %rsi,%rcx
   5cba8:	75 37                	jne    5cbe1 <benchmark_helper+0x481>
   5cbaa:	48 83 c4 10          	add    $0x10,%rsp
   5cbae:	5b                   	pop    %rbx
   5cbaf:	41 5e                	pop    %r14
   5cbb1:	5d                   	pop    %rbp
   5cbb2:	31 c9                	xor    %ecx,%ecx
   5cbb4:	31 ff                	xor    %edi,%edi
   5cbb6:	31 d2                	xor    %edx,%edx
   5cbb8:	31 f6                	xor    %esi,%esi
   5cbba:	45 31 c0             	xor    %r8d,%r8d
   5cbbd:	45 31 c9             	xor    %r9d,%r9d
   5cbc0:	45 31 d2             	xor    %r10d,%r10d
   5cbc3:	45 31 db             	xor    %r11d,%r11d
   5cbc6:	c5 f8 77             	vzeroupper
   5cbc9:	c3                   	ret
   5cbca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   5cbd0:	45 01 ca             	add    %r9d,%r10d
   5cbd3:	45 01 da             	add    %r11d,%r10d
   5cbd6:	4c 01 d0             	add    %r10,%rax
   5cbd9:	48 ff c1             	inc    %rcx
   5cbdc:	48 39 ce             	cmp    %rcx,%rsi
   5cbdf:	74 c9                	je     5cbaa <benchmark_helper+0x44a>
   5cbe1:	89 ca                	mov    %ecx,%edx
   5cbe3:	81 e2 ff 0f 00 00    	and    $0xfff,%edx
   5cbe9:	4c 8d 04 52          	lea    (%rdx,%rdx,2),%r8
   5cbed:	46 8b 14 87          	mov    (%rdi,%r8,4),%r10d
   5cbf1:	46 8b 5c 87 04       	mov    0x4(%rdi,%r8,4),%r11d
   5cbf6:	44 89 d2             	mov    %r10d,%edx
   5cbf9:	81 e2 00 00 80 00    	and    $0x800000,%edx
   5cbff:	41 b9 00 00 00 01    	mov    $0x1000000,%r9d
   5cc05:	45 29 d1             	sub    %r10d,%r9d
   5cc08:	85 d2                	test   %edx,%edx
   5cc0a:	45 0f 44 ca          	cmove  %r10d,%r9d
   5cc0e:	c1 ea 17             	shr    $0x17,%edx
   5cc11:	42 32 54 87 08       	xor    0x8(%rdi,%r8,4),%dl
   5cc16:	45 89 d8             	mov    %r11d,%r8d
   5cc19:	41 81 e0 00 00 80 00 	and    $0x800000,%r8d
   5cc20:	41 ba 00 00 00 01    	mov    $0x1000000,%r10d
   5cc26:	45 29 da             	sub    %r11d,%r10d
   5cc29:	45 85 c0             	test   %r8d,%r8d
   5cc2c:	45 0f 44 d3          	cmove  %r11d,%r10d
   5cc30:	41 c1 e8 17          	shr    $0x17,%r8d
   5cc34:	45 89 cb             	mov    %r9d,%r11d
   5cc37:	41 81 e3 ff 0f 00 00 	and    $0xfff,%r11d
   5cc3e:	44 89 d5             	mov    %r10d,%ebp
   5cc41:	81 e5 ff 0f 00 00    	and    $0xfff,%ebp
   5cc47:	41 c1 e9 0c          	shr    $0xc,%r9d
   5cc4b:	41 81 e1 ff 0f 00 00 	and    $0xfff,%r9d
   5cc52:	44 89 cb             	mov    %r9d,%ebx
   5cc55:	0f af dd             	imul   %ebp,%ebx
   5cc58:	41 0f af eb          	imul   %r11d,%ebp
   5cc5c:	41 c1 ea 0c          	shr    $0xc,%r10d
   5cc60:	41 81 e2 ff 0f 00 00 	and    $0xfff,%r10d
   5cc67:	45 0f af da          	imul   %r10d,%r11d
   5cc6b:	45 0f af d1          	imul   %r9d,%r10d
   5cc6f:	41 89 de             	mov    %ebx,%r14d
   5cc72:	41 c1 e6 0c          	shl    $0xc,%r14d
   5cc76:	41 81 e6 00 f0 ff 00 	and    $0xfff000,%r14d
   5cc7d:	41 01 ee             	add    %ebp,%r14d
   5cc80:	45 89 d9             	mov    %r11d,%r9d
   5cc83:	41 c1 e1 0c          	shl    $0xc,%r9d
   5cc87:	41 81 e1 00 f0 ff 00 	and    $0xfff000,%r9d
   5cc8e:	45 01 f1             	add    %r14d,%r9d
   5cc91:	c1 eb 0c             	shr    $0xc,%ebx
   5cc94:	44 01 d3             	add    %r10d,%ebx
   5cc97:	41 c1 eb 0c          	shr    $0xc,%r11d
   5cc9b:	41 01 db             	add    %ebx,%r11d
   5cc9e:	45 89 ca             	mov    %r9d,%r10d
   5cca1:	41 c1 ea 18          	shr    $0x18,%r10d
   5cca5:	45 01 da             	add    %r11d,%r10d
   5cca8:	4c 89 d3             	mov    %r10,%rbx
   5ccab:	48 c1 e3 19          	shl    $0x19,%rbx
   5ccaf:	45 01 c9             	add    %r9d,%r9d
   5ccb2:	45 89 cb             	mov    %r9d,%r11d
   5ccb5:	41 81 e3 00 00 00 01 	and    $0x1000000,%r11d
   5ccbc:	49 09 db             	or     %rbx,%r11
   5ccbf:	41 81 e1 fe ff ff 00 	and    $0xfffffe,%r9d
   5ccc6:	49 c1 eb 18          	shr    $0x18,%r11
   5ccca:	41 81 e3 ff ff ff 00 	and    $0xffffff,%r11d
   5ccd1:	41 c1 ea 17          	shr    $0x17,%r10d
   5ccd5:	44 38 c2             	cmp    %r8b,%dl
   5ccd8:	0f 84 f2 fe ff ff    	je     5cbd0 <benchmark_helper+0x470>
   5ccde:	41 f7 d9             	neg    %r9d
   5cce1:	44 89 ca             	mov    %r9d,%edx
   5cce4:	c1 fa 1f             	sar    $0x1f,%edx
   5cce7:	44 29 da             	sub    %r11d,%edx
   5ccea:	41 89 d0             	mov    %edx,%r8d
   5cced:	41 c1 e8 18          	shr    $0x18,%r8d
   5ccf1:	45 29 d0             	sub    %r10d,%r8d
   5ccf4:	41 81 e1 fe ff ff 00 	and    $0xfffffe,%r9d
   5ccfb:	81 e2 ff ff ff 00    	and    $0xffffff,%edx
   5cd01:	45 0f b6 d0          	movzbl %r8b,%r10d
   5cd05:	41 89 d3             	mov    %edx,%r11d
   5cd08:	e9 c3 fe ff ff       	jmp    5cbd0 <benchmark_helper+0x470>

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
