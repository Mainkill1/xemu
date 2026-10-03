# #246: unchanged Morrowind repeat and PGR2 throttle attempt

**Decision: HOLD.** The Morrowind PFIFO abort did not reproduce on one unchanged
collector-ON repeat. The original abort remains unclassified. Both new finalized
allocation windows have zero destroys or reuse opportunities. The attempted
PGR2 throttle extension does not qualify driving or transition coverage.

## Controlled identity and preserved procedures

Steam Deck **10.0.0.123** only, Vulkan/full DSP/JIT, HTTP runner
`848dca74e1ff79f9fc886769a785c23a0945a87e`, cold private Mesa/application
cache and guest state, no VM restore. Both private Mesa qualifications passed
without waivers. OS/page and driver memory caches remain uncontrolled.

Both runs used runtime **8079502ae25a602e1395fa9ee87ec1ef8b57c9d6**,
executable SHA-256
`1b52adf0dea0823a95c37ae79d447af01bf8a904d6385037908b2ca57d44bba2`,
ELF build ID `d584912b97c1e515f9348cb57e99747196005e65`, pinned unchanged
parent library bundle. Source/runtime is unchanged by this evidence publication.
These are diagnostic procedures, not matched A/B speed measurements.

| Procedure | Run ID | Original outcome | Actual coverage |
|---|---|---|---|
| Unchanged Morrowind collector-ON repeat | `20261002-065419840-3bfbca7e36a34b0e945412cf3724f229` | Completed; correctness/evidence passed; full allocation/schema-8 readers passed | Initial ship interior/NPC behind name entry; no streaming |
| PGR2 bounded throttle extension | `20261002-065934927-b3b606aa22ca49b293d4fde0fb92959c` | Completed; original generic checks passed; full readers passed | Throttle HUD response, little scene progress, no requested pause menu; **not qualified** for driving/transitions |

The Morrowind procedure was cloned from the failed unprofiled collector-ON job
with no binary, plan, asset or execution-contract changes. Full saved definitions
were retrieved through their definition URLs and compared after removing attempt
IDs, source-job identity and description. `procedure-equivalence.json` retains
that check; both frozen definitions are archived. A single repeat does not prove
collector safety or attribute the earlier abort to a particular cause.

The earlier PFIFO failure had no exported core/registers/DMA object values. Its
systemd frame addresses were symbolized locally using the exact debug build;
return/inlined locations are retained in `prior-abort-symbolization.json` and do
not constitute a root-cause diagnosis. No PFIFO code or assertion was changed.

PGR2 extends the existing saved loading/stationary-grid procedure with three
15-second `RTrigger` holds, captures after each hold, then `Start`, a two-second
wait and a requested pause-menu capture. The original early plan, binary,
settings/assets and diagnostic contract remain unchanged. The extended procedure
is baked as `issue246-deck-collector-pgr2-throttle-v1`; its immutable definition
and preparation script remain available for later qualification.

## Allocation results

| Window / allocation class | Creates | Total host elapsed time | Maximum single create | Destroys | Post-destruction reuse opportunities |
|---|---:|---:|---:|---:|---:|
| Morrowind eligible ordinary | 139 | 1.958 ms | 0.051 ms | 0 | 0 |
| Morrowind surface copies, excluded | 9 | 0.288 ms | 0.049 ms | 0 | Excluded |
| PGR2 eligible ordinary | 792 | 12.102 ms | 0.067 ms | 0 | 0 |
| PGR2 surface copies, excluded | 19 | 57.154 ms | 56.801 ms | 0 | Excluded |

Durations bracket actual VMA calls and are **host elapsed time**, not CPU time,
whole-probe overhead, frame time or improvement percentages. Disjoint classes
sum to each validated trace total. Copy images are excluded from pool analysis.
All images remain live at an idle shutdown checkpoint; later destruction is
unobserved. Equal live configurations are not reuse hits.

| Original finalized allocation log | Records / live images | Live allocation bytes | Original JSONL SHA-256 |
|---|---:|---:|---|
| Morrowind | 148 | 33,619,968 | `51a55e1b16dd202f12f0f98ba6e03c03c634416ab11d17e2846ce85a7fd891a3` |
| PGR2 | 811 | 63,151,104 | `c36ed26927bbca14501377f571a1a1afc11f8fa0ad9b456ba3568c1829a20abf` |

Both report `end_reason=shutdown_checkpoint`, no dropped events/clock errors,
and complete observed accounting. These are traced bytes, not total VRAM use or
pressure/budget measurements.

## PGR2 coverage rejection

Direct review of three captures shows **3 mph** after the first hold and **14
mph** after the final hold. The scene remains near the starting grid. The
requested pause-menu capture still shows the race scene. Therefore this does not
establish a driven race, sustained streaming or a menu/race transition, despite
passing the original generic visibility/artifact contract.

The final 20 NV2A frame-log event intervals are **1,481.409–2,349.073 ms**, mean
**1,774.507 ms**. These are host elapsed intervals between logged emulated frame
events, not display presentation timestamps or guest clock durations. Morrowind's
final 20 intervals are 32.985–49.978 ms, mean 34.170 ms. Neither is an A/B result.

Schema-8 PGR2 telemetry has 3,700 frame records over a 212,779.785 ms timestamp
span. Its maximum recorded per-frame `pipeline_prepare` total is 1,636.333 ms
at frame 3,551. `draw_flush` reaches 1,690.253 ms in that frame; these regions are
nested and must not be added. This is an observed stall requiring attribution,
not proof that allocation collection or pipeline creation caused it. No profiler,
new timeout tolerance, clock suspension or guest-time modification was added.

## Raw records and reproduction

[records.tar.gz](records.tar.gz) includes complete original assessments, state
qualification reports, allocation/schema-8 traces, frame logs, captures, recipes,
collector summaries, class totals and source/build identities. The runner's
full collections contain 443 and 879 files respectively, excluded count zero.
State/cache payloads are explicitly omitted from the publication archive with
their hashes in [records-inventory.json.gz](records-inventory.json.gz); the
original files remain local/server-side. No disc/firmware/executable payloads.

After extraction, rerun the maintained reader from runtime `8079502ae2` or this
documentation-only publication:

```sh
python3 scripts/performance/vk-texture-allocation-summary.py \
  morrowind-on-repeat/collected/20261002-065419840-3bfbca7e36a34b0e945412cf3724f229/vk-texture-allocations.jsonl
python3 scripts/performance/vk-texture-allocation-summary.py \
  pgr2-throttle-attempt/collected/20261002-065934927-b3b606aa22ca49b293d4fde0fb92959c/vk-texture-allocations.jsonl
```

[verification.json](verification.json) records the archive hash/size, payload
reverification count and unchanged original outcomes.

## Decision and next gates

No retained-image candidate, accepted speedup, A/A, ABBA/BAAB, default-off or
same-executable OFF/ON overhead qualification, or affected/control per-leaf XISO
timing acceptance exists. Texture-cache capacity is 1,024; none of these windows
demonstrates eviction. Allocation cost is negligible in their observed ordinary
paths. Resolve stalled PGR2 progress and establish real repeating churn before
considering a pool; preserve the unresolved intermittent Morrowind abort.
Transitions, another texture-heavy title, eviction/in-flight reasons, upload
bytes and heap pressure remain missing. #246 and draft #293 remain open/HOLD.

See the [previous native packet](../issue246-collector-native-20261002/README.md)
for the original failure, parent/OFF controls and accepted stationary PGR2
window. Original outcomes are never retroactively repaired by this repeat.
