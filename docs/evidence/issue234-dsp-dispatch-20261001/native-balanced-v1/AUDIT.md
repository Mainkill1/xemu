# Native comparison audit procedure

Run the retained `summarize-native-balanced.py CAMPAIGN --out NEW_SUMMARY_JSON` after maintained HTTP collection places all 12 archived directories under `CAMPAIGN/collected`. It reads evidence only; it does not contact the tester, schedule attempts, alter outcomes or rewrite an existing summary.

The audit checks frozen physical role/order and unique build hashes, all expected attempts, canonical eligibility, executable identities, executed-versus-frozen jobs, common fixed inputs/host/profile/storage identities, private stopped-target Mesa qualification, complete runner reports, and hashes of raw measurement sources. Missing or failed records remain explicit and prevent a percentage claim. AAAA controls stay separate from the ABBA/BAAB estimates; the combined estimate contains only balanced attempts. Medians and ranges are per-attempt values; host samples are not pooled.

CPU core-percent is a diagnostic under fixed observation time, not fixed-work cost. The frame and flip logs count `NV_PGRAPH_INCREMENT_READ_3D` control events with host realtime-clock timestamps (see `pgraph_control_write` and `nv2a_profile_log_increment`). Report **flip-control cadence/intervals**, not rendered FPS. Guest progress, host cost, audio and actual scene equivalence still need their own checks.

Before collection, a local dry audit listed all 12 planned attempts as not collected, incomplete and noncomparable, with no percentages. A separate read-only check used the already retained #229 eight-attempt corpus: all per-attempt values and canonical outcomes reproduced exactly, and its incomplete frame report blocked every percentage. Existing output refusal preserved its prior hash. These are evidence-tool checks, not additional native #234 results or a repaired #229 campaign.

This recipe extends the existing #229 audit, with explicit control separation, physical role checking and source-byte verification. Full collection/packaging and exact screenshot review are still required; this script does not substitute for them or promote the PR.
