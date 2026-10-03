# Runner XISO fixes and archived evidence preservation

## Decision

Maintenance qualified on Steam Deck `10.0.0.123`. Resume performance research using runner `55a360daf82a699ecd12d69d8484f32bf1cf3752`. **Performance improvement: N/A.** No new emulator measurement ran during this maintenance; the previous 40 attempts retain runner `c264004` and their original values. Diagnostic xemu PR #299 remains draft/HOLD.

## What changed

Upstream runner main `fc698b1da4a6beef2e2f6b9e60c2765956fcccde` includes [#96](https://github.com/Mainkill1/Xemu-Test-Runner/pull/96), which stops campaign materialization after setup failure, and [#97](https://github.com/Mainkill1/Xemu-Test-Runner/pull/97), which requires an explicit suite/application configuration source when both inputs declare configuration. Application-selected settings are pinned by SHA-256 per attempt. These changes were incorporated into the maintained standalone-process branch [draft #89](https://github.com/Mainkill1/Xemu-Test-Runner/pull/89).

The initial integrated upgrade `2c4254d` exposed a compatibility failure: deserializing an old eight-field request into the new record added a null `ConfigurationSource` to its canonical hash. An already completed A/A campaign became unreadable. [Draft runner #98](https://github.com/Mainkill1/Xemu-Test-Runner/pull/98) adds an exact old-shape match only for unspecified sources. Current-shape hashing remains accepted. Plan hashes, explicit source choices, selections and authorization remain checked; stored evidence is not rewritten.

## Checks

| Check | Observed result |
| --- | --- |
| Retained HTTP regression before fix | Failed with the native archive identity error; 25/26 XISO checks passed |
| XISO checks after fix | 26/26; independent fix branch also 26/26 |
| Eleven managed check suites | 290/290 |
| Python XISO client checks | 10/10 |
| Verified self-contained Linux payload | 59 files; exact deployed source identity |
| Original configuration and metadata | All 1,091 backed-up files byte unchanged |
| Five previous corrected CPU campaigns | All plans, terminal statuses and reports identical; 40 native attempts preserved |
| Existing archive | 633 native entries; no new measurement attempts |
| Live ambiguous declared configuration | Rejected with `xiso_configuration_source_required` |
| Live explicit suite/application choices | Source and per-attempt configuration hash frozen; no execution authorized |
| Application-selected hash | `aa356a9d7b916ace9b71ff1ef47946db1ac289e36a8b00235c4397f57523b0b8` |

The legacy regression uses the actual HTTP archive reader and idempotent creation. It verifies unchanged stored bytes, rejects a changed selection and explicit source on the old identity, and rejects a corrupt request hash. Native preservation was checked over maintained LAN HTTP. SSH was used only for the operator upgrade, payload verification and independent metadata backup/preservation checks.

## Failures and interruptions retained

The first live archive check failed and led to the retained regression/fix. The regression first needed a missing test namespace import corrected before reproducing the intended assertion failure. An initial local publish supplied an incorrect informational revision; it was rebuilt from `git rev-parse HEAD` and the exact identity verified before packaging or deployment. A control payload upload overlapped the operator restart and received a connection refusal; it resumed with the same immutable upload identity after the runner returned. No guest test was started, silently repeated or excluded. Full relevant logs and HTTP receipts are in the evidence archive.

The application used for the first live selection control did not declare `-config_path`, so omission was correctly accepted with the suite configuration. A separate container explicitly declared it; that control was rejected on omission and accepted with each explicit choice. None of these frozen plans was started.

## Limits and next action

The configuration controls prove refusal, source selection and hash publication on the native server. Actual application-configuration injection is covered by the retained managed HTTP integration fixture. They establish no emulator speedup, graphics correctness, native setup-failure injection or new Mesa-cache result. The original Mesa qualification remains attached to the original measurement identities. Subsequent XISO work explicitly selects its configuration authority; settings experiments use application source, while binary-only experiments use the same pinned suite settings for both builds.

Resume the bounded miss-only eight-entry experiment for issue #167 with lifetime/invalidation correctness controls, then matched uninstrumented A/A and physical ABBA/BAAB. No production victim candidate or speedup is claimed here. No PR was merged.
