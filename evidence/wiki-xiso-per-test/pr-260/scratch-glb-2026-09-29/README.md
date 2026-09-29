# Default scratch GLB

The Asset Browser exports the selected assembly to `SDL_GetPrefPath("xemu", "xemu")/asset-browser/scratch.glb` without requiring a filename. Update scratch GLB replaces that file only after a successful staged export. Save GLB as... exports a separately named retained file and refuses an existing destination.

## Verification

- Replacing an existing scratch failed before the correction (`scratch-glb-red.log`).
- Four export cases passed with strict warnings and ASan/UBSan. The scratch case verifies changed GLB content, cancellation and validation failure preserving the previous bytes, and normal retained export refusing overwrite. Leak checking is disabled under the traced host.
- Sanitized compilation uses `-O0`: GCC 14's standard regex library reports `-Wmaybe-uninitialized` with this fixture under `-O1 -Werror`; the initial build did not execute tests. The retained sanitized verification log is the completed `-O0` run.
- Actual UI fixture passed. Actual browser/platform/live translation units compiled with `-Werror`.
- Windows GLB translation unit, including `MoveFileExW` replacement, cross-compiled with the MXE x86_64 compiler and `-Werror`. The empty Windows compile log corresponds to successful exit with no diagnostics; native execution remains a separate gate.
- Reviewed staged publication, cancellation and retained-file behavior without blocking findings.

The fixture uses authored synthetic inputs. No game bytes or private screenshots are included.
