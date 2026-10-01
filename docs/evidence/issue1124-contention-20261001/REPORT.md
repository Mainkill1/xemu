# PGRAPH/PTIMER tests and performance closeout

## Recommendation: HOLD (test integration only)

**Summary:** The known correctness fix is unchanged. No additional runtime optimization or measured game speedup is proposed. Close the performance investigation and preserve the useful fixture and tools in a draft test PR for later integration. Fork issue #266 is already CLOSED and PR #271 is MERGED; the user's latest instruction replaces the earlier upstream correctness-only submission and endurance plan.

**Remaining:** CI/platform coverage before test integration. Contended PFIFO and game performance remain unqualified. Windows preflight prevented new native smoke runs.

## Quick correctness comparison

| State / ordering gate | Pre-flip `2b21ca0126` | Fixed main `458730bf53` |
| --- | ---: | ---: |
| Callbacks / 10,000 expected | 1 | 10,000 |
| Alarm writes, initial plus rearms | 1 | 10,001 |
| Past-target rejections | 1 | 0 |
| Renderer waits while BQL owned | 1 | 0 |
| Recurring chain | stops first event | active |
| Current hard gate | expected assertion failure | PASS |
| Forced stale CAS / final-word checks | not run | 10,000 / 10,000 |
| Controlled FIFO scenarios / seven patterns | not run | 1,792 / 1,792 |

## Additional performance opportunity

The Linux cost executable compiles scheduling hooks out and links actual production profiling, MMIO, CAS, PFIFO mutex and wake broadcasts. Each observation performs 100,000 INCREMENT read/write pairs or an empty checksum loop: 30 balanced A/B and B/A pairs per invocation. Process CPU time, pinned CPU31, same executable; frequency policy recorded but not locked. Both complete retained invocations are shown.

| Retained invocation | Empty loop / 100k | Fixed MMIO / 100k | Paired incremental ns/read-write pair | Hypothetical saving at 60 pairs/sec | Improvement % |
| --- | ---: | ---: | ---: | ---: | --- |
| [1](control-cost/receipt.json) | 0.022125ms | 4.134324ms | 41.122ns | 2.467µs CPU/sec | N/A: no candidate |
| [2](control-cost-final-v2/receipt.json) | 0.020781ms | 3.882742ms | 38.620ns | 2.317µs CPU/sec | N/A: no candidate |

The empty loop is **not an emulator candidate**. Empty-loop subtraction estimates marginal uncontended path cost. Even hypothetical removal of that entire cost saves only about 2–3µs CPU/sec at the synthetic 60-pair cadence; this does not justify another fast-path rewrite. This conditional calculation is not a title callback-rate measurement, bound on contended waits, overall emulator profile, or FPS prediction. Larger PFIFO waits would need actual host workload evidence. Further optimization is not proven impossible; no useful additional change is established here.

## Production boundary under test

The fixture includes unmodified production `pgraph.c` and `pfifo.c` to access private Kelvin methods, flip predicates and the actual FIFO worker. It links `ptimer.c` and `ptimer_core.c`. Adapters fake virtual clock/queue effects, IRQ sink, BQL identity and renderer service. Real mutexes, events, CAS, register methods, kick/check/broadcast/condition-wait logic and PTIMER callback/arithmetic execute. FIFO DMA pushing is halted; a synthetic pending-service adapter evaluates the production flip predicate. Full DMA/draw throughput is outside this fixture's scope.

The renderer holds its mutex behind an event. At a BQL-owned renderer-lock attempt, virtual time reaches the queued deadline, the timer actor confirms BQL contention with trylock, and virtual time advances by a period plus 9ms before renderer release. The real callback fires late. The synthetic guest computes `previous_target + 12,222,222 RDTSC ticks` and rejects an already-past target before writing the next alarm. RDTSC 733,333,333Hz and GPU 233,333,333Hz units are converted explicitly.

The historical control's 25,666,666ns callback lateness/BQL wait is injected virtual time, not a host performance observation or the earlier native 37.921ms wait. Fixed main performs 40,000 controls across 10,000 periods while the renderer remains held.

```text
Historical: BQL guest -> INCREMENT -> renderer wait -> late callback
            -> next target past -> guest refuses rearm
Fixed main: BQL guest -> shared atomic flip state -> PFIFO kick -> release BQL
            -> PTIMER callback -> future target -> successful rearm
This PR:    test and measurement tools only; production behavior unchanged
```

Five CAS patterns each run 2,000 forced stale snapshots: READ/WRITE increments; READ increment/modulo; WRITE/modulo fields; whole-word replacement/WRITE increment; whole-word replacement/WRITE field. Independent literal bit-position oracles check nonzero moduli and distinct unrelated-bit generations.

Seven FIFO patterns each repeat 256 times: surface during evaluation; completing READ/WRITE increments; waiting flag during wake; 100 coalesced kicks; pre-check/pre-wait kick; post-check kick. Producers contend on the mutex condition-wait atomically releases. No wall sleeps decide correctness. Meson/replay 60-second watchdogs detect hangs only.

## Verification and preserved limitations

The complete built Xbox suite passes **4 tests / 41 subtests**: 20 PTIMER, 16 PGRAPH control, 2 PFIFO flip and 3 integrated cases. New/modified test files compile without warnings. Historical replay verifies identical linked PTIMER inputs and compiles the same fixture against actual pre-flip PGRAPH/header/PFIFO. Its ordinary current gate fails; `--negative-control` asserts the complete expected failure chain. [Exact source, test and executable hashes, commands and exits](final-controls/receipt.json). The tested fixture was `45f63931c6`; the branch is rebased onto `ee5ce48b48` (a documentation-only main update). The manifest pins the rebased fixture commit and the audit verifies compiled C/header input equivalence; only output-preservation guards and evidence documentation followed testing. Roughly 0.2-second fixture duration is a fast-test property, not an emulator gain.

A broad automatic rebuild failed because local `/tmp` was full. Targeted builds use on-disk `TMPDIR`; the complete built Xbox suite passes. Broad unit/platform coverage is not claimed. Initial exploratory cost raw output was overwritten on replay before the preservation guard existed. Its [recovered aggregate](initial-cost-summary-excluded.json) is excluded from arithmetic. Both reusable drivers now reject nonempty output directories; [verification](output-preservation-check.log) confirms rejection preserves the receipt. Retained invocations use separate directories and include all samples.

A reusable four-minute Vulkan smoke definition was baked through HTTP. [Submission](smoke-vulkan-operation-status.json) failed `free_space`: 1,001,476,096 bytes available versus 1,073,741,824 required. No guest executed, so no new Vulkan callback/MMIO results exist. OpenGL was not launched under the shared storage blocker. No gate waiver, SSH bypass or endurance run. Prior native metadata is historical and does not qualify this PR's performance.

## Reuse

```sh
ninja -C build-native tests/xbox/ptimer/test-pgraph-ptimer-contention \
  tests/xbox/ptimer/benchmark-pgraph-control-cost
build-native/pyvenv/bin/meson test -C build-native --suite xbox --no-rebuild
python3 tests/xbox/ptimer/run-contention-control.py --build-dir build-native \
  --reference-source /path/to/2b21ca0126 --output /new/evidence-directory
python3 tests/xbox/ptimer/run-control-cost.py --build-dir build-native \
  --output /another/new/evidence-directory
python3 docs/evidence/issue1124-contention-20261001/audit.py
```

No XISO test code was added and no test-tool repository received evidence. Future XISO additions require their own draft test-code PR. Native tools are committed and reusable; xemu owns the results. [Manifest](manifest.json), [all hashes](SHA256SUMS). Prepared by Codex (GPT-6). No merge or ready action.
