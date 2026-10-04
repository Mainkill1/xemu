# PR #189 and #200 qualification evidence — October 4, 2026

This is an isolated evidence branch. It is not intended to merge into product main.

## Candidate identity and applicability

The 72 native Morrowind runs use PR #189 head `5fc1b6abd1715aa1ca51d4ef7d017878a26dd314` and PR #200 head `aac7fbd9fcc4db6e8ba66c8cdddaac71b4abb2b6`. Portable CI packages identify the merge commits `cfbaf86cc7fb65af9ae150522a78b37e619cf93f` and `29277c3b2b01402ee57695e6353fb9fd153f613d`; their complete trees match their corresponding heads. Reference `59e5a983` has the same runtime product source as main `b4d69b2440a33ad93987b269c2ecac97c45367bc` (the difference is three test files).

The branches subsequently integrated independent worker-wait prerequisite #305, producing #189 `00835a42f14e21fd6aa823b07028f5d85b2fce49` and #200 `683737cc003a79b199caddfd65c87b0d514c1083`. The only additional product delta is the two completion-predicate loops. Measurements here are explicitly pre-integration; they do not establish final integrated native qualification.

## Native game procedures and results

Each of six cohorts contains twelve runs: four A/A controls followed by ABBA and BAAB. Inputs and the final-input measurement boundary were unchanged. Every run completed with passing scene/evidence contracts. Deck runs have verified Mesa/private-state isolation. Windows runs lack the required driver-cache isolation ledger and are comparison-ineligible; no waiver was applied. Benchmark modes were verified from actual startup logs. #200 child logs confirm Auto4; parent Auto8/16 is inferred from config/source.

`canonical-per-run-results.csv` contains CPU mean, READ_3D cadence, average/p95/p99/max intervals and eligibility for every run. Per-run `performance.json` is the canonical runner analysis; retained telemetry matches its source identities. The installed runner lacks scoped aggregate comparison capability: these are per-run distributions, not a fabricated canonical pooled report. CPU100% means one logical CPU; READ_3D cadence is not SDL presented FPS.

- Deck #189 default: secondary medians cadence12.901→12.665/s (-1.83%), average76.060→76.994ms (-1.23% improvement), median maxima175.029→194.405ms (-11.07% improvement). A/A variation is larger than the small cadence difference.
- Deck #189 linear lowers balanced process CPU (sinc215.10–222.76%, linear208.96–212.53%) but linear cell9 p99 reaches127.653ms versus sinc102.184–116.784ms. No universal tail improvement is established.
- Deck #200 parent-linear CPU212.69–218.63%, child-linear193.85–197.44%; cadence12.530–12.969→13.104–13.330/s; p99111.746–116.743→100.180–113.452ms. Windows trends positively but is ineligible.

Hardware: Deck AMD Custom APU0405,8logical,15.52GB,amdgpu1002:163F, SteamOS Mesa25.3/RADV. Windows AMD Ryzen9 6900HX,16logical,16.36GB, RTX3070TiLaptop+AMD integrated,Windows19045,AC/high performance. Device-wide GPU measurements are retained in metrics.csv, not attributed exclusively to xemu.

## Full XISO diagnostics

Matched registered v5 suites have171leaves,149pinned oracles and22missing. ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373`, catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd`; three warmups, multiplier4,per_iteration completion,OpenGL. Both unpaired full candidate diagnostic campaigns finished23/23chunks; every leaf's actual extracted guest output and timing is retained, including failed/ineligible cases. These are not balanced before/after timing comparisons.

Windows:7passed chunks,16failed,65oracle/work checks mismatch plus missing-oracle coverage; all comparison-ineligible. Deck:4passed,19failed,70mismatch checks plus missing-oracle coverage. The pinned `dxt1_same_address_queued` reference itself contains `oracle_status: FAIL`; it cannot qualify a candidate. Unchanged-main attribution controls are in progress. Do not call these audio regressions or passes until attribution is completed.

## Worker and audio diagnostics

Corrected worker screen:45independent voices across1/2/4/8busy workers, old all-on-worker-zero routing retained as control, four repetitions each,200warmup+5000measured blocks. On the32-thread Ryzen AI MAX+395 workstation, independent medians105.53/58.89/43.47/33.10µs/block:8beats4. Four is a conservative policy for the intended rigs, not a universal optimum. Instrumented completion wait includes reduction; phase durations are not additive.

PCM numeric summaries retain two correctly configured Deck diagnostics, verified sinc/linear, non-silent/no-clipping PCM before/after active full-VM save/load. Windows also restores both modes with actual startup mode confirmation. PCM recordings are private. Recorderwall8s produced294912frames (6.144s at48kHz). After-capture starts5seconds after load: sinc contains a48ms stereo-silent interval within that later capture; this does not prove a48ms gap exactly at restore. No listening-quality or continuity qualification is claimed. Host filter history is not serialized; final-only impulse behavior and ratio-dependent synthetic guard output remain documented limits.

No firmware, commercial game resource bytes, binaries, snapshots or PCM payloads are in this evidence branch. Remaining full native renderer/title qualification, final-stack CI and accepted XISO before/after timing gates prevent a MERGE recommendation at this checkpoint.
