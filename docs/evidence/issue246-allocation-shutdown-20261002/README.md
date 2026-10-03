# Issue #246: preserve the failed native trace and correct shutdown ownership

## Decision: HOLD

The first native collector run on Steam Deck `10.0.0.123` completed, qualified
private Mesa disk-cache writes and reached Morrowind's initial rendered ship
interior behind name-entry UI. Its original runner contract checked a live
schema header, so it passed. **Full post-exit allocation validation failed:
`Missing final lifecycle summary`.** The original outcome is preserved; it is
not qualified allocation evidence or a performance comparison.

| Check | Tested revision | Result |
|---|---|---|
| Native collector v1 | `0ef8d9bf5c8dbf9b09895827ea615a3a789a82c0` | Original diagnostic passed; finalized allocation reader **failed**. |
| v1 Linux release build / CI unit job | Same | Passed; 137 unit tests, 16 skips, additional required GL depth check. |
| Local shutdown correction | Tested source hashes below | Product build and 27 focused C subtests passed, including 9 collector cases. |
| Allocation reader / retained schema-8 reader | Local correction | 11 / 11 checks passed. |
| Actual C checkpoint writer → reader | Local correction; fake VMA boundary | Reports one live image, 123,456 live bytes, zero destroy calls. Not native timing. |
| Corrected native shutdown, observer cost, matched XISO timings | Pending | No improvement percentage or pool acceptance. |

The earlier local full-unit build failure in the unchanged resampler remains
recorded. CI's older GCC accepts those declarations with warnings; its success
does not rewrite the local failure or exact-parent reproduction.

## Root cause and correction

`qemu_cleanup()` calls `vm_shutdown()` but does not unrealize all devices; its
root-container cleanup remains a TODO. Therefore `nv2a_exitfn()` and Vulkan
renderer finalization are not reached on this ordinary process-exit path.
Closing only after texture finalization could not produce a footer.

`nv2a_vm_state_change(RUN_STATE_SHUTDOWN)` already waits for FIFO idle through
`nv2a_lock_fifo()` and holds both PFIFO and PGRAPH locks while calling the
renderer pre-shutdown trigger. The correction closes and detaches the collector
at that existing quiescent handoff. It adds no lock, VMA destruction, retry,
clock manipulation or resource-ownership change.

```text
BEFORE: ordinary exit -> idle shutdown callback -> skip NV2A unrealize
        -> collector's renderer-finalize hook never runs -> missing footer
AFTER:  ordinary exit -> existing idle PFIFO/PGRAPH handoff
        -> shutdown-checkpoint footer -> detach collector
        -> original cleanup continues; resource ownership unchanged
```

Schema 2 labels the end reason. `shutdown_checkpoint` validates the complete
observation window and reports resources still live; later cleanup is
unobserved. `renderer_teardown` still requires balanced destruction. The reader
rejects unknown reasons, mixed checkpoint/teardown records, missing footers,
caps, errors and inconsistent totals. A checkpoint is not proof that shutdown
destroyed resources or a measurement of their eventual destruction cost.

The new reader regression failed on the original live-image rejection. The C
regression initially lacked its checkpoint API. Review found an outdated
schema-1 C expectation; that failure was reproduced, retained and corrected.
The nine complete C cases then passed. Review found no further concurrency or
ownership defect; native behavior and overhead were outside its scope.

## Exact native identity

- Saved test: `issue246-deck-collector-morrowind-newgame-v1`, revision
  `9ea253ee8c7c4b7a509fe2dab42ab71e581d137fccec89fb06dfc7b0fe4c3885`.
- Run: `20261002-054517618-93365480ae4647ad81820e2306e55b01`.
- Executable SHA-256:
  `60db782d3d0db6af8533c740ae1a255ffeaf4b8bb6d6c3c572f815fdfb5a6e1a`;
  ELF build ID: `95c41a774a886a16b327686ba6960c6d27737f6c`.
- Linux release CI run `36969935133`, job `110721900816`; debug SHA
  `2c624fe6fbb68b8e931a77a57236a4b8aa59fdbaf82c65fda6d9bc7bbd74415a`.
- Parent and candidate debug compiler metadata match: Clang/LLD 21.1.8,
  Rust 1.96.0, GCC 12.3.0. Candidate packaged GLib differs, so this diagnostic
  deliberately uses the unchanged pinned parent runtime libraries.
- Vulkan, full DSP/JIT, 128 MiB guest; pinned private HDD/EEPROM, cold private
  Mesa/application cache. Runner `848dca74`; three declared 30-second profiles,
  no manual intervention. OS page cache and driver memory cache uncontrolled.

Two post-intro captures were reviewed directly. They show the same stationary
initial interior with modal UI; movement/streaming/transitions remain
unqualified. Three profiler recordings are mapped to the exact candidate build
ID and debug image. The existing schema-8 log validates independently; none of
these checks cures the missing allocation footer.

## Evidence and remaining work

[records.tar.gz](records.tar.gz): 163 payload files, 28,396,221 bytes;
SHA-256 `dbb5600e1bd35d05503e6c6be2896b3c7105ef3bb6561cfee37088ae7e9d49ed`.
[records-inventory.json.gz](records-inventory.json.gz) preserves byte hashes and
explicitly lists 394 omitted cache payloads. All 476 eligible native artifacts
(148,621,387 bytes) were collected before packaging. Original JSONL, screenshots,
profiles, outcomes, immutable recipe, deployed reader and reproduced reader
failure remain intact. No binary or game media is published.

[verification.json](verification.json) records scope and collection receipt;
[tested-source-inventory.json](tested-source-inventory.json) binds the local
correction to exact source bytes. Publishing this packet does not qualify the
correction's native behavior. Run a new immutable corrected recipe and validate
its footer, then measure same-executable OFF/ON and matched parent/candidate
default-off costs with A/A and balanced ABBA/BAAB. Required XISO leaf timings,
representative churn, pressure/in-flight attribution and safe pooling remain
pending. [Usage and coverage semantics](../../performance/vulkan-texture-allocation-trace.md).
