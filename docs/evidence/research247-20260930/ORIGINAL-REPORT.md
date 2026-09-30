# Research 247: physical voice-write attribution and performance checks

## Summary and decision

An independently written, opt-in probe around the existing voice-word read and
physical store reproduces a large equality opportunity: **88.19–88.23% unchanged
writes in three Windows PGR2 runs and 88.62% in the admitted Steam Deck run**.
The envelope count register is the clearest initial target. All physical stores
remain. This supports investigating an equal-write candidate, not accepting one.
No production speedup, PCM equivalence, or safe RAM-only elision is established.
Both associated PRs are drafts; nothing is merged.

## Investigation and hypothesis

[Issue #247](https://github.com/Mainkill1/xemu/issues/247) asks whether repeated
voice state stores waste CPU and dirty/invalidation work. Its proposed first
gate is attribution before behavior changes. The issue references
`izzy2lost/xemu@e8e92c077a50ab7ec7075a88e0f0014be1071699` (`vp.c`). No foreign
implementation source was opened or copied. Target APIs and code provided the
independent design. No overlapping #247 PR was found in the initial inventory;
other audio experiments are separate changes.

### Processing flow and code changes

Current: authoritative physical read → masked word calculation → physical store.
Probe: same read/calculation → atomic attribution → optional timing bracket →
**same physical store** → optional elapsed-time sample. Workers use atomic
register/phase buckets. Approximately 1/1024 samples use a mixed sequence to
avoid fixed-stride aliasing. No persistent shadow cache or equal-store branch
is added. A build option defaults off; a writable environment-selected file
activates an instrumented build. Normal exit pauses the APU before aggregating;
unrealization removes the exit notifier. Forced termination may lose output.

Phase 0–7 is EF, 8–15 is EA, and 16 is outside envelope stepping. Exposure uses
APU frames and existing debug active flags (processed entries, including paused
entries); it is not a guest-memory census at frame end. Counters cover the
**whole process including boot/navigation**, not only the performance segment.

## Exact identities

| Item | Identity |
| --- | --- |
| Previous main | `2d289cb349bca95eae81b6a54b0f8d965365ff82` |
| Measured product code | `abd598cf596aff497d3b962bb309221f2d3092cc` |
| Measured product tree | `c9759b7ab6067f2a9f51c6d3c88591293e5b4cde` |
| Current-main CI Windows executable | `70173532e9057a2dbebce15be1fea4b8482e4e780329375ca49cb7785439503d` |
| Diagnostic Windows executable | `cdc586252855f926002f7e4337a44aa7e692d6e5c5efc0b5f2b7c5d815089144` |
| Diagnostic Linux executable | `0f00290ec9b03d53b2f7237d884cb1a320c4fd850e76b5eea1250d34c59a155f` |
| Fixed cycle product reference | `bd1fecb93353272dda2a810991e28945de35b665`, retained binary `3489fdcc593e942b92a612bf35a98f509ff0907e3370e1e5f45f2972d83fb16b` |
| Fixed baseline comparisons | Not collected under these saved test revisions; no delta claimed |
| Windows runner | `0.1.0+079e3d483523b3e53a11983efd40e2c530747b5f` |
| Deck runner and local client source | `0.2.0+6089e8b841bce379015500853c0455551d7fd2cf` |

The product PR's later documentation commit does not change the measured code.
The original current-main executable comes from successful CI run `36687928103`.
Diagnostic Linux was GCC 14 and system SDL3; Windows used the installed MXE
Clang wrapper and static dependencies, with an O2 development build. These
are **different build pipelines from the current-main CI binary**. Cross-pipeline
CPU differences cannot identify a source-change effect.

The closest local version tag already includes a git suffix; after a new commit
it produces a double suffix rejected by the repository version validator. Local
builds used a separate tag-free bare source identity and the supported
`XEMU_VERSION=0.8.136` file fallback. Generated headers identify the exact measured
commit. This is disclosed build metadata, not a product version-script change.
Cross-build setup also required `VULKAN_SDK` pointing to the MXE target root;
otherwise CMake selected host Linux headers. `TMPDIR` used root-disk scratch
because the shared tmpfs was full. No existing tmpfs evidence was deleted.

## Attribution results

| Run | Attempts | Unchanged | Unchanged ratio | APU frames | Max debug active entries |
| --- | ---: | ---: | ---: | ---: | ---: |
| w1 | 29,768,907 | 26,254,382 | 88.19% | 117,764 | 108 |
| w2 | 29,771,497 | 26,266,614 | 88.23% | 117,972 | 109 |
| w3 | 29,756,555 | 26,251,808 | 88.22% | 118,148 | 107 |
| d4 | 30,677,098 | 27,447,860 | 89.47% | 175,173 | 91 |
| d5 | 45,014,071 | 39,892,051 | 88.62% | 205,165 | 96 |

`d4` includes race loading during its requested stationary segment. Its complete
trace is useful as whole-process diagnostic exposure; its segment timing is
**not admitted as a stationary-race measurement**. `d5` adds a 20-second loading
margin; both saved screenshots show the stationary race at 000 MPH. Windows
runs show their stationary Hong Kong race; Windows and Deck use different saved
seeds, procedures and race targets, so no cross-host timing or exposure ratio
comparison is qualified.

In `w1`, register `0x34` (envelope count) had 10,547,745 attempts, 99.6657%
unchanged; `0x54` (state) 99.9398%; `0x58` (offset) 64.6247%; `0x5c` (next)
99.9123%. Off-phase EF/EA envelope count writes dominate the equality exposure.
The exact register/phase counts remain in each `voice-writes.csv`.

For `w1`, the changed bracket averaged 162.76 ns over 3,397 samples and unchanged
172.23 ns over 25,484 samples; `w2` was 164.90 ns over 3,419 and 176.91 ns over
25,460. These include timer-read cost and scheduling effects, exclude counter
updates, and have coarse clock quantization. No sample means no cost estimate
for that bucket. Do not extrapolate these brackets into a production speedup.

## Performance checks

Windows start images include the race-start camera; `w3` still shows a countdown
at the first checkpoint. The last-25-second frame tail excludes the beginning,
but the CPU profile covers the entire requested segment. Coarse saved input
waits do not establish identical scene timing, so the observer check remains
descriptive even beyond its cache and sample-size limitations.

These are the runner's retained analysis values, not recalculated host CSV
statistics. CPU core percent means 100% is one core. Guest cadence is distinct
from rendered FPS; p99/max refer to guest frame intervals. Diagnostic timings
are excluded from product performance acceptance.

| Run | CPU mean, core % | Guest cadence, fps | Guest interval p99, ms | Maximum, ms |
| --- | ---: | ---: | ---: | ---: |
| main-aa1 | 309.87 | 30.00 | 34.00 | 34.33 |
| main-aa2 | 311.10 | 30.00 | 34.14 | 34.64 |
| w1 | 396.95 | 30.00 | 34.48 | 35.12 |
| w2 | 397.83 | 30.00 | 34.56 | 35.42 |
| off1 | 380.02 | 30.00 | 34.32 | 35.47 |
| off2 | 389.54 | 30.00 | 34.40 | 34.86 |
| w3 | 393.61 | 30.00 | 34.34 | 34.76 |
| d4 | 355.50 | 18.89 | 79.09 | 93.81 |
| d5 | 320.24 | 24.12 | 60.03 | 66.16 |

### Observer overhead and unfavorable evidence

The same Windows binary ran **ON/OFF/OFF/ON** (`w2`, `off1`, `off2`, `w3`):
only the trace environment setting and its evidence requirement differ. The
OFF runs produce no trace. This is a diagnostic overhead check with two
observations per setting, not an optimization trial. Windows collector duty
was approximately 33–37% in these profiles; the large observation cost and
uncontrolled cache state limit interpretation. Even runtime-OFF uses a build
with the probe compiled in. A default-OFF production build removes the probe
entirely, verified separately by the absence of collector/TLS symbols.

Windows diagnostic jobs have runner `comparison=eligible` because their
experiment explicitly allows diagnostics. This label does not certify
production qualification: state is unmanaged, driver controls are incomplete,
and the diagnostic binary is excluded by project policy. Current-main A/A
runs and Deck runs are runner-ineligible. Neither A/A nor the ON/OFF sequence
provides a qualified noise floor. GPU/RSS aggregate scores are not provided by
this saved performance profile; no missing resource numbers are invented.

Candidate versus previous main: no accepted production candidate or qualified
causal delta. Candidate versus fixed cycle baseline: not measured. XISO/PCM
qualification: not performed for this first diagnostic; no comparison rows
are invented.

## Correctness, validation and retained failures

- Four production-collector unit cases pass: counts/cost, phase separation plus
  a periodic million-call mixed-class sampling regression, four concurrent
  writers, and disabled/bad-path/range behavior.
- The periodic workload failed on the original stride sampler with zero changed
  samples; the mixed sequence passes. Strict ASan/UBSan build passes all four.
- Native diagnostic, Windows diagnostic and native default-OFF full emulator
  builds complete. New files pass clang-format; `git diff --check` passes.
  Existing graphics/header/third-party warnings remain; changed code introduces
  no identified warnings. This is not a claim of an entirely warning-free build.
- Adjacent `test-xbox-mcpx-apu-resampler` fails to compile on GCC 14 because its
  existing source lacks `<math.h>` for `sinf`/`cosf`. It is retained separately;
  the full unit suite is not claimed green and unrelated fixes are not bundled.
- Review found normal-quit aggregate loss and fixed-stride sampling aliasing.
  Both were repaired and reviewed again. The final Windows and Deck normal
  quits return exit zero and have numeric summary rows. `audit.py` verifies
  changed+unchanged=attempts and bytes=4×attempts for complete retained traces.
- Scene admission is bounded to screenshots and guest progress. It does not
  prove audio equivalence, guest-memory equivalence, or broad game compatibility.

### Deck setup and failures

The new Deck at `10.0.0.123` initially had no runner/assets. Published runner
0.2.0 was bootstrapped as a desktop user service. Normal tests then used the
maintained HTTP client only. A scoped firewalld rule for TCP 9368 from
`10.0.100.1/32` in the wired `dmz` zone was applied at runtime and permanently;
HTTP reachability was verified. The firewall remains enabled. System library
preflight and private firmware/disc acquisition were separate operator setup.
Game images, firmware, writable disks and credentials are absent from this
repository; only hashes, test definitions and bounded result evidence are saved.

`d1`: rejected before launch because authored private HDD/EEPROM paths were
missing. `d2`: failed before QMP because an older bundled slirp lacked the
required `SLIRP_4.7` version. `d3`: launched xemu but plan failed on unsupported
button name `Right`; forced failure termination left an incomplete trace.
`d4`: corrected button plan exited normally, but its first recording screenshot
shows the race-loading introduction. `d5`: added the loading margin and passed
scene admission. Earlier attempts/revisions are preserved, not overwritten.
Deck driver namespace remains unverified with uncontrolled driver/OS page
caches; that blocker was not waived.

## Remaining proof before any equal-write candidate

The typed physical-store helper translates the address, handles MMIO, and for
RAM writes calls `invalidate_and_set_dirty`, which can invalidate translated
code and mark NV2A/VGA/migration dirtiness even for equal words. Generic flat
view writes have a RAM access callback; the typed store here does not invoke
that callback. The numeric `ram_range` counter is **not a translated ordinary
RAM guard**. Dirty/invalidation callback counts and costs remain unmeasured.
Guest writes can also interleave between the authoritative read and store;
read-time equality alone does not prove that skipping the later write preserves
register ownership or ordering.

Prove the actual RAM guard and observable store/dirty/callback/interleaving
semantics with the real helper; cover reset/save/load; compare PCM and guest
state; add Forza, Morrowind, high-voice/phase diversity and an audio-light
control; then run matched uninstrumented ABBA/BAAB previous-main and fixed
baseline comparisons. This draft keeps that work explicit and pending.

## Retained runs and evidence

| Label | Run ID | Execution | Correctness / evidence / runner comparison |
| --- | --- | --- | --- |
| d1 | `20260930-102509286-47d976bf532941ea8fc870f6700af86a` | start_failed | failed / incomplete / ineligible |
| d2 | `20260930-102921311-fc980115e64941698750b19ada203164` | control_error | failed / incomplete / ineligible |
| d3 | `20260930-103141398-f0af4140332c485a85ee5f7414ea6977` | plan_failed | failed / incomplete / ineligible |
| d4 | `20260930-103401396-f936c1a9fc934be6ac28e1e4e0bfa937` | completed | passed / complete / ineligible |
| d5 | `20260930-103846385-41140180f8fa4b7fa391d9ba8c6260a1` | completed | passed / complete / ineligible |
| main-aa1 | `20260930-094317816-5901d683a67541b4b186d53405cdb128` | completed | passed / complete / ineligible |
| main-aa2 | `20260930-095602990-cbfd87824f674b5da2ac62faa5589f61` | completed | passed / complete / ineligible |
| off1 | `20260930-103601681-b66b598609c64a7585a681d44682867c` | completed | passed / complete / eligible |
| off2 | `20260930-103855885-ce2ede4057de488ca0c4edda5a4c8bd9` | completed | passed / complete / ineligible |
| w1 | `20260930-102410994-f93b071605734724a8a39ad501582fa8` | completed | passed / complete / eligible |
| w2 | `20260930-102727381-805862b9c2ec4c46a7449d919da8604f` | completed | passed / complete / eligible |
| w3 | `20260930-104035873-7afda96492dc450bbbcce5ff731ae679` | completed | passed / complete / eligible |

`summary.json` preserves runner analysis and derived counter totals, always
with `productionAcceptance=false`. `runs/` contains original focused metadata,
metrics, frame/counter logs, state ledgers and screenshots. Cache binaries,
diagnostic ZIP duplicates and large guest assets are excluded. Full eligible
runner collections remain locally retained; the published manifest describes
only these selected files. `SHA256SUMS` covers all retained evidence; regenerate
and validate with `python3 audit.py` and `sha256sum -c SHA256SUMS` in this folder.

Agent/model: Codex (GPT-6). Read-only review: Codex subagent using the inherited
model. These records support diagnostic research only.
