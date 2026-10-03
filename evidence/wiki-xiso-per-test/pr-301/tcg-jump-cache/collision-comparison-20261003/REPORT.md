# Deliberate collisions: baseline and recovery comparison

Recommendation: HOLD. Parent comparison improves two/eight-target collision work about 25%, but costs 56% on the above-capacity ten-target case. The current branch is a useful synthetic direction with a material regression, not ready for review as an accepted optimization. These are matched uninstrumented xemu builds and independently source-qualified CPU work contracts; rendering is unqualified. The original five-leaf missing-reference pilot remains archived separately and failed. No observed framebuffer or timing was made a golden.

## Complete results

| Comparison | CPU work | Baseline A | Candidate B | Time saved A-B | Improvement % | ABBA / BAAB | CPU correctness |
|---|---|---:|---:|---:|---:|---:|---|
| Identical parent A/A | 2 colliding targets | 0.838781 s | 0.841661 s | -0.002879 s | -0.34% | +1.33% / -0.52% | 8/8 PASS |
| Identical parent A/A | 8 colliding targets | 0.877774 s | 0.879944 s | -0.002170 s | -0.25% | +0.93% / +0.44% | 8/8 PASS |
| Identical parent A/A | 10 colliding targets (above capacity) | 0.889447 s | 0.892428 s | -0.002981 s | -0.34% | -0.54% / +0.38% | 8/8 PASS |
| Identical parent A/A | 8 noncolliding targets (equal-work control) | 0.377686 s | 0.374948 s | +0.002738 s | +0.72% | +0.27% / +0.71% | 8/8 PASS |
| Production parent → full candidate | 2 colliding targets | 0.733440 s | 0.549667 s | +0.183773 s | +25.06% | +25.13% / +33.23% | 8/8 PASS |
| Production parent → full candidate | 8 colliding targets | 0.792624 s | 0.588471 s | +0.204153 s | +25.76% | +24.28% / +32.37% | 8/8 PASS |
| Production parent → full candidate | 10 colliding targets (above capacity) | 0.805409 s | 1.257613 s | -0.452205 s | -56.15% | -57.77% / -40.46% | 8/8 PASS |
| Production parent → full candidate | 8 noncolliding targets (equal-work control) | 0.372686 s | 0.367160 s | +0.005526 s | +1.48% | +1.70% / +0.64% | 8/8 PASS |
| Recovery bypass → full candidate | 2 colliding targets | 0.917288 s | 0.565770 s | +0.351518 s | +38.32% | +24.46% / +38.37% | 8/8 PASS |
| Recovery bypass → full candidate | 8 colliding targets | 1.116886 s | 0.642513 s | +0.474373 s | +42.47% | +45.06% / +39.65% | 8/8 PASS |
| Recovery bypass → full candidate | 10 colliding targets (above capacity) | 1.015947 s | 1.266128 s | -0.250180 s | -24.63% | -18.52% / -25.01% | 8/8 PASS |
| Recovery bypass → full candidate | 8 noncolliding targets (equal-work control) | 0.477601 s | 0.380089 s | +0.097511 s | +20.42% | +17.70% / -3.02% | 8/8 PASS |

Positive Improvement %=100*(A-B)/A; negative means slower. Identical-parent A/A changes are variation, not optimization. Times are the median of four attempt-level mean work times; each attempt retains ten guest samples of 8,000,000 calls per leaf. Physical order is A1 B1 B2 A2 B3 A3 A4 B4. Four attempts per label are not forty independent observations. Raw samples, all reports and order-specific results stay available; no exclusion, rerun, confidence bound or universal noise floor is implied.

Two/eight/ten targets deliberately share a primary hash slot; equal-work eight-target noncollision uses the same instructions and recurrence with distinct slots. Target count does not prove actual C-dispatch recovery rate: loop/return PCs, generated chaining and interrupts affect it. The source hashes and guest assertions check code/geometry/results. These guest work times are not displayed FPS, host CPU time or suite wall time. A synthetic gain must be evaluated against noncollision/capacity regression and useful retail work.

## Exact comparisons and boundaries

A/A: frozen parent 76c23c7d444a6f12c9778bb2c35fab513f6c8056 against identical executable. Parent versus candidate: same parent against full product 5679cce099eb777977e761245de47e77d05d0c05, which includes enabled-only forced inlining and history/recovery. Recovery: bypass ae939df5e36e989e4e4bd6a8b424a1ea26c90f7f against full product. Bypass preserves history/fill/invalidation/callers but lookup returns NULL. It is not the production baseline or a zero-cost hook control. Its dispatch/helper instructions and linked entry addresses match the candidate except lookup body; later linked layout can differ. Do not subtract percentages across campaigns.

Parent SHA5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b; candidate SHA51cd99ef27b7212ba75029c489b31b36b44f732302cee27276c2c62756357c24; bypass SHA8febd0d31e04386c0bbf7a34761842b1ba8bbdacf3ff2b60604d458716485d4e. Matched GCC14/O2 i386-softmmu native builds, noLTO, no observer clocks/counters. Current later xemu commits add evidence only; no measured executable was rebuilt.

Deck 10.0.0.123, SteamOS/AMD Custom APU 0405, Vulkan/RADV,128MiB. Runner 0e05af4. Source ISO 19f252d, SHA87296b1b35e9f3ade67661ed53d42e9d11ff786bf9ecb07182149c9924c7767f; embedded catalog sha256:5f579ca71a19d22c66ffa213aaa665983125bacb9c962bfd690798b7e3770012. Arithmetic-suite revision a7cbe7d53b011dae8b9d7efd189cf62407fcd4414c72aabc27bdf0b5bc361171. Reference SHA db086d21eb0e600f30246c1a8bc129290dcce0f16f4308ead9765f35cd49876b from independently reviewed generator 311c258. ConfigurationSource=suite, configSHAaa356a9d..., warmups 0, multiplier 1, per_iteration. Prepared private HDD/EEPROM and cold application/private Mesa disk caches, no waiver/purge. Host power scheme is not reported; OS page cache/driver memory cache and detailed CPU clock policy are not controlled by this campaign. No PMU claim.

All 24 attempts complete and pass these four CPU contracts with complete evidence and qualified Mesa. All 7,441 indexed artifacts collected; zero exclusions. Exact guest bytes match extraction SHA. No firmware/private runtime bytes published. Whole suite remains unverified:159 other leaf oracles absent from this new suite; no rendering or physical Xbox approval. Original DirectLoop missing-oracle failure is not waived; the new selected four-leaf campaign has a distinct CPU-only purpose and reference.

## Correctness and retained tools

Source draft https://github.com/Mainkill1/xemu-perf-tests/pull/54 retains actual generated-code fixture, independent CPU reference derivation, executable IA-32 host checks, catalog/plans and docs. All146 contracts pass. The reference calculates 8M recurrence and full 4096-byte page independently; it reads no emulator output and includes no framebuffer/timing value. Guest checks every profile iteration, immutable bytes, geometry and memory release. RED missing records, wrong arithmetic/geometry negative controls and original failed native pilot remain visible.

13 actual victim-helper cases pass locally/natively, including 10,000 controlled delayed fills and guarded storage reuse; four safety mutations fail. Those are not real VM remap, spanning-page translation, reset/load, SMC/code-cache exhaustion or retail qualification. Full unit rebuild still fails in unchanged resampler math declarations; full suite success is not claimed.

## Interpretation and remaining gates

The fresh identical-parent A/A has wide per-attempt variation whose cause is unclassified: two-target 0.713494–0.955805seconds, eight-target 0.777833–0.971610, ten-target 0.800577–0.980386; equal-work noncollision 0.369380–0.378822. Balanced A/A differences are small, but that does not make every small change reliable or establish a universal noise bound. Keep the complete sample/order data.

Recovery versus its retained-fill bypass saves 38.32% on two targets and 42.47% on eight, positive in both physical orders. It costs 24.63% on ten targets, again slower in both orders. This supports the cache mechanism in fit cases and identifies search/promotion policy cost under thrashing. The bypass itself retains metadata/fill/invalidation overhead, so its gain is not the net production benefit.

Recovery's noncollision aggregate +20.42% is inconclusive: ABBA +17.70%, BAAB -3.02%. Its bypass A1/A2 means 0.581785/0.598823seconds versus A3/A4 0.364504/0.373417; full B1 is 0.597560 with B2–B4 0.374–0.381. These slow early samples and causes remain unclassified. Full candidate collision2 B2=0.833927seconds is also retained, compared with other B attempts 0.558–0.569. Do not interpret the noncollision aggregate as recovery gain, or conceal these samples.

In the parent comparison, all four candidate attempt means are below all four parent means for two/eight-target work, and all four are above all four for ten-target work. Those observed separated ranges support the respective synthetic directions, not a statistical confidence bound or retail gain. The small parent noncollision +1.48% effect remains limited by control variation.

Resolve the ten-target above-capacity cost before readiness; it is not a harmless correctness failure or something to hide behind the useful fit cases. Then qualify useful retail work and actual VM/lifetime controls. Read the whole baseline/candidate/order table before judging benefit. Strong collision results can demonstrate useful synthetic recovery; they do not predict retail gain. Small changes whose orders disagree or overlap A/A variation remain inconclusive. Noncollision and above-capacity regressions stay reported and can block readiness. Broader XISO, real VM/lifetime/reset tests, cross-renderer, reached retail and resource checks remain required for a promising candidate. PGR2 requires 300 seconds of measured validated scene. No merge authorized.

## Evidence

EVIDENCE.zip retains all public native measurements/outcomes/input manifests/frozen plans/raw guest results plus source-reference review and maintained orchestration. INDEX.json inventories every collected file with local length/SHA; private runtime-state/cache bytes and redundant diagnostics.zip are manifest-only. Resumed same-length client files are not claimed to have been rehashed remotely; guest extraction SHA is independently checked. Reports come from the maintained runner, not an alternative comparison analyzer. All failed pilot/source controls remain in linked collision-pilot packet. Evidence is stored in owning xemu draft 301.
