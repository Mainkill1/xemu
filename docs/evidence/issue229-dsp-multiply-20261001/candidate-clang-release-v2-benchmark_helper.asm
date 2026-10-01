
/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/candidate-clang-release-v2:     file format elf64-x86-64


Disassembly of section .text:

0000000000058360 <benchmark_helper>:
   58360:	48 85 f6             	test   %rsi,%rsi
   58363:	74 18                	je     5837d <benchmark_helper+0x1d>
   58365:	48 8d 86 ff ef ff ff 	lea    -0x1001(%rsi),%rax
   5836c:	48 3d 06 f0 ff ff    	cmp    $0xfffffffffffff006,%rax
   58372:	77 10                	ja     58384 <benchmark_helper+0x24>
   58374:	31 c0                	xor    %eax,%eax
   58376:	31 c9                	xor    %ecx,%ecx
   58378:	e9 33 02 00 00       	jmp    585b0 <benchmark_helper+0x250>
   5837d:	31 c0                	xor    %eax,%eax
   5837f:	e9 b1 02 00 00       	jmp    58635 <benchmark_helper+0x2d5>
   58384:	89 f1                	mov    %esi,%ecx
   58386:	81 e1 f8 1f 00 00    	and    $0x1ff8,%ecx
   5838c:	8d 04 b5 00 00 00 00 	lea    0x0(,%rsi,4),%eax
   58393:	25 e0 7f 00 00       	and    $0x7fe0,%eax
   58398:	48 8d 04 40          	lea    (%rax,%rax,2),%rax
   5839c:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
   583a0:	31 d2                	xor    %edx,%edx
   583a2:	c4 e2 79 58 0d 59 48 	vpbroadcastd -0x4b7a7(%rip),%xmm1        # cc04 <__abi_tag+0xc908>
   583a9:	fb ff 
   583ab:	c4 e2 79 58 15 8c 48 	vpbroadcastd -0x4b774(%rip),%xmm2        # cc40 <_IO_stdin_used+0x4>
   583b2:	fb ff 
   583b4:	c5 e1 ef db          	vpxor  %xmm3,%xmm3,%xmm3
   583b8:	c4 e2 7d 59 25 67 b3 	vpbroadcastq -0x44c99(%rip),%ymm4        # 13728 <__PRETTY_FUNCTION__.14+0xf0>
   583bf:	fb ff 
   583c1:	c4 e2 7d 59 2d c6 b3 	vpbroadcastq -0x44c3a(%rip),%ymm5        # 13790 <__PRETTY_FUNCTION__.14+0x158>
   583c8:	fb ff 
   583ca:	c4 e2 7d 59 35 4d b3 	vpbroadcastq -0x44cb3(%rip),%ymm6        # 13720 <__PRETTY_FUNCTION__.14+0xe8>
   583d1:	fb ff 
   583d3:	c5 c1 ef ff          	vpxor  %xmm7,%xmm7,%xmm7
   583d7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
   583de:	00 00 
   583e0:	c5 79 6e 04 17       	vmovd  (%rdi,%rdx,1),%xmm8
   583e5:	c4 63 39 22 44 17 0c 	vpinsrd $0x1,0xc(%rdi,%rdx,1),%xmm8,%xmm8
   583ec:	01 
   583ed:	c4 63 39 22 44 17 18 	vpinsrd $0x2,0x18(%rdi,%rdx,1),%xmm8,%xmm8
   583f4:	02 
   583f5:	c4 63 39 22 44 17 24 	vpinsrd $0x3,0x24(%rdi,%rdx,1),%xmm8,%xmm8
   583fc:	03 
   583fd:	c5 79 6e 4c 17 30    	vmovd  0x30(%rdi,%rdx,1),%xmm9
   58403:	c4 63 31 22 4c 17 3c 	vpinsrd $0x1,0x3c(%rdi,%rdx,1),%xmm9,%xmm9
   5840a:	01 
   5840b:	c4 63 31 22 4c 17 48 	vpinsrd $0x2,0x48(%rdi,%rdx,1),%xmm9,%xmm9
   58412:	02 
   58413:	c4 63 31 22 4c 17 54 	vpinsrd $0x3,0x54(%rdi,%rdx,1),%xmm9,%xmm9
   5841a:	03 
   5841b:	c5 79 6e 54 17 04    	vmovd  0x4(%rdi,%rdx,1),%xmm10
   58421:	c4 63 29 22 54 17 10 	vpinsrd $0x1,0x10(%rdi,%rdx,1),%xmm10,%xmm10
   58428:	01 
   58429:	c4 63 29 22 54 17 1c 	vpinsrd $0x2,0x1c(%rdi,%rdx,1),%xmm10,%xmm10
   58430:	02 
   58431:	c4 63 29 22 54 17 28 	vpinsrd $0x3,0x28(%rdi,%rdx,1),%xmm10,%xmm10
   58438:	03 
   58439:	c5 79 6e 5c 17 34    	vmovd  0x34(%rdi,%rdx,1),%xmm11
   5843f:	c4 63 21 22 5c 17 40 	vpinsrd $0x1,0x40(%rdi,%rdx,1),%xmm11,%xmm11
   58446:	01 
   58447:	c4 63 21 22 5c 17 4c 	vpinsrd $0x2,0x4c(%rdi,%rdx,1),%xmm11,%xmm11
   5844e:	02 
   5844f:	c4 63 21 22 5c 17 58 	vpinsrd $0x3,0x58(%rdi,%rdx,1),%xmm11,%xmm11
   58456:	03 
   58457:	c5 79 6e 64 17 08    	vmovd  0x8(%rdi,%rdx,1),%xmm12
   5845d:	c4 63 19 20 64 17 14 	vpinsrb $0x1,0x14(%rdi,%rdx,1),%xmm12,%xmm12
   58464:	01 
   58465:	c4 63 19 20 64 17 20 	vpinsrb $0x2,0x20(%rdi,%rdx,1),%xmm12,%xmm12
   5846c:	02 
   5846d:	c4 63 19 20 64 17 2c 	vpinsrb $0x3,0x2c(%rdi,%rdx,1),%xmm12,%xmm12
   58474:	03 
   58475:	c5 79 6e 6c 17 38    	vmovd  0x38(%rdi,%rdx,1),%xmm13
   5847b:	c4 63 11 20 6c 17 44 	vpinsrb $0x1,0x44(%rdi,%rdx,1),%xmm13,%xmm13
   58482:	01 
   58483:	c4 63 11 20 6c 17 50 	vpinsrb $0x2,0x50(%rdi,%rdx,1),%xmm13,%xmm13
   5848a:	02 
   5848b:	c4 63 11 20 6c 17 5c 	vpinsrb $0x3,0x5c(%rdi,%rdx,1),%xmm13,%xmm13
   58492:	03 
   58493:	c5 39 db f1          	vpand  %xmm1,%xmm8,%xmm14
   58497:	c5 31 db f9          	vpand  %xmm1,%xmm9,%xmm15
   5849b:	c4 42 7d 35 f6       	vpmovzxdq %xmm14,%ymm14
   584a0:	c4 42 7d 35 ff       	vpmovzxdq %xmm15,%ymm15
   584a5:	c5 39 db c2          	vpand  %xmm2,%xmm8,%xmm8
   584a9:	c5 31 db ca          	vpand  %xmm2,%xmm9,%xmm9
   584ad:	c4 42 7d 35 c0       	vpmovzxdq %xmm8,%ymm8
   584b2:	c4 41 0d fb c0       	vpsubq %ymm8,%ymm14,%ymm8
   584b7:	c4 42 7d 35 c9       	vpmovzxdq %xmm9,%ymm9
   584bc:	c4 41 05 fb c9       	vpsubq %ymm9,%ymm15,%ymm9
   584c1:	c5 29 db f1          	vpand  %xmm1,%xmm10,%xmm14
   584c5:	c5 21 db f9          	vpand  %xmm1,%xmm11,%xmm15
   584c9:	c4 42 7d 35 f6       	vpmovzxdq %xmm14,%ymm14
   584ce:	c4 42 7d 35 ff       	vpmovzxdq %xmm15,%ymm15
   584d3:	c5 29 db d2          	vpand  %xmm2,%xmm10,%xmm10
   584d7:	c5 21 db da          	vpand  %xmm2,%xmm11,%xmm11
   584db:	c4 42 7d 35 d2       	vpmovzxdq %xmm10,%ymm10
   584e0:	c4 41 0d fb d2       	vpsubq %ymm10,%ymm14,%ymm10
   584e5:	c4 42 2d 28 c0       	vpmuldq %ymm8,%ymm10,%ymm8
   584ea:	c4 42 7d 35 d3       	vpmovzxdq %xmm11,%ymm10
   584ef:	c4 41 05 fb d2       	vpsubq %ymm10,%ymm15,%ymm10
   584f4:	c4 42 2d 28 c9       	vpmuldq %ymm9,%ymm10,%ymm9
   584f9:	c5 19 74 d3          	vpcmpeqb %xmm3,%xmm12,%xmm10
   584fd:	c4 42 7d 22 d2       	vpmovsxbq %xmm10,%ymm10
   58502:	c5 11 74 db          	vpcmpeqb %xmm3,%xmm13,%xmm11
   58506:	c4 42 7d 22 db       	vpmovsxbq %xmm11,%ymm11
   5850b:	c4 41 3d ef c2       	vpxor  %ymm10,%ymm8,%ymm8
   58510:	c4 41 2d fb c0       	vpsubq %ymm8,%ymm10,%ymm8
   58515:	c4 41 35 ef cb       	vpxor  %ymm11,%ymm9,%ymm9
   5851a:	c4 41 25 fb c9       	vpsubq %ymm9,%ymm11,%ymm9
   5851f:	c4 41 3d d4 d0       	vpaddq %ymm8,%ymm8,%ymm10
   58524:	c4 41 35 d4 d9       	vpaddq %ymm9,%ymm9,%ymm11
   58529:	c4 c1 1d 73 d0 2f    	vpsrlq $0x2f,%ymm8,%ymm12
   5852f:	c4 c1 15 73 d1 2f    	vpsrlq $0x2f,%ymm9,%ymm13
   58535:	c5 1d db e4          	vpand  %ymm4,%ymm12,%ymm12
   58539:	c5 15 db ec          	vpand  %ymm4,%ymm13,%ymm13
   5853d:	c4 c1 3d 73 d0 17    	vpsrlq $0x17,%ymm8,%ymm8
   58543:	c4 c1 35 73 d1 17    	vpsrlq $0x17,%ymm9,%ymm9
   58549:	c5 3d db c5          	vpand  %ymm5,%ymm8,%ymm8
   5854d:	c4 41 3d d4 c4       	vpaddq %ymm12,%ymm8,%ymm8
   58552:	c5 35 db cd          	vpand  %ymm5,%ymm9,%ymm9
   58556:	c4 41 35 d4 cd       	vpaddq %ymm13,%ymm9,%ymm9
   5855b:	c5 2d db d6          	vpand  %ymm6,%ymm10,%ymm10
   5855f:	c5 ad d4 c0          	vpaddq %ymm0,%ymm10,%ymm0
   58563:	c5 bd d4 c0          	vpaddq %ymm0,%ymm8,%ymm0
   58567:	c5 25 db c6          	vpand  %ymm6,%ymm11,%ymm8
   5856b:	c5 bd d4 ff          	vpaddq %ymm7,%ymm8,%ymm7
   5856f:	c5 b5 d4 ff          	vpaddq %ymm7,%ymm9,%ymm7
   58573:	48 83 c2 60          	add    $0x60,%rdx
   58577:	48 39 d0             	cmp    %rdx,%rax
   5857a:	0f 85 60 fe ff ff    	jne    583e0 <benchmark_helper+0x80>
   58580:	c5 c5 d4 c0          	vpaddq %ymm0,%ymm7,%ymm0
   58584:	c4 e3 7d 39 c1 01    	vextracti128 $0x1,%ymm0,%xmm1
   5858a:	c5 f9 d4 c1          	vpaddq %xmm1,%xmm0,%xmm0
   5858e:	c5 f9 70 c8 ee       	vpshufd $0xee,%xmm0,%xmm1
   58593:	c5 f9 d4 c1          	vpaddq %xmm1,%xmm0,%xmm0
   58597:	c4 e1 f9 7e c0       	vmovq  %xmm0,%rax
   5859c:	48 39 f1             	cmp    %rsi,%rcx
   5859f:	0f 84 90 00 00 00    	je     58635 <benchmark_helper+0x2d5>
   585a5:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
   585ac:	00 00 00 00 
   585b0:	89 ca                	mov    %ecx,%edx
   585b2:	81 e2 ff 0f 00 00    	and    $0xfff,%edx
   585b8:	48 8d 14 52          	lea    (%rdx,%rdx,2),%rdx
   585bc:	44 8b 04 97          	mov    (%rdi,%rdx,4),%r8d
   585c0:	44 8b 4c 97 04       	mov    0x4(%rdi,%rdx,4),%r9d
   585c5:	45 89 c2             	mov    %r8d,%r10d
   585c8:	41 81 e2 ff ff 7f 00 	and    $0x7fffff,%r10d
   585cf:	41 81 e0 00 00 80 00 	and    $0x800000,%r8d
   585d6:	4d 29 c2             	sub    %r8,%r10
   585d9:	45 89 c8             	mov    %r9d,%r8d
   585dc:	41 81 e0 ff ff 7f 00 	and    $0x7fffff,%r8d
   585e3:	41 81 e1 00 00 80 00 	and    $0x800000,%r9d
   585ea:	4d 29 c8             	sub    %r9,%r8
   585ed:	4d 0f af c2          	imul   %r10,%r8
   585f1:	4d 89 c1             	mov    %r8,%r9
   585f4:	49 f7 d9             	neg    %r9
   585f7:	80 7c 97 08 00       	cmpb   $0x0,0x8(%rdi,%rdx,4)
   585fc:	4d 0f 44 c8          	cmove  %r8,%r9
   58600:	4c 89 ca             	mov    %r9,%rdx
   58603:	48 c1 ea 2f          	shr    $0x2f,%rdx
   58607:	0f b6 d2             	movzbl %dl,%edx
   5860a:	4d 89 c8             	mov    %r9,%r8
   5860d:	49 c1 e8 17          	shr    $0x17,%r8
   58611:	41 81 e0 ff ff ff 00 	and    $0xffffff,%r8d
   58618:	41 81 e1 ff ff 7f 00 	and    $0x7fffff,%r9d
   5861f:	4a 8d 04 48          	lea    (%rax,%r9,2),%rax
   58623:	4c 01 c0             	add    %r8,%rax
   58626:	48 01 d0             	add    %rdx,%rax
   58629:	48 ff c1             	inc    %rcx
   5862c:	48 39 ce             	cmp    %rcx,%rsi
   5862f:	0f 85 7b ff ff ff    	jne    585b0 <benchmark_helper+0x250>
   58635:	31 c9                	xor    %ecx,%ecx
   58637:	31 ff                	xor    %edi,%edi
   58639:	31 d2                	xor    %edx,%edx
   5863b:	31 f6                	xor    %esi,%esi
   5863d:	45 31 c0             	xor    %r8d,%r8d
   58640:	45 31 c9             	xor    %r9d,%r9d
   58643:	45 31 d2             	xor    %r10d,%r10d
   58646:	c5 f8 77             	vzeroupper
   58649:	c3                   	ret

Disassembly of section .init:

Disassembly of section .fini:

Disassembly of section .plt:
