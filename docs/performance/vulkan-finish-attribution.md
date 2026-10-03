# Vulkan finish attribution

`scripts/performance/vk-perf-summary.py --finish` reads existing schema-8
`XEMU_VK_PERF_LOG` output. It introduces no emulator instrumentation or policy.
The retained CPU-region and auxiliary modes come from PR #293 at
`d1970118f30`; this branch adds finish analysis for issue #250 and Python 3.9
compatible streamed source hashing.

Use a complete, stopped diagnostic run and retain its original assessment,
source/executable/configuration identities, scene audit and cache ledger:

```sh
python3 scripts/performance/vk-perf-summary.py vk-perf.jsonl \
  --finish --tail-seconds 60 --out finish-summary.json
python3 tests/unit/test-vk-perf-summary.py
```

Output files are created exclusively: an existing report is never overwritten.
The source hash covers the complete input file. File size/mtime changes during
analysis fail. Supported Python is 3.9 or later; no external package is needed.
Meson registers the reader's checks in the unit suite.

## Interpret the values

Each of the ten original finish reasons reports calls, submissions, timing
samples and fence waits. A call without a command buffer can submit nothing.
All submitted finishes wait on the existing command-buffer fence.

The schema declares timing of the first eight **calls** per reason/control frame
and every sixteenth subsequent call. Calls that do not submit still advance the
ordinal. Hybrid tracing may cause additional submissions to be timed, so use the
recorded timing-sample counts to determine duration coverage. Do not reconstruct
timing coverage from the number of submissions alone.

Durations are measured **host elapsed milliseconds** around queue submission
and fence wait. They are neither CPU execution time nor GPU work. The fields
retain `sampled` in their names:

| Duration coverage | Meaning |
|---|---|
| `no_submissions` | No finish submissions in the reported scope. |
| `all_submissions` | Every recorded submission has a timing sample. |
| `partial` | Untimed submissions exist; accumulated duration omits them. |

The reader does not multiply sparse durations to estimate total time, add nested
CPU regions, label control-frame events as rendered FPS, predict savings or
compute a baseline/candidate improvement. `need_buffer_space` and
`vertex_buffer_dirty` are investigation targets; a reason label alone does not
prove completion can be deferred. Pending reports/readbacks and resource reuse
still require ownership analysis.

## Window boundaries and validation

All-log totals contain all recorded buckets, including the first bucket whose
start is unknown. Tail complete totals include only buckets entirely inside the
requested interval. A crossing bucket is reported separately; its individual
events cannot be located before/after the boundary. The first bucket and work
after the final record cannot supply tail coverage. Short coverage fails.
Tail storage is bounded by the time span, not a fixed record count.

Unknown/duplicate reasons, unsupported sampling/schema, incomplete records,
missing arrays, bool/negative counters, missing frame indices, backward clocks,
samples greater than submissions, submissions greater than calls, unequal
wait/submission counts and duration without a sample fail. This validates the
telemetry contract; it does not prove the run completed or its scene is suitable.
Live header-only and auxiliary/finish modes are mutually exclusive.

## Reusable native recipe

[The frozen PGR2 300-second recipe](recipes/pgr2-finish-300/README.md)
retains its input contract, configuration, navigation and failed first outcome.
It is an unpaired diagnostic; the owning PR retains raw native evidence.

The [separate 30-second host profile recipe](recipes/pgr2-host-profile-30/README.md)
retains a failed native report stage and offline attribution. It does not
replace the 300-second recipe or qualify a converter change. VP setting zero
means automatic worker selection, not zero workers.
