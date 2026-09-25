# Shader worker scheduling — optional research, not a prerequisite

The active work is [one learned-fallback readiness repair](../../performance/issue-198-ubershader-loading.md) under #198/#203. Keep current worker count and scheduling for that candidate. Reuse bounded queues and current publication ownership.

[The previous scheduling addendum is preserved here](https://github.com/Mainkill1/xemu/blob/6bbd8316c6ff12639c5147315e5fe3738a55bcf4/docs/superpowers/plans/2026-09-24-vulkan-worker-priority-addendum.md).

#217/#222/#226 contain useful experiments, but no normal/low-priority matrix, demand-promotion implementation, deadline scheduler or new pool is required before testing the focused candidate. Existing review findings remain attached to the corresponding revisions; they are not a mandatory backlog for the active repair.

Revisit scheduling only if a naturally occurring queued-work delay materially prevents known fallback pipelines from becoming ready before demand. Distinguish actual queued promotion from an already-active compiler operation. Do not manufacture a workload delay and call removing it a native performance improvement.
