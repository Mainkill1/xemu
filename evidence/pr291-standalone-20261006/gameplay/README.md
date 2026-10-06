# PR 291 standalone Conker development pair

Reference current-main tree e15b180 (existing release197654 source-equivalent); candidatef58e6139d4 (CI merge2f52de70729e source-equivalent). Both use identical saved diagnostic61ef1d68 and configuration01bb08a6. No route, input, wait, measurement-boundary or baseline changes.

This is one instrumented pair with uncontrolled driver cache, not ABBA/BAAB acceptance or a stable speedup. Start/end images were privately reviewed and show the intended menu. No game images/resource bytes are included. Both report the same GLib shutdown assertion.

Per-frame counters here cover the unchanged25second frame-analysis interval. All listed waits are sampled; when timed-count is less than count, do not extrapolate an exact total. Peak staging offset uses maximum, not sum. Recorded uploads are not completion receipts. Aggregate results and identities are in wait-comparison.json.
