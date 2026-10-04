# PR #189 and #200 qualification evidence — October 4, 2026

This is an isolated evidence branch. It is not intended to merge into product main.

## Candidate identity and applicability

The 72 native Morrowind runs use PR #189 head `5fc1b6abd1715aa1ca51d4ef7d017878a26dd314` and PR #200 head `aac7fbd9fcc4db6e8ba66c8cdddaac71b4abb2b6`. Portable CI packages identify the merge commits `cfbaf86cc7fb65af9ae150522a78b37e619cf93f` and `29277c3b2b01402ee57695e6353fb9fd153f613d`; their complete trees match their corresponding heads. Reference `59e5a983` has the same runtime product source as main `b4d69b2440a33ad93987b269c2ecac97c45367bc` (the difference is three test files).

The branches subsequently integrated independent worker-wait prerequisite #305, producing #189 `00835a42f14e21fd6aa823b07028f5d85b2fce49` and #200 `683737cc003a79b199caddfd65c87b0d514c1083`. The only additional product delta is the two completion-predicate loops. The initial cohorts are explicitly pre-integration. Final integrated cohorts are recorded separately below.

## Native game procedures and results

Each of six cohorts contains twelve runs: four A/A controls followed by ABBA and BAAB. Inputs and the final-input measurement boundary were unchanged. Every run completed with passing scene/evidence contracts. Deck runs have verified Mesa/private-state isolation. Windows runs lack the required driver-cache isolation ledger and are comparison-ineligible; no waiver was applied. Benchmark modes were verified from actual startup logs. #200 child logs confirm Auto4; parent Auto8/16 is inferred from config/source.

`canonical-per-run-results.csv` contains CPU mean, READ_3D cadence, average/p95/p99/max intervals and eligibility for every run. Per-run `performance.json` is the canonical runner analysis; retained telemetry matches its source identities. The installed runner lacks scoped aggregate comparison capability: these are per-run distributions, not a fabricated canonical pooled report. CPU100% means one logical CPU; READ_3D cadence is not SDL presented FPS.

- Deck #189 default: secondary medians cadence12.901→12.665/s (-1.83%), average76.060→76.994ms (-1.23% improvement), median maxima175.029→194.405ms (-11.07% improvement). A/A variation is larger than the small cadence difference.
- Deck #189 linear lowers balanced process CPU (sinc215.10–222.76%, linear208.96–212.53%) but linear cell9 p99 reaches127.653ms versus sinc102.184–116.784ms. No universal tail improvement is established.
- Deck #200 parent-linear CPU212.69–218.63%, child-linear193.85–197.44%; cadence12.530–12.969→13.104–13.330/s; p99111.746–116.743→100.180–113.452ms. Windows trends positively but is ineligible.

Hardware: Deck AMD Custom APU0405,8logical,15.52GB,amdgpu1002:163F, SteamOS Mesa25.3/RADV. Windows AMD Ryzen9 6900HX,16logical,16.36GB, RTX3070TiLaptop+AMD integrated,Windows19045,AC/high performance. Device-wide GPU measurements are retained in metrics.csv, not attributed exclusively to xemu.

## Full XISO diagnostics

Matched registered v5 suites have171leaves,149pinned oracles and22missing. ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373`, catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd`; three warmups, multiplier4,per_iteration completion,OpenGL. Both unpaired full candidate diagnostic campaigns finished23/23chunks; every leaf's actual extracted guest output and timing is retained, including failed/ineligible cases. These are not balanced before/after timing comparisons.

Windows:7passed chunks,16failed,65oracle/work checks mismatch plus missing-oracle coverage; all comparison-ineligible. Deck:4passed,19failed,70mismatch checks plus missing-oracle coverage. The pinned `dxt1_same_address_queued` reference itself contains `oracle_status: FAIL`; it cannot qualify a candidate. Unchanged-main controls completed all171leaves on each host. Main and189 fail exactly the same65Windows/70Deck checks;169/171output records match perhost. The only differing framebuffer hashes are the two unsynchronized same-address overwrite leaves, whose guest metadata disqualifies framebuffer comparison. All684actual leaf timing records are archived. This attributes the observed mismatches as pre-existing qualification failures; it does not approve a new oracle or make these unpaired diagnostics into balanced performance comparisons.

## Worker and audio diagnostics

Corrected worker screen:45independent voices across1/2/4/8busy workers, old all-on-worker-zero routing retained as control, four repetitions each,200warmup+5000measured blocks. On the32-thread Ryzen AI MAX+395 workstation, independent medians105.53/58.89/43.47/33.10µs/block:8beats4. Four is a conservative policy for the intended rigs, not a universal optimum. Instrumented completion wait includes reduction; phase durations are not additive.

PCM numeric summaries retain two correctly configured Deck diagnostics, verified sinc/linear, non-silent/no-clipping PCM before/after active full-VM save/load. Windows also restores both modes with actual startup mode confirmation. PCM recordings are private. Recorderwall8s produced294912frames (6.144s at48kHz). After-capture starts5seconds after load: sinc contains a48ms stereo-silent interval within that later capture; this does not prove a48ms gap exactly at restore. No listening-quality or continuity qualification is claimed. Host filter history is not serialized; final-only impulse behavior and ratio-dependent synthetic guard output remain documented limits.

No firmware, commercial game resource bytes, binaries, snapshots or PCM payloads are in this evidence branch. All three final heads have green exact-head CI. Remaining audio acceptance, full XISO oracle gaps, Windows cache eligibility and PGR2 scene qualification prevent a MERGE recommendation.

## Integrated native follow-up

`integrated-canonical-per-run-results.csv` adds 48 actual runs: #189 current-head OpenGL sinc/linear Morrowind (12 per host) and the independent #305 main/candidate Vulkan Morrowind (12 per host). Their per-run canonical reports and numeric telemetry are under the matching `*-integrated` directories. Procedures are unchanged, with four A/A runs followed by ABBA/BAAB. Deck is comparison-eligible; Windows remains cache-ineligible.

#189 exact-head Deck OpenGL has mixed secondary medians: CPU183.877→176.703% (+3.90%), cadence14.074→14.068/s, p9592.661→96.289ms (-3.91%), p99100.092→115.509ms (-15.40%). The completed #200 Auto4 OpenGL follow-up is recorded below. #305 Deck secondary medians CPU223.763→220.297% (+1.55%), cadence12.619→12.530/s (-0.70%), average76.533→77.119ms (-0.77%), p99107.292→114.297ms (-6.53%), median maxima163.943→173.675ms (-5.93%). These distributions remain primary; mixed medians do not alone establish causation.

Two original PGR2 Deck starts completed with runner PASS but failed manual scene qualification: measurement-start image is still the Hong Kong flyover, not the parked car. They are not accepted stationary benchmark evidence. Inputs, timings and measurement boundary were not changed. Screenshots remain private local evidence.

## Current #200 uninterrupted OpenGL qualification

`integrated-worker-policy-results.csv` retains three 12-run sets: a preliminary power-split Deck cohort, an uninterrupted Deck follow-up, and an uninterrupted Windows retry. They are separately indexed and not pooled. Actual linear mode and child Auto4 were verified from startup logs. All valid runs pass execution/scene/evidence; Windows remains driver-cache-ineligible. The first Windows scene-gate failure is retained separately and was runner-terminated, not an xemu crash.

Uninterrupted Deck secondary medians: CPU177.406→167.622% (+5.52%), READ_3D14.226→14.655/s (+3.01%), average70.495→68.492ms (+2.84%), p9592.606→83.428ms (+9.91%), p99100.108→100.031ms (+0.08%), maximum117.432→116.709ms (+0.62%). Both orders improve CPU/average interval. ABBA maximum worsens116.990→124.608ms (-6.51%); BAAB improves125.477→116.708ms (+6.99%). Tail repair is not established by the flat median p99. Windows directional CPU250.099→199.490% (+20.24%), average29.849→29.260ms (+1.97%); eligibility gap remains.

## Integrated balanced XISO controls

Parent #305 versus #189 default sinc completed 16 attempts per host (two leaves, ABBA+BAAB each). Cross-host plan identity matches. All 32 attempts pass execution/correctness/evidence, and extracted FATX guest results retain each leaf mean/median/min/max/p95, total work time and separate runner wall time. `balanced-xiso-controls-per-attempt-times.csv` retains every attempt. These CPU/texture leaves are unaffected controls; no affected APU/audio leaf exists in this catalog.

Deck all16eligible/indexed. Canonical report remains **inconclusive**: texture MIN has opposing orders, although both primary medians improve in both orders. CPU median 1,195,661.75→1,174,026.75µs (−21,635µs; +1.81%), texture median56,564.25→54,266µs (−2,298.25µs; +4.06%). All >1% rows are retained in the report. This does not attribute unrelated control gains to APU conversion.

Windows all16cache-ineligible; canonical report is **ineligible**, not a pass. Secondary CPU median517,033.5→524,110.75µs (+7,077.25µs; −1.37%), with opposing ABBA+2.63%/BAAB−4.00%. Texture median28,254.75→28,346µs (+91.25µs; −0.32%). Other >1% observations: CPU max/p95+1.15%, texture minimum+1.16%. Every attempt was indexed explicitly and canonical records retrieved; report `indexedAttempts=0` reflects its eligible-only counting filter, not missing run data. No cache waiver or pinned baseline change.

## Integrated state and PCM diagnostics

The six native final-head diagnostics reuse the unchanged saved procedures: #189 sinc, #189 linear and #200 linear Auto4 on both hosts. All complete active full-VM save/load, with actual mode/effective-worker startup proof. Per-run assessments and input manifests are retained under `pr200/*-integrated-lifecycle`. Numeric PCM summaries retain only measurements; audio bytes remain private. This is a diagnostic procedure, not performance evidence.

Deck each before/after capture has294,912stereo frames (6.144s@48k), non-silent and unclipped. The initial Auto4 after-capture contains three512/1,024-frame stereo-zero bursts (maximum21.333ms). Capture starts five seconds after restore, so these are not measured gaps exactly at the load boundary. The completed repeated diagnostic is recorded below.

## Balanced parent/child PCM repeat

Eight unchanged v4 save/load runs (ABBA then BAAB) completed and all state/execution/evidence checks passed. Mode/startup logs verify parent linear and child linear Auto4. Sixteen before/after process-isolated captures each contain294,912stereo frames@48k, nonzero PCM and no clipping.

| Build | Captures with stereo silence ≥1ms | Bursts | Total silent time | Maximum |
|---|---:|---:|---:|---:|
| Parent #189 linear Auto8 |4/8 (2before,2after)|6|90.667ms|26.667ms|
| Child #200 linear Auto4 |1/8 (0before,1after)|3|42.667ms|26.667ms|

The silence is not unique to the child; this small diagnostic does not establish that the child prevents dropouts, nor identify whether xemu production, SDL delivery or PipeWire capture caused them. Before/after captures are separate sample windows and start five seconds after restore, so neither proves sample-continuous load. Raw PCM stays private; exact burst frame ranges and numeric measurements are in `pr200/deck-balanced-pcm/summary.json`, with every run outcome and actual mode proof. No source, controller inputs, measurement boundaries or fixed baselines changed for these repeats.
