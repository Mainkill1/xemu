# Fresh review: b4c566 →6caee6

One read-only reviewer, isolated task context. No Critical findings.

| Grade | Finding | Decision |
|---|---|---|
| Important | Signed inspection NDC depth fails retained non-perspective PS clipping. | Accept; normalize window-depth bookkeeping, native generated clipping regression. |
| Important | Actual GL generated epilogue rejected by placement adapter. | Accept; guarded GL epilogue + depth conversion, actual-source-shape regression. |
| Important | Emission-sorted members replace chosen anchor after save/reopen/UI. | Accept; explicit version2 anchor, legacy1 support, non-first anchor roundtrip. |
| Important | Duplicate null/undersized texture image uploaded after validating first matching image. | Accept; reject duplicate/out-of-range entries and upload validated set only. |
| Important | Legal GL maximum texture level1000 rejected for single-level linear filter. | Accept; derive equivalent effective range, preserve missing mip-chain rejection. |
| Minor | Cached mesh ignores active stream count. | Accept; include count/offset/slot metadata, changed-count regression. |

No findings deferred. Native Windows car fidelity/cadence, actual-head CI and
stacked draft259 remain separate pending gates. Runtime failures are retained;
compile setup errors and mixed-output attempts do not qualify as runtime evidence.
