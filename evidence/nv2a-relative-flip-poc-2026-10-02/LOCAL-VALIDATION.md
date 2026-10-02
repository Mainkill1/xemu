# Final review validation

Source `0b2be47fe9af1baa3f174d51a6ee0acb4a14b4da`, based on fork main
`ee5ce48b48784f999af374c1452003f8b2b1230f`.

- Production stop callback: 2 checks pass, covering normal stop, nested disarm,
  resume, re-arm, interrupted running state, running-save cancellation, paused
  rejection, repeated save after disarm, resumed save and disabled pass-through.
- The retry assertion failed before the correction (first rejection cancelled the
  gate; a second save incorrectly succeeded). The stale-marker callback regression
  also failed before revalidation. Both green after fixes.
- Actual production gate: standalone core checks exit 0.
- Actual PFIFO and PGRAPH integration: 3 + 16 checks PASS.
- Actual Windows presentation policy: 17 checks PASS, including drop/quarantine
  exclusion and successful SDL/DXGI qualification.
- Werror compile of actual branch flip_probe.c, migration/savevm.c and ui/xemu.c
  passes using the existing sibling build configuration. The first ad-hoc core
  compile command lacked QEMU include flags; corrected to the maintained fixture
  flags and passed. It was a command setup failure, not a source failure.
- New-source checkpatch: zero warnings/errors. git diff --check clean.
- Review confirmed no remaining critical/important correction findings.

Native linked binaries and lifecycle results belong to the exact-head CI/rig
reports, not the local VM-stop mocks. The enabled diagnostic intentionally rejects
saving a non-running VM until resume. Disabled snapshot behavior is unchanged.

Final branch `d6fabace38137d35312a82ec13b0fada97d63284` changes only the core
test registration after native-tested runtime `0b2be47`. Exact-head CI
36988514260 passes all 20 jobs: 138 unit targets passed, 0 failed, 16 inherited
skips. The core gate is now OK (one subtest), callback/save OK (two), and the
required Mesa shader-depth draw check passes. Runtime-build CI36987622322 also
passed all20 jobs. Both native debug binaries identify correctly as
0.8.136-0-g0b2be47fe9af.
