# Issue 229: balanced native CPU controls

**Decision: HOLD.** All 16 native Deck attempts pass correctness and evidence gates; all are comparison-eligible. The 32 leaf checks match the pinned historical CPU oracles. Timings are mixed, including a 5.50% worse OpenGL direct-loop p95. These are unaffected controls with DSP JIT enabled, not C DSP performance evidence. They do not establish a game gain or absence of regression.

## Baseline versus candidate

The server reports a median of four independent per-attempt values for each build/backend. Each attempt contains ten guest timing samples per leaf, with warmup count 3 and measurement multiplier 4. Those nested samples are not 40 independent process repetitions. Values below are **guest-reported fixed-work time**, converted from microseconds to milliseconds; they are not host CPU cost, suite duration or FPS. Saved time is before minus after; positive Improvement means less time.

### Mean work time

| Test ID | Backend | Before ms | After ms | Saved ms | Improvement | Correctness |
|---|---|---:|---:|---:|---:|---|
| `cpu_floating_point.sse_scalar` | Vulkan | 1175.756 | 1187.375 | −11.619 | −0.99% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Vulkan | 19.870 | 19.726 | +0.144 | +0.73% | 8/8 leaf checks pass |
| `cpu_floating_point.sse_scalar` | OpenGL | 1178.237 | 1196.858 | −18.622 | −1.58% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | OpenGL | 19.891 | 20.091 | −0.200 | −1.00% | 8/8 leaf checks pass |

### p95 work time

| Test ID | Backend | Before ms | After ms | Saved ms | Improvement | Correctness |
|---|---|---:|---:|---:|---:|---|
| `cpu_floating_point.sse_scalar` | Vulkan | 1189.356 | 1204.724 | −15.367 | −1.29% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Vulkan | 20.604 | 20.543 | +0.061 | +0.30% | 8/8 leaf checks pass |
| `cpu_floating_point.sse_scalar` | OpenGL | 1197.519 | 1215.927 | −18.408 | −1.54% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | OpenGL | 20.952 | 22.105 | −1.153 | −5.50% | 8/8 leaf checks pass |

These are descriptive comparisons. The runner's nearest-rank p95 for ten within-attempt samples is the maximum sample, followed here by the median of four attempt p95s. These values do not establish frame-time tails. No confidence interval or universal overhead percentage is claimed.

The Vulkan SSE result changed from +0.57% improvement in the first ABBA snapshot to −0.99% after BAAB. OpenGL SSE attempt means range 1169.066–1190.991 ms for parent and 1193.226–1212.807 ms for candidate. These observations warrant investigation; they are not dismissed as noise. Power policy/frequency were not fixed, and code/build layout remains a possible cause. This campaign cannot assign the cause of the observed slowdowns. A controlled repeat and build/layout check are required before declaring an unaffected-path regression absent.

## How this connects to the existing tests

These are existing leaves from the CPU fixture suite used in issue #245. The actual self-contained ISO is SHA-256 `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`, catalog `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`. Both leaves are revision 1. The frozen historical correctness reference is SHA-256 `fe7ea361d2427e81e58e6addfd745f037023497bebd9ea59bf68e90dbc13848f` and is retained unchanged in [historical-correctness-reference.json](historical-correctness-reference.json).

Its framebuffer expectations are `6962077c244da325` for SSE scalar and `bd1fc9bf0c318325` for direct loop. The reference is used for correctness and work/settings compatibility. **Its historical timings are not the performance baseline.** Every Before/After value above comes from this new campaign's parent/candidate runs. Existing full-suite graphics mismatches and missing references are not reclassified or replaced. This ISO has no direct DSP/audio leaf; these two leaves check unrelated whole-emulator behavior and cannot replace DSP instruction or PCM validation.

Actual parent source: `ee5ce48b48784f999af374c1452003f8b2b1230f`. Tested candidate code: `1e6cebe0cb6452f875329710fa19795f8644ad2d`, packaged by PR CI merge `b9359dbf55c72bd7e8569f677dc4aafd771592d7`; later branch changes contain evidence only. Executable hashes:

- Parent: `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
- Candidate: `84ac46913180a55b2fce18cf304fd820d3ff21a828477f7b73c0136401e17418`.

Both use the exact **parent** runtime-library bundle. The candidate CI libglib differs and is excluded. Every fixed input hash matches within each backend across both builds and both orders. The actual DSP translation units in both matching debug packages identify the same Ubuntu Clang **21.1.8** producer. Eight build/workflow files match byte-for-byte and both pinned jobs are x86-64 release jobs. [Compiler identity](native-toolchains.json), [prescribed build contract](native-build-contract.json), and both job metadata files are retained. Exact compile-command logs are absent; matching prescribed options is not a captured compiler-command audit. Earlier standalone results used GCC 14.2/Clang 19.1 and must not be relabeled as Clang 21 measurements.

## Procedure and ordering

Steam Deck/SteamOS, AMD Custom APU 0405, 128 MiB guest RAM, vsync off, ordinary reduced-work DSP setting (`use_dsp=false`), **DSP JIT enabled**. OpenGL and Vulkan have distinct frozen definitions and comparison cohorts. Firmware, ISO, oracle, EEPROM seed and prepared HDD are pinned. Every attempt receives a private EEPROM/HDD and cold private Mesa disk namespace. OS page cache and driver RAM remain uncontrolled and disclosed.

A is parent, B is candidate. One server-owned ABBA block and one BAAB block per backend yield four attempts per build/backend, 16 total. BAAB uses the same maintained ABBA planner with the reference/application roles reversed; the frozen physical executable sequence is checked before starting. No agent loop schedules child attempts, no rerun is added, no failure is filtered, and no transfer/diagnostic runs alongside a benchmark. All operations use maintained HTTP clients; no SSH test execution occurs.

| Campaign | Physical order | Finished | Correctness | Comparison eligible |
|---|---|---:|---:|---:|
| `issue229-deck-cpu-vulkan-abba-v1` | ABBA | 4/4 | 4/4 | 4/4 |
| `issue229-deck-cpu-vulkan-baab-v1` | BAAB | 4/4 | 4/4 | 4/4 |
| `issue229-deck-cpu-opengl-abba-v1` | ABBA | 4/4 | 4/4 | 4/4 |
| `issue229-deck-cpu-opengl-baab-v1` | BAAB | 4/4 | 4/4 | 4/4 |

Runner `0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e`, instance `63e6c849eb694b3a9d11848efe26d08a`. Frozen CPU control definitions:

- Vulkan test revision `316aba385bd1718f8d765e22e157aecaae0f124124a9a30a00b779457e6e76ad`; suite revision `6943b9b72692638724ec9d7df010ce3cbd328c05bd77dc4ab55864d4a5ba2ae3`.
- OpenGL test revision `730129843d496bcc20c9e118f130d4be79bd350d3d65ed605121566751c923f9`; suite revision `24dd41f8c92686d58bfa61123ebc832a911737a6f8457cea289cfeeeaacec583`.

The inventory retains qualification label `unverified`: this is not promotion of the whole ISO to an approved baseline. Each selected attempt separately passes the canonical correctness/evidence/comparison contract. Storage schema 2 records observed private Mesa writes, a stopped target and no issues on **all 16 attempts**, without `AllowUncontrolledDriverCache`. Final runner state is idle, with no pending/testing job or current process. Windows was inspected while idle; no Windows attempt belongs to this campaign.

## Complete evidence and reuse

[final-comparison.csv](final-comparison.csv) preserves all **48** server-derived comparison rows, including other ineligible historical/native cohorts. The compact [JSON snapshot](final-comparison.json) is deliberately bounded and reports omitted rows; it is not full coverage. The two CPU leaves above use distinct backend cohorts with `n_a=n_b=4`. [attempts-audit.json](attempts-audit.json) contains all 16 run IDs, canonical outcomes, per-leaf times, fixed hashes and cache qualification. Run IDs are also in each campaign's terminal attempt list under `campaigns/`.

Four archives under `raw/` preserve the **complete** eligible artifact inventories, including original guest bytes, normalized results, coverage/selection receipts, input/job/launch hashes, sampler CSV, logs and cache ledgers. Inventory files preserve every path, length and SHA-256; every archived member was read back and checked. They retain 1,247 files per Vulkan campaign and 639 per OpenGL campaign. This is durable owning-xemu evidence; no evidence-only change was made to either tool repository.

The preparation, freeze, collection and audit scripts retain the executed workspace-specific recipes. Immutable definitions under `frozen-procedures/` can be reused through `runner_xiso.py`; new deliberate repetitions require new campaign IDs. Existing definitions and references remain intact. These are additional saved procedures built on the retained XISO tool, not disposable tests or new guest code.

Remaining gates: explain or bound the observed unaffected-control slowdowns, qualified native C/JIT warm-scene comparisons, ordinary/reduced C profiling, native PCM parity, audio queue/underrun checks, and broader required regression coverage. The faster synthetic C multiply remains useful preliminary evidence. Game gains remain unproven, and #278 remains draft.
