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

Windows:7passed chunks,16failed,65oracle/work checks mismatch plus missing-oracle coverage; all comparison-ineligible. Deck:4passed,19failed,70mismatch checks plus missing-oracle coverage. The pinned `dxt1_same_address_queued` reference itself contains `oracle_status: FAIL`; it cannot qualify a candidate. Unchanged-main controls completed all171leaves on each host. Main and189 fail exactly the same65Windows/70Deck checks;169/171output records match perhost. The only differing framebuffer hashes are the two unsynchronized same-address overwrite leaves, whose guest metadata disqualifies framebuffer comparison. All684actual leaf timing records are archived. This attributes the observed mismatches as pre-existing qualification failures; it does not approve a new oracle or make these unpaired diagnostics into balanced performance comparisons.

## Worker and audio diagnostics

Corrected worker screen:45independent voices across1/2/4/8busy workers, old all-on-worker-zero routing retained as control, four repetitions each,200warmup+5000measured blocks. On the32-thread Ryzen AI MAX+395 workstation, independent medians105.53/58.89/43.47/33.10µs/block:8beats4. Four is a conservative policy for the intended rigs, not a universal optimum. Instrumented completion wait includes reduction; phase durations are not additive.

PCM numeric summaries retain two correctly configured Deck diagnostics, verified sinc/linear, non-silent/no-clipping PCM before/after active full-VM save/load. Windows also restores both modes with actual startup mode confirmation. PCM recordings are private. Recorderwall8s produced294912frames (6.144s at48kHz). After-capture starts5seconds after load: sinc contains a48ms stereo-silent interval within that later capture; this does not prove a48ms gap exactly at restore. No listening-quality or continuity qualification is claimed. Host filter history is not serialized; final-only impulse behavior and ratio-dependent synthetic guard output remain documented limits.

No firmware, commercial game resource bytes, binaries, snapshots or PCM payloads are in this evidence branch. Remaining full native renderer/title qualification, final-stack CI and accepted XISO before/after timing gates prevent a MERGE recommendation at this checkpoint.

## Integrated native follow-up

`integrated-canonical-per-run-results.csv` adds 48 actual runs: #189 current-head OpenGL sinc/linear Morrowind (12 per host) and the independent #305 main/candidate Vulkan Morrowind (12 per host). Their per-run canonical reports and numeric telemetry are under the matching `*-integrated` directories. Procedures are unchanged, with four A/A runs followed by ABBA/BAAB. Deck is comparison-eligible; Windows remains cache-ineligible.

#189 exact-head Deck OpenGL has mixed secondary medians: CPU183.877→176.703% (+3.90%), cadence14.074→14.068/s, p9592.661→96.289ms (-3.91%), p99100.092→115.509ms (-15.40%). The #200 Auto4 OpenGL follow-up is in progress; no result is claimed for it here. #305 Deck secondary medians CPU223.763→220.297% (+1.55%), cadence12.619→12.530/s (-0.70%), average76.533→77.119ms (-0.77%), p99107.292→114.297ms (-6.53%), median maxima163.943→173.675ms (-5.93%). These distributions remain primary; mixed medians do not alone establish causation.

Two original PGR2 Deck starts completed with runner PASS but failed manual scene qualification: measurement-start image is still the Hong Kong flyover, not the parked car. They are not accepted stationary benchmark evidence. Inputs, timings and measurement boundary were not changed. Screenshots remain private local evidence.

## Current #200 uninterrupted OpenGL qualification

`integrated-worker-policy-results.csv` retains three 12-run sets: a preliminary power-split Deck cohort, an uninterrupted Deck follow-up, and an uninterrupted Windows retry. They are separately indexed and not pooled. Actual linear mode and child Auto4 were verified from startup logs. All valid runs pass execution/scene/evidence; Windows remains driver-cache-ineligible. The first Windows scene-gate failure is retained separately and was runner-terminated, not an xemu crash.

Uninterrupted Deck secondary medians: CPU177.406→167.622% (+5.52%), READ_3D14.226→14.655/s (+3.01%), average70.495→68.492ms (+2.84%), p9592.606→83.428ms (+9.91%), p99100.108→100.031ms (+0.08%), maximum117.432→116.709ms (+0.62%). Both orders improve CPU/average interval. ABBA maximum worsens116.990→124.608ms (-6.51%); BAAB improves125.477→116.708ms (+6.99%). Tail repair is not established by the flat median p99. Windows directional CPU250.099→199.490% (+20.24%), average29.849→29.260ms (+1.97%); eligibility gap remains.
