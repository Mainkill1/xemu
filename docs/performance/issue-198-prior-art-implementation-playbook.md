# Shader prior art — reference archive

**The active delivery plan is [one main-based learned-fallback readiness repair](issue-198-ubershader-loading.md).** The former playbook is no longer an instruction to implement every mechanism or its A-H chain.

[Full prior-art review, code sketches and source references preserved at the reviewed revision](https://github.com/Mainkill1/xemu/blob/6bbd8316c6ff12639c5147315e5fe3738a55bcf4/docs/performance/issue-198-prior-art-implementation-playbook.md).

| Research | Owning issue | Reopen when |
| --- | --- | --- |
| Compile-required probes / Cemu-style omission | #204 | The separately scoped nonblocking policy needs it; not necessary to replay a known fallback pipeline in the existing worker. |
| Graphics pipeline libraries / fast link / optimized replacement | #205 | Measured monolithic pipeline construction remains the dominant cost. |
| Key entropy and early partial identities | #206, #174 | Repeated semantically equivalent compiler work is demonstrated. |
| Off-thread source generation and worker scheduling | #207 | Foreground generation or queued-work delay materially affects the targeted loading interval. |
| Hot-ready retention, module identifiers, binaries, recipe replay | #208, #97 | Measured eviction or reconstruction cost justifies another cache mechanism. |

Two corrections govern use of the archive: main already has complete fallback pipeline preparation and ready-fallback/specialized selection; `FAIL_ON_PIPELINE_COMPILE_REQUIRED` forbids compilation, not every driver allocation, locking or cache delay. Neither finding justifies a new framework by itself.

Preserve and reuse the research. Do not treat a technique used by another project as evidence that xemu needs it or that it fixes #198.
