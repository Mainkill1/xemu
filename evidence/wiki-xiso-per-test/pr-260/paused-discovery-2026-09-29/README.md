# Paused discovery watchdog

Live discovery on the Windows diagnostic build stopped while the guest was deliberately paused before a short animation. The watchdog measured host time even though guest frames could not advance.

The capture owner now receives guest pause state. Paused ticks still publish completed owned inputs; resume grants a full acquisition timeout without changing the request generation. Renderer reset, freeze, selection and cancellation guards remain active.

## Verification

- The deterministic pause/resume case failed before the correction (`paused-watchdog-red.log`).
- 28 controller cases passed with `-Wall -Wextra -Werror`, including paused publication and the original running-state watchdog.
- The same 28 cases passed ASan/UBSan; leak checking is disabled under the traced host.
- Actual browser, platform and live-capture translation units compiled with `-Werror`; the actual UI fixture passed.
- Reviewed capture lifetime, cancellation and ownership integration without blocking findings.

These focused results do not establish native qualification of a later CI artifact. No game bytes, generated game shader sources or private screenshots are included.
