# Issue 197 real memory mapping validation

Goal: preserve per-word memory observations while measuring the bounded reader.

- [x] Reproduce same-physical-address RAM replacement with actual QEMU APIs.
- [x] Establish original-reader PASS and unguarded candidate FAIL.
- [x] Check current FlatView identity before cached payload reads; fall back on changes.
- [x] Retain a Linux full-emulator-object test target and fixed-work baseline/candidate modes.
- [ ] Verify full target, original control, existing unit suite and output checksums.
- [ ] Review production ownership and test validity independently.
- [ ] Publish correction and accessible evidence in draft PR275.
- [ ] Build portable CI artifact; use maintained HTTP client for Deck.123 A/A, ABBA and BAAB.

No Windows jobs, merges, register arithmetic changes, timing tolerances or old-run repairs.
Real tests cover deterministic remapping, not all concurrent register/memory activity.
Component timing cannot qualify games or replace affected ADPCM XISO/native audio checks.
