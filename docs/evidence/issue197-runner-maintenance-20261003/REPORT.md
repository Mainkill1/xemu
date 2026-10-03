# Deck runner maintenance: retained RAM fixture

## Result

The fixed runner main and retained process extension run together on Deck `10.0.0.123`. This campaign verifies the existing native component fixture and process lifecycle. **No performance comparison or new speedup is claimed.** xemu PR #275 remains draft/HOLD with its existing XISO, audible-output, PCM-control and game-variance limits.

| Check | Expected | Observed |
| --- | --- | --- |
| Existing RAM component assertions | 3 | 3 PASS |
| Execution / correctness / evidence / comparison | completed / passed / complete / eligible | all match |
| Launch arguments | `--tap`, no QMP | exact match |
| Inherited or automatic xemu diagnostic records | 0 | 0 |
| Complete eligible result collection | no exclusions | 21 files; 0 exclusions |
| Existing native hard/soft correctness outcomes | 4 preserved | unchanged run IDs/outcomes |
| Earlier negative control | timeout/incomplete/ineligible | unchanged |
| Queue after qualification | no pending/testing jobs | idle; 567 archived entries |

## Identity and applicability

Runner draft [#89](https://github.com/Mainkill1/Xemu-Test-Runner/pull/89), source `62eff1cebc7003859f58cb684bd62f21f4c07c07`, replays retained process execution onto fixed main `f34fee787a776bc9e1e344adaea932a4af846153`. Main contributes the explicit keyboard binding and measurement-start scene guards. Fresh review found stale cross-run diagnostics and ambiguous case-variant target keys; both have failing-then-passing regression tests and repairs. All [21 runner CI jobs](https://github.com/Mainkill1/Xemu-Test-Runner/pull/89/checks) pass; detailed tool checks belong to that genuine implementation draft.

The existing archived fixture payload was cloned through maintained HTTP, not rebuilt or launched through SSH. Its executable SHA-256 is `71feecec2e29001bff791fbcd1d8d90361daead26b35fac8aa7b54239d625ac6`. Source-bound CI36993825114 is equivalent to `e25b0ba239c8146d2d661f26ec5ad29e82a14853` for the production reader, fixture and caller; see the [original component packet](../issue197-deck-component-20261002/README.md). The default candidate correctness mode runs mapping replacement between words and real RAM mono/stereo cases. This is the retained three-case fixture, not the later thirteen-case lifetime extension or a game/audio-throughput measurement.

Run `20261003-015123616-0d727ee4d97b44dea92568fe3063c486`; job `runner62eff1c-retained-ram-controls-20261003`. The frozen job, launch/hash identities, raw output, native exit, telemetry, assessment and complete collection manifest are in the archive. `INDEX.json` pins every payload and identifies secondary omissions. xemu product code remains unchanged from previous evidence head `9474ebfafd18e18a5ad700f1d81e8e01f8b5c5f5`.

## Operational boundary

SSH was used only for operator package installation/restart. Package files and exact runner version were checked on Deck; the existing configuration, official release and workspace backup were retained. Job cloning, submission, waiting, results and collection used `http://10.0.0.123:9368` through maintained clients.

Future game measurements need fresh immutable definitions carrying the updated controller/scene contracts, fresh A/A controls and physical ABBA/BAAB. This single correctness canary supplies no bridge for pooling timings across runner versions, no retroactive scene qualification, and no Mesa-cache waiver. A process fixture does not exercise Mesa or renderer state. Historical emulator performance evidence and failed attempts remain intact.
