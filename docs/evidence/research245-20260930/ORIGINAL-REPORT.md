# Research #245: current-main invalidation reachability and cost

## Decision

Individual TB invalidations are frequent in the admitted stationary PGR2 scene
on both Windows and Steam Deck. Continue with an opt-in cache occupancy and
lookup attribution probe. Keep the existing invalidation behavior until that
probe and the issue's stale-entry safety tests justify a candidate.

This is a partial first-gate investigation of
[Mainkill1/xemu #245](https://github.com/Mainkill1/xemu/issues/245), with no
product code change, speedup claim, or completed optimization qualification.
The issue's fork-local Halo figures are not measurements of these builds.

## Exact inputs and operation

- Product source: `2d289cb349bca95eae81b6a54b0f8d965365ff82`, current `main`.
- Published build workflow: [36687928103](https://github.com/Mainkill1/xemu/actions/runs/36687928103).
- Windows executable SHA-256:
  `70173532e9057a2dbebce15be1fea4b8482e4e780329375ca49cb7785439503d`.
- Linux AppImage SHA-256:
  `3f8bad0fae4dc19bc145a4523fc41f5e6793db7b986f884dfa6f9cc2fb0eb43a`.
- Unchanged executable extracted from that AppImage:
  `5b3764acb93ee6d319b9cf7822295ae8748896b87b39ba52702838f1294f22f9`;
  ELF build ID `767622c38f0e8f2726c236b62e5b3075f8c31e22`.
- Maintained runner: Deck `6089e8b841bce379015500853c0455551d7fd2cf`,
  Windows `079e3d483523b3e53a11983efd40e2c530747b5f`.
- Normal tests used the runner HTTP clients from the build host. No SSH test
  launches, queue edits, guest networking, or global cache deletion.

The Deck uses registered immutable firmware/disc/save assets and private
HDD/EEPROM. Windows reuses the retained private test inputs. Configurations,
effective launch/input manifests, hashes, and saved test definitions are
retained here; guest inputs and executables are excluded. Windows and Deck
menu procedures, seeds, and race targets differ, so these are independent
reachability observations, not a cross-host performance comparison.

Existing HMP `info jit` snapshots bracket the 30-second named stationary
segment. Screenshot/query overhead makes the snapshot intervals longer than
30 seconds. Rates below use diagnostic invocation start times as an
approximation; the actual counter reads occur inside those short queries.

## Measured reachability

| Run | TB invalidations before → after | Delta | Snapshot interval | Approximate rate | Full TB flushes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows w1 | 39,333 → 77,531 | 38,198 | 32.323134 s | 1,181.75/s | 0 → 0 |
| Deck native v2 | 98,078 → 138,051 | 39,973 | 31.383629 s | 1,273.69/s | 0 → 0 |
| Deck perf v1 | see raw snapshots | 39,967 | 31.177604 s | 1,281.91/s | 0 → 0 |

All three runs completed, exited zero, passed the runner correctness contract,
and retained complete required evidence. All six start/end screenshots were
manually inspected: stationary race, 000 MPH. The nonblack-image contract by
itself would not prove this scene admission.

Current source sets `CF_PCREL` for system-mode x86 TCG; individual PC-relative
invalidation calls `tcg_flush_jmp_cache()` for each vCPU, clearing 4,096 slots.
The successful-invalidation statistic is incremented after that call. This
source audit makes the measured invalidation rate relevant to #245. It is
**an inference from source**, not a measured PC-relative/flush/occupancy
breakdown. HMP full **TB** flush count is a different counter from jump-cache
flushes. No discarded-entry count or post-flush miss count was collected.

## Diagnostic performance checks

These values are copied from the runner's retained `performance.json` files.
CPU is process core-percent; guest cadence is distinct from rendered FPS.

| Run | Mean CPU | Guest cadence | Frame p99 | Actual maximum |
| --- | ---: | ---: | ---: | ---: |
| Windows w1 | 327.81% | 30.00/s | 34.081 ms | 34.390 ms |
| Deck native v2 | 303.71% | 24.28/s | 56.089 ms | 69.246 ms |
| Deck perf v1 | 302.87% | 24.37/s | 57.415 ms | 951.861 ms |

The perf capture follows the named CPU measurement segment. The runner's
frame-tail analysis also sees later frame-log data; the perf run contains a
pause/report/resume sequence and its 951.861 ms maximum is retained. It is
unsuitable for an uninstrumented frame-tail comparison. Windows collector
duty is also substantial. All three comparison outcomes remain ineligible:
Windows records diagnostic/transfer intervention, and Deck driver-cache
namespace control remains unverified. No policy was relaxed to obtain a pass.

## Five-second userspace profile

The maintained runner's external diagnostic adapter ran literal arguments:

```text
perf record -e cycles:u -p {pid} -g --call-graph dwarf -F 499
  -o {resultDir}/research245-perf.data -- sleep 5
```

The runner paused, attached to its actual xemu PID, resumed, captured, paused,
generated a report, and resumed before normal quit. User-only sampling worked
with the existing `perf_event_paranoid=2`; host permissions were not changed.
Capture: **8,714 samples, zero lost samples**, 5.007089 seconds between first
and last samples. `perf-identity.json` pins the original 73,819,992-byte payload;
the committed gzip preserves its exact bytes and SHA-256.

| Resolved self symbol | Weighted userspace cycles | Samples |
| --- | ---: | ---: |
| `qht_lookup_custom` | 2.23% | 151 |
| `tb_lookup_cmp` | 0.59% | 38 |
| `tb_htable_lookup_common` | 0.33% | 23 |
| `do_tb_phys_invalidate` | 0.12% | 8 |

The first three self rows sum to about 3.15% of sampled process cycles;
callchains connect them to TB lookup. The invalidation row includes the
inlined jump-cache flush. Eight samples provide weak precision. These are
weighted event shares, not percentages of wall time or predicted savings.
Lookup costs have other causes, and JIT/host-library symbol coverage is
incomplete. The report does not establish how much lookup work a retained
jump cache would avoid.

Offline symbolization used the matching CI release `.ddeb` and the exact
extracted executable/libraries under their recorded paths in `--symfs`.
Providing only the debug file produced incorrect names because executable
mapping offsets matter; those preliminary reports are excluded. The correct
raw report, whitespace-collapsed readable view, self rows, build IDs, header,
and stderr are retained. Reproduction requires both the executable and debug
file, not merely a matching build-ID string.

## Retained unsuccessful attempts

| Request | Outcome | Cause / resolution |
| --- | --- | --- |
| Deck AppImage v1 | Preparation failed; no run | Saved test required two missing library build slots; retained receipt |
| Deck AppImage v2 | Plan failed; forced exit 137 | Extract-and-run child PID did not match runner's X11 window ownership; use inner executable |
| Deck native v1 | Control error; exit 127 | Older saved test carried only two library slots; missing `libcurl-gnutls.so.4`; save all 41 current package files |

Failed runs are excluded from the successful tables. Their errors, available
launch/result evidence and failure screenshot are retained. Driver cache
binaries are excluded from the public evidence payload.

## Remaining gates

The next probe must count PC-relative invalidations, occupied entries
discarded, cache hits/misses, global lookup/retranslation work, and sampled
cost. Broaden to the CPU-heavy rewrite workload, Morrowind, and a low-
invalidation control required by #245. No stale-entry, remapping, spanning-
page, reclamation, reset/load, debug-mode, or concurrency candidate tests have
been run. Balanced uninstrumented A/A, ABBA/BAAB, previous-main and fixed-cycle
baseline comparisons remain absent. Retain the current behavior until those
gates pass.

Run `python3 audit.py` to verify completed diagnostic contracts, counter
arithmetic, executable identities in the summary, and decompressed perf hash.
`SHA256SUMS` pins every other committed evidence file.

Agent declaration: researched, tested and written with Codex / GPT-6.
