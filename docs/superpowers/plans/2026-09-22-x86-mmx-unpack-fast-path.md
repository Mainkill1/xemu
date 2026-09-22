# x86 MMX unpack fast-path implementation plan

1. Add focused known-value coverage for all six legacy MMX unpack forms,
   including destination aliasing and memory sources.
2. Run the focused test against stable main to record the characterization
   baseline.
3. Add a direct 64-bit TCG translation used only by legacy MMX unpack forms;
   retain existing SSE/AVX helper paths.
4. Run focused and existing x86/MMX tests, formatting checks, and an optimized
   Linux build.
5. Re-run the PGR2 Steam Deck Vulkan full-launch workload and compare FPS,
   p50/p95/p99 interval, CPU, and GPU metrics against `83eeec715a`.
6. If the result is outside variance, repeat the JIT profile to confirm the
   `0x211ba7` TB shrank and the helper cost moved; otherwise reject the patch.
7. For a promising candidate, qualify OpenGL and Windows, then follow the
   remaining validation and reporting gates in
   `evidence/wiki-xiso-per-test/PERFORMANCE_PR_TEMPLATE.md`.
