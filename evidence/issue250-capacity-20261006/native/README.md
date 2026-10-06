# First PR328 native capacity diagnostic

Reference source e15b180 tree-equivalent executable197654; candidate31159c52c6. Exact identities and unchanged saved procedure a7427cec are in wait-comparison.json. Same Vulkan settings, controller, inputs, delays and measurement boundary. Both recordings begin in the Hong Kong flyover and end parked000MPH. Manual inspection rejects stationary A/B eligibility despite runner admission. Single pair, uncontrolled driver cache, diagnostics enabled; no stable performance claim.

| Metric | Reference | Candidate |
|---|---:|---:|
| Telemetry frames |236|223|
| Descriptor capacity requests |236|0|
| Descriptor writes/frame |1762.21|1762.19|
| NEED_BUFFER_SPACE wait/frame |16.193ms|5.169ms|
| SURFACE_DOWN wait/frame |0ms|9.199ms|
| Auxiliary surface-download wait/frame |4.396ms|2.393ms|
| Total queue submits/frame |11.195|10.000|
| Guest flip cadence/s |9.138|6.812|
| Process CPU, one core=100% |374.98%|381.08%|
| Mean/median interval |105.935/68.370ms|112.064/68.599ms|
| p95/p99 interval |255.652/385.857ms|250.987/309.778ms|
| Maximum interval |417.305ms|552.233ms|

The target capacity limit is removed, but waits become visible at a different boundary and no end-to-end benefit is established. Descriptor-update elapsed time/frame falls9.636->1.350ms while texture-binding elapsed grows7.536->14.792ms; these nested elapsed intervals include waits and cannot be summed as exclusive CPU. SURFACE_DOWN reason is shared by readback and invalidation, so its callsite must be identified before attributing it to one operation. This test does not contain #290 alias retirement.

Keep HOLD. Next engineering gate is capacity/readback finish-callsite attribution and whether a real CPU consumer requires the new wait. Do not launch a broad unchanged campaign or remove a synchronization boundary without ownership proof. PGR2 start-scene drift is retained, not repaired by modifying the frozen procedure. Required full XISO timings, native lifecycle/memory and balanced acceptance remain incomplete.
