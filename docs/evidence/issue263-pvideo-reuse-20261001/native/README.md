# Steam Deck PVIDEO native evidence

**Recommendation: HOLD.** The resource helper does less work and both Vulkan
builds render the sampled overlay sequence correctly. The first native balanced
comparison establishes no useful guest-throughput gain. This packet separates
that result from the helper microbenchmark and preserves failed diagnostics.

## What A and B mean

| Identity | A: parent/reference | B: candidate |
| --- | --- | --- |
| Product source | `ee5ce48b48784f999af374c1452003f8b2b1230f` | `b0f0661860fc5c70a0be2bd2c20bb1bfc8c7e069` |
| Built candidate head | N/A | `aeeb9554a87f5e4cb706ff6c58b3825b16f320f1` (evidence commits after product change) |
| Native executable SHA-256 | `d406d6072238c81ebed3b144257fcfb23a5fd12b5215769f853d9365d33d5471` | `5dd8d5453574fdad00e2acd9d0dc4aacb1e32ca142334ce87a05226f99e46e5b` |
| Build | GCC 14.2, O2/debug, no LTO, plugins off, i386-softmmu/DXBOX | Same local toolchain/settings |

Both use the same packaged slirp/pcap libraries; all dependency hashes are in
canonical input inventories. Runner revision is `0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e`.
The runner is reached from the build machine at
`http://10.0.0.123:9368`; every native launch uses its maintained HTTP clients.
Vulkan, 64 MiB guest, host vsync off, fullscreen startup, DSP disabled. These
native tests do not pin cores or lock a CPU power policy. A/A controls and
reverse order expose some variation; they do not remove every confounder.

Fixture source `c1e81dee18c0a3e3c9778d4746df53ba47152fd2`, descriptor revision 2;
[test-tool draft #50](https://github.com/Mainkill1/xemu-perf-tests/pull/50) head
`82a109e394f155513ed5f308514e29d71b4d66bb` adds the host oracle/docs/contracts
without changing the measured guest source. NXDK
`73c95900965a16be3a3e34b8d4d5d41bc18498be`, Release XISO;
ISO SHA `a1006c77017ab8cb91ec6165ca01fbcebb6636e8eaee27f1deac75c14c70a11d`,
catalog `sha256:8d412b1c1cfb6f2b2d8681f4a9a931625a5e68df059e59f2c4b61aa2e65163d1`.
The binary frozen for measurement is retained separately from later rebuilds.

## Work and correctness contract

- `pvideo.steady_upload`: same 128×128 storage, generated normal/inverted YUY2
  pixels and alternating source windows; overlay remains enabled.
- `pvideo.resize_toggle`: the same source changes plus 128/64 resize and
  disable/re-enable in an eight-phase sequence.
- Each leaf: eight batches of 128 guest frame submissions, no warmups,
  multiplier 1, per-iteration completion. Guest vblank paces submissions;
  neither a submission nor a timer sample authenticates a host present.
- Source known answers use independently specified bytes and FNV-1a64.
  Final inverse-128 source hash: `f4e5cbb5c68ea325`. Background framebuffer
  hash: `b2dc72d53890c325`. The framebuffer hash excludes host PVIDEO compositing.
- Source-limit compatibility: a 68 KiB guest allocation contains two 32 KiB
  windows plus allocated guard space. Every sampled phase respects the
  production inclusive-limit check. No emulator limit interpretation changed.
- Revision 2 selects the pbkit front render screen immediately before
  `Profile()`, outside measurement. Revision 1 accidentally timed the progress
  debug scanout; its apparent PASS is not accepted as PVIDEO path coverage.

## Cache qualification

Every completed Deck attempt requests a fresh private Mesa namespace, cold
application-cache start, `cacheShaders=false`, private EEPROM and FATX HDD.
The canonical `diagnostics/run-state/report.json` records
`DriverNamespaceVerified=true`,
`DriverQualification=mesa_private_disk_writes_observed`,
`ComparisonReady=true`, `AllowUncontrolledDriverCache=false` for the benchmark
attempts. Driver before/after path/length/SHA inventories are retained. No waiver,
global purge or reuse of another build's cache was used. Boot warms driver state
inside each fresh process; a later measurement window is not a pristine driver
first-use measurement.

## First balanced guest-time comparison

Frozen suite: `issue263-deck-pvideo-benchmark-v1-suite`.
AAAA campaign: `i263-deck-vk-aaaa-v1` (all four parent binaries).
ABBA: `i263-deck-vk-abba-v1` = parent/candidate/candidate/parent.
BAAB: `i263-deck-vk-baab-v1` = candidate/parent/parent/candidate. The BAAB
runner role names reverse; analysis binds variants to executable SHA, not label.
All 12 attempts complete/pass with qualified state; no screenshot operations or
verbose Vulkan trace occur in these benchmark runs. Existing frame/flip logging
is identical for both builds. Exact run IDs and eight raw guest samples per leaf
are in the packet.

Cells are medians of two per-run guest means per build/order, microseconds per
128-submission batch. Positive improvement is `100*(A-B)/A`.

| Test / order | A µs/batch | B µs/batch | Saved µs/batch | Improvement | Source correctness |
| --- | ---: | ---: | ---: | ---: | --- |
| steady_upload / ABBA | 2,133,067.688 | 2,132,867.188 | 200.500 | +0.0094% | PASS |
| steady_upload / BAAB | 2,132,599.750 | 2,133,001.625 | −401.875 | −0.0188% | PASS |
| resize_toggle / ABBA | 2,134,551.188 | 2,133,700.125 | 851.063 | +0.0399% | PASS |
| resize_toggle / BAAB | 2,132,485.313 | 2,132,518.563 | −33.250 | −0.0016% | PASS |

AAAA per-run means span 2,131,634.625–2,133,453.250 µs for steady uploads and
2,132,628.625–2,133,581.250 µs for resize/toggle. The small changes reverse sign
and do not establish useful throughput gains. Vblank pacing can hide host cost
changes. These batches must not be presented as game FPS improvements.

The maintained hash-comparison API returns a separate aggregate (8 reference
runs including A/A controls vs 4 candidate runs), and retains unmatched/failed
diagnostic cohorts. Its full CSV has 31 rows; the compact Markdown truncates
23. All rows and paged indexed records are retained. The per-order table above
is transparently reproduced from canonical normalized guest values; it does
not replace the server comparison or invent a statistical significance test.
The one-shot screenshot A/B timings are diagnostic, not the performance result.

## Second balanced host-cost comparison

All 12 attempts complete with passing source correctness, complete evidence and
qualified cold private Mesa state. Frozen suite `issue263-deck-pvideo-benchmark-v2-suite`;
AAAA → ABBA → BAAB retains the same binaries, guest work and cold-cache contract.
An authored readiness wait observes `frame=600 ` in `guest-frames.log`, then
marks a fixed 16-second CPU segment. Built-in runner analysis requires at least
24 CPU samples at its existing 500 ms interval. This is an explicitly bounded
host window after a guest frame marker; it is not the guest `Profile()` boundary,
not a whole-process average, and not an isolated PVIDEO function measurement.
No agent-side CSV analyzer or retroactive canonical-result repair is used.

The table uses medians of two per-run CPU means per build/order. **100 core %
means one fully occupied logical CPU**, so a 0.597 percentage-point saving is
0.00597 CPU cores; it is not 0.597% of the eight-logical-CPU system.

| Order | A core % | B core % | Saved percentage points | Improvement |
| --- | ---: | ---: | ---: | ---: |
| ABBA | 162.740328 | 162.143178 | 0.597151 | +0.3669% |
| BAAB | 162.652391 | 162.324859 | 0.327531 | +0.2014% |

AAAA means: 162.882094, 162.874219, 162.984344, 163.082281 core %
(range 0.208063 percentage points). All eight reference runs including A/A
span 162.433688–163.082281; four candidates span 161.980719–162.413344.
All candidate means are lower in this small dataset, but the total reference
variation (0.648594 points) and between-order effect difference are comparable
to the observed savings. No fixed native power policy or game coverage exists;
**this is a small observed reduction, not proof of a useful game speedup**.
Each window has 32 or 33 CPU samples, no missing cells and zero collector
overruns; average collector duty spans 0.2832–0.3102%. Samples within a run
are not counted as independent A/B repetitions.

The server aggregate includes 8 reference runs (with A/A controls) versus
4 candidates: 162.872656 → 162.271006 core %, +0.3694%. It is explicitly
separate from the balanced per-order estimates above. The full 47-row comparison
also retains the first campaign and unmatched/failed diagnostic cohorts. Its
cohort identity prevents these protocols from being pooled.

| Guest test / order | A µs/batch | B µs/batch | Saved µs/batch | Improvement | Correctness |
| --- | ---: | ---: | ---: | ---: | --- |
| steady_upload / ABBA | 2,132,740.563 | 2,131,666.563 | 1,074.000 | +0.0504% | PASS |
| steady_upload / BAAB | 2,133,543.125 | 2,132,825.500 | 717.625 | +0.0336% | PASS |
| resize_toggle / ABBA | 2,133,221.563 | 2,133,205.000 | 16.563 | +0.0008% | PASS |
| resize_toggle / BAAB | 2,132,606.375 | 2,132,299.938 | 306.438 | +0.0144% | PASS |

These fractional vblank-paced batch changes do not establish an FPS gain.
Full per-order min/mean/median/max/p95 tables and all individual runs remain
in the packet; none of the first campaign's negative rows is discarded.

## Rendered output and failed diagnostics

| Attempt | Canonical outcome | Independent path/image qualification |
| --- | --- | --- |
| Revision-1 source pilot | Completed, source PASS, evidence complete | **FAIL:** only 213 PVIDEO submits in final 2 of 1,427 frame bins, after timed work; excluded from PVIDEO performance |
| Revision-2 source pilot | Completed, source PASS, evidence complete | 17,808 PVIDEO submit events across 897 active frame bins (416–1440; extent 17.369702 s), covering the enabled portions of timed work |
| 1x / 50-shot candidate | Failed execution, source PASS, incomplete evidence | Only 25/50 shots before guest exit; observed visual cycle does not repair plan failure |
| 4x / 50-shot candidate, verbose trace | Completed, correctness not evaluated, invalid evidence | Trace exceeded the existing 16 MiB text-inspection limit; preserved unchanged |
| 4x / 50-shot candidate, trace off | Completed, source PASS, evidence complete | All 50 shots and ordered visual cycle PASS |
| Same 4x / 50-shot parent procedure | Completed, source PASS, evidence complete | All 50 shots and ordered visual cycle PASS |

All of these native attempts exit zero; the failed classifications above are
measurement/coverage failures, not a claimed native crash. The corrected image
procedure changes multiplier and expected work together before freezing a new
revision. It removes redundant verbose trace instead of relaxing its limit.
The successful short path diagnostic remains available separately.

The retained Python image oracle maps a declared guest viewport
`107,0,1066,800` into 1280×800 host captures. It checks 100 interior/exterior
RGB probes (background 51/76/153 and generated black/white quadrants), tolerance
2/255. Duplicates do not add phase coverage and unknown captures break a cycle.
It requires normal128 → inverse128 → normal128 → inverse64 → normal128 →
inverse128 → disabled → inverse128 in order. Both successful 4x procedures pass.
This is one sampled visual cycle, not every frame/pixel or phase-time proof.
All original PNGs and classification SHA receipts are retained.

Preparation failures remain visible too: unsupported asset-content GET returned
404; re-uploading an application directory after adding a receipt produced a
409, corrected by reusing its frozen receipt. A v2 continuation script initially
addressed completed ABBA v1; its terminal assertion stopped before v2 execution,
and the route plus response identity check were corrected. A read-only collector
then used `/api/v2` instead of the actual `/api/v1` namespace and received 404;
its log/source and correction remain too. Neither helper fault changed native
measurements. That idempotent v1
start created no native attempt. Original script, response and logs remain.

## Windows and remaining qualification

A correctness-only Windows CI executable is staged and its source pilot queued.
The runner currently owns a different paused Conker job, so it is left untouched.
Subsequent HTTP status reads timed out; no foreign job was resumed or stopped.
Its driver cache is explicitly unqualified; no Windows performance comparison
or cross-platform percentage is claimed. The Windows candidate comes from
CI run `36930561902`, artifact `11197065429`, measured-head revision `aeeb9554`.

Remaining: confirmation of whether the small CPU saving justifies investment,
unaffected and OpenGL controls; native Vulkan
validation/delayed use, reset/load/restart and teardown/resource growth;
scale/interlace/clipping/color-key coverage; representative game measurements.
The local GCC14 full-unit build still fails in unchanged resampler test code;
all 40 CI checks on the measured head pass. Focused 7 boundary + 1 real-Vulkan
lifecycle subtests and 143 host contracts pass. No full-suite/native validation
or readiness claim is implied by these focused checks.

## Packet and reproduction

The [consolidated archive](native-evidence.tar.gz) contains original canonical result/assessment,
input/host/state inventories, guest raw/normalized results, source oracles,
all planned/received screenshots, raw monitoring and frame/flip/diagnostic logs,
frozen campaign plans and maintained-client preparation/continuation recipes.
[Attempt audit](attempt-audit.json.gz), [file manifest](file-manifest.json.gz) and
full server/per-order comparisons are available alongside the archive. Logs
use lossless gzip; source SHA/length and retained-byte SHA inventories
identify each file. Binary/firmware/EEPROM/HDD/Mesa-cache contents are excluded
from redistribution; identities and omitted-file hashes remain. Generated
fixture source, permanent oracle/contracts and build instructions stay in the
test-tool draft. Emulator evidence stays with this xemu PR.

Large metadata copies are losslessly compressed for review. `file-manifest.json.gz`
describes the full archive contents; its extracted plain JSON remains in the archive.
