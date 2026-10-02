# Matched runtime bundles for issue #197

**Recommendation: HOLD.** The reference rebuild resolves the previously identified GLib mismatch. This qualifies build inputs for game testing; it establishes no game gain. The first of two planned original-only controls is running on Steam Deck `10.0.0.123` before any candidate game run.

| Build input | Original reference | Candidate |
|---|---|---|
| Source commit | `ee5ce48b48784f999af374c1452003f8b2b1230f` | `c4cdef6cad3dd22d516e16ec2cb1aa9c06ec11f6` |
| CI run | [37004023200](https://github.com/Mainkill1/xemu/actions/runs/37004023200), successful attempt 3 | [36999719003](https://github.com/Mainkill1/xemu/actions/runs/36999719003) |
| Executable size | 25,548,144 bytes | 25,552,240 bytes |
| Executable SHA-256 | `5b6ccf357cfab428e92692b82dc3a5cc75cb50e4065608defbf1cc66b68a597c` | `95f81f33d932cb297d6f637eb0635ac2faa2b895d330bae6871e3012d6353667` |
| ELF build ID | `3aa4cbcd128f8fcc48f4892ee51d4f3b56729345` | `ed2f947f2c60572bf06c29cc4b58d6ca25f07486` |
| Compiler / linker | Clang / LLD 21.1.8 | Identical recorded versions |
| Release options | Thin LTO; `x86_version=3`; identical recorded build options | Identical |
| Corresponding AppImage libraries | 38 pinned hashes | **38/38 identical** |

The fixed reference source commit remains unchanged. This is a newly built executable from that commit, rather than reuse of the older bundle from the earlier controls. Both source packages were checked against their commits for the production caller; the candidate reader/fixture were also checked. The original package has no candidate reader header. Complete compiler settings, source-archive hashes, AppImage hashes, all library/executable hashes, sizes and ELF notes are in the archived identity ledger.

## Failed attempts remain visible

Reference attempt 1 failed an ARM LLVM package download with `curl` exit 7; matrix fail-fast canceled x86 builds. Attempt 2 requested only the canceled x86 release job, which was immediately canceled again because GitHub retained the failed ARM matrix state. Its log request returned `BlobNotFound`; both the original response and job metadata are retained. The explicit failed-jobs rerun, attempt 3, passes all four Linux jobs; the final CI record contains **20 successful jobs**. Earlier outcomes were not edited or replaced.

No old GLib binary was substituted. Matching libraries do not control the host OS, graphics driver, OS page cache, CPU power/affinity or executable code layout. Game cache qualification and native outcomes still have to pass for each measurement.

## Maintained native test and retained tools

The immutable saved test is `issue197-deck-pgr2-parked-warm60-300-v3`, revision `3e72921bec480a32d141fe3b8c3894b6a64a864de037cee803d8fde52d7ef919`: fixed input route, 60 seconds excluded warmup and at least 300 seconds parked measurement, Vulkan/full DSP/JIT/128 MiB guest, private guest state and private cold application/Mesa disk caches. All **39** executable/library slots are replaced together through the maintained HTTP client. Workload/config/seed files remain pinned by the saved test.

Two reusable helpers are retained inside [EVIDENCE.tar.gz](EVIDENCE.tar.gz): `qualify-matched-game-builds.py` verifies corresponding CI bundles and builds complete packages; `run-matched-game-cohort.py` runs original-only controls or physical **ABBA then BAAB**, collects every eligible artifact, and preserves any failure without retries. Its preliminary control gate is two eligible original runs with qualified Mesa and mean-interval range/median at most 5%; variation and scenes must still be reviewed before interpreting balanced results. CLI/import checks completed; qualification ran against the actual artifacts. The first original control is confirmed live through HTTP; native qualification of the game cohort is unfinished.

[SUMMARY.json](SUMMARY.json) records the current scope. [INDEX.json](INDEX.json) hashes every archived payload; the archive was reopened and all payloads verified. Large build artifacts remain in linked CI and local retained storage with hashes, rather than duplication in the repository. The prior [original controls and GLib mismatch](../issue197-game-controls-20261002/README.md) remain applicable historical evidence; they are not the A/A controls for this newly built bundle. No audio parity, affected XISO coverage, or game performance gain is claimed here.
