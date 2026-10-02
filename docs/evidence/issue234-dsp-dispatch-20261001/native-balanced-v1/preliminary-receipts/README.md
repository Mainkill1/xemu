# Preliminary runner receipts — campaign incomplete

**Historical checkpoint, superseded by the [complete campaign report](../README.md).**
All twelve attempts are now collected and audited. The final report classifies
the shutdown failures and retains these preliminary receipts unchanged.

Steam Deck `10.0.0.123`; the unchanged frozen plan contains four AAAA
controls followed by ABBA and BAAB. These are the saved reports from the
first seven terminal attempts. They are retained here because the second
candidate attempt failed execution. The remaining five attempts have not
been collected or evaluated at this checkpoint.

| Physical attempt | Build | Mean process CPU (core %) | Mean flip-control interval (ms) | Positive intervals | Canonical outcome |
|---|---|---:|---:|---:|---|
| ABBA 1 | Parent A | 200.29 | 1271.34 | 236 | Completed / passed / complete / eligible |
| ABBA 2 | Candidate B | 193.10 | 1797.58 | 167 | Completed / passed / complete / eligible |
| ABBA 3 | Candidate B | 193.74 | 1527.34 | 197 | **Failed / passed / complete / ineligible** |

The mean process CPU is diagnostic under a fixed wall observation; it is
not a fixed-work cost. Flip-control events are
`NV_PGRAPH_INCREMENT_READ_3D` accesses measured on the host clock, not
independently verified rendered frames. The first candidate observation
combines lower CPU with slower control-event progression. This does not
establish a speedup, and no balanced improvement estimate is reported.

The failure is not yet classified. Its bounded crash brief reports
`crashed: false`, no fatal code, and unavailable dump/analysis. That is
insufficient to explain the failed execution or to attribute it to the
candidate. The attempt remains ineligible, without retry or outcome repair.

Only the cached `/performance`, result-summary and diagnostics-summary
routes were successfully read while the next attempt ran. An attempted
`assessment.json` download received HTTP 409 under the next job's benchmark
policy; no artifact payload was transferred. Raw collection is deferred
until all twelve attempts finish and the tester is idle.

`observations.json` preserves all seven cached reports' identities and
outcomes. `manifest.json` hashes the included payloads; it verifies these
receipts, **not the raw metric sources named inside them**. Raw source-byte
checks, executed-job/seed/cache checks and scene screenshots remain pending.
The AAAA controls remain separate from any eventual A/B estimate. The
retained audit must keep this failure visible and withhold estimates for
an incomplete or ineligible order.
