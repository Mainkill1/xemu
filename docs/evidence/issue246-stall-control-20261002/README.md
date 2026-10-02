# #246: collector-OFF stall control and short CPU profile

**Decision: HOLD.** PGR2's near-grid slowdown also occurs with allocation
collection disabled. A short CPU profile points toward voice processing and
sample-memory lookup as worthwhile attribution targets. Neither diagnostic
qualifies driving/transitions, proves the slowdown's cause, or demonstrates a
performance gain from an optimization.

## Identical executable and controlled procedures

Steam Deck **10.0.0.123**, Vulkan/full DSP/JIT, HTTP runner
`848dca74e1ff79f9fc886769a785c23a0945a87e`. Runtime:
`8079502ae25a602e1395fa9ee87ec1ef8b57c9d6`; executable SHA-256:
`1b52adf0dea0823a95c37ae79d447af01bf8a904d6385037908b2ca57d44bba2`;
build ID `d584912b97c1e515f9348cb57e99747196005e65`.
Both use pinned parent libraries, firmware/discs and private guest state, cold
Mesa/application caches, no VM restore. Both Mesa qualifications pass without
waivers. OS/page and driver memory caches remain uncontrolled.

The OFF control preserves the previous ON procedure's binary, dependency files,
navigation, three 15-second throttle holds, screenshots and schema-8 telemetry.
Only allocation collection and its post-gameplay validation/output contract are
removed. The inactive reader payload remains to keep package bytes identical.
`control-contract.json`, manifests and reusable baked tests retain this boundary.

The profile procedure extends that OFF control with before/after `ps -T` thread
observations and a **five-second, 99 Hz, frame-pointer** perf recording after the
existing gameplay captures. It uses the maintained perf adapter, followed by
another screenshot. No profiler timeout, guest clock or tolerance was changed.
This instrumentation makes it an ownership diagnostic, not clean timing evidence.

| Procedure | Run ID | Original outcome | Direct scene qualification |
|---|---|---|---|
| Same-executable allocation OFF | `20261002-071800687-e136dd0995744f5c8932e1eb209b9a43` | Completed; generic correctness/evidence passed; full schema-8 reader passed | Throttle capture 10 mph; requested pause capture still race scene, 11 mph; **driving/transitions rejected** |
| OFF with short profile | `20261002-072654622-a1fc8fb51e57422185da95af1dfaab76` | Completed; planned diagnostics and reader passed | Post-profile capture shows 14 mph near grid; **driving/transitions rejected** |

All original outcomes remain unchanged. Full artifact collections contain 873
and 894 files respectively, complete with excluded count zero.

## Stall observations, not an overhead percentage

| Diagnostic | Final 20 NV2A frame-log intervals: mean / min / max (ms) | Full schema-8 frame records |
|---|---:|---:|
| Earlier collector ON throttle | 1,774.507 / 1,481.409 / 2,349.073 | 3,700 |
| Collector OFF throttle | 2,263.900 / 1,411.061 / 3,047.374 | 3,664 |
| OFF profile procedure, including profiling | 2,152.961 / 1,255.314 / 3,240.064 | See original summary |

These are host elapsed intervals between logged emulated frame events, not
display presentation timestamps or guest clock time. One ON/OFF pair with
stalled guest progress is **not** A/A or ABBA/BAAB overhead qualification; no
improvement percentage is calculated. It demonstrates that the symptom can
occur without allocation collection, not that the collector is free of cost.

In the final 20 OFF telemetry records, `draw_flush` totals 561.172 ms,
`pipeline_prepare` 287.532 ms, and nested `bind_textures` 75.409 ms. Do not add
nested regions. These do not account for the roughly 45 seconds covered by the
last 20 frame-log intervals; full causal attribution is still missing. No
wall-clock mapping of plan segments to guest timestamps is invented.

Source inspection shows `pipeline_prepare` brackets `create_pipeline()`, which
includes texture binding, shader preparation and pipeline lookup/creation. A
large elapsed value does not isolate shader or pipeline compilation.

## Native CPU profile

The retained `cycles:Pu` capture has **1,755 samples**, weighted period
**24,364,103,879**, and reports zero lost samples. Offline mapping uses the
exact executable/debug build and pinned parent library paths with remote
debug downloads disabled. The recorded `libsamplerate` build ID matches the
local pinned file: `694779a0c93a3b5401c245c544391807783a7d03`.
Native exports, commands, build IDs, errors and full weighted leaf/thread
tables are retained under `pgr2-stall-profile/mapped/`.

| Observed native work | Share of sampled process userspace cycles | Meaning / limitation |
|---|---:|---|
| All `libsamplerate.so.0` leaves | 35.16% | Includes 34.92% in stripped internal symbols; not a proposed resampler quality change |
| `address_space_ldl_internal` on all threads | 10.69% | Generic physical word-read helper; not all reads are sample payload |
| Same word-read helper on observed voice-worker threads | 9.31% | Threads also sampled native `voice_worker_thread`; ADPCM/PCM and descriptor/payload split unresolved |
| `voice_resample_callback` | 4.75% | Separate leaf samples, not inclusive time |
| Anonymous `/tmp/perf-53956.map` samples | 18.56% | Unresolved generated-code bodies; not credited to a particular CPU/DSP instruction |

Eight TIDs (54005–54012) have native voice-worker loop samples. TID 54018 has
native DSP-run/helper samples. Before/after `ps` confirms these TIDs were present,
but many OS thread names are simply `xemu`; cumulative `ps %CPU` is not the
five-second profile's utilization. The native symbols narrow ownership without
inventing names for unresolved JIT bodies. A frame-pointer profile does not
establish complete caller attribution.

This makes [#197](https://github.com/Mainkill1/xemu/issues/197)'s bounded
sample-memory lookup experiment worth investigating further. It does **not**
validate [draft #275](https://github.com/Mainkill1/xemu/pull/275), predict its
gain, prove ADPCM activity or explain a particular long frame. Byte-level audio
parity, affected XISO coverage and matched native performance tests remain needed.

## Raw evidence

[records.tar.gz](records.tar.gz): **13,246,997 bytes**, SHA-256
`c107d2ffcfb80e609e4fab7d9674fc2c8dab3198d9788dc9e260db12664f5da7`.
Packaging rehashed **162 payloads**; **1,655 state/cache payloads** are explicitly
listed as omitted in [records-inventory.json.gz](records-inventory.json.gz).
Original cache files remain local/server-side. Local symbol symlinks are omitted;
no firmware/disc/executable payloads are published.

The archive contains original assessments, cache qualifications, captures,
schema-8/frame logs, profiler data/reports, frozen preparation and saved-test
recipes, identity records and exact offline exports. The offline mapper's first
parser assumed separated PID/TID fields; the actual export uses `PID/TID`.
That parser was corrected and all original exported leaf records were consumed;
no capture, outcome or sample was repaired or dropped.

[verification.json](verification.json) binds the archive and unchanged outcomes.
This publishing commit changes documentation/evidence only; runtime/scripts/tests
remain byte-equivalent to tested runtime `8079502ae2`.

## Remaining work

No pool is implemented. The earlier finalized allocation windows still show zero
destroys/reuse opportunities, and these OFF procedures add no allocation evidence.
Driving/transition and third-title coverage, collector overhead, A/A, ABBA/BAAB,
per-leaf XISO correctness/timings, eviction/in-flight reasons, upload bytes and
heap pressure remain unqualified. The earlier intermittent Morrowind PFIFO
abort stays unclassified. Draft #293 remains HOLD; issue #246 remains open.

See [the previous repeat/coverage report](../issue246-collector-followup-20261002/README.md)
and [earlier native controls/failures](../issue246-collector-native-20261002/README.md).
