# Blackout implementation plan — parked

**Do not execute the former Task 1-7 / Child A-H chain as the shader-stall repair.** The owner approved switching to one focused main-based learned-fallback readiness candidate.

The active specification is [issue-198-ubershader-loading.md](../../performance/issue-198-ubershader-loading.md). It reuses main's complete fallback selector, background pipeline builder and completion wake. Optional draw omission and host-black presentation are not dependencies.

[Full prior blackout plan, preserved at its original revision](https://github.com/Mainkill1/xemu/blob/6bbd8316c6ff12639c5147315e5fe3738a55bcf4/docs/superpowers/plans/2026-09-24-vulkan-shader-miss-blackout.md).

Preserve old branches, comments and hardware failures. The all-draw omission experiment and zero-omission correction are recorded in #225; they do not establish a useful Continue-mode performance result. The owner's willingness to accept temporary missing rendering remains valid, but it is deferred rather than made part of the first accurate-path repair.

Do not spend the active candidate's budget fixing parked blackout status/UI/recovery code. Reopen this work only as a separately scoped feature with its own supported behavior and evidence.
