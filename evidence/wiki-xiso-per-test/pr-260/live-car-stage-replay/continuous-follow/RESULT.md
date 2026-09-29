# Continuous selected car inputs

Prior exact-head e90e4d7547208c8ff6c31fac4c019d0aaa60e429: push
36520508678 and PR36520513093 both20/20 successful. Unit190passed,0failed,17skipped.
Native6 run20260929-041628175-ada366b6c0ec411a9ea3afb5c1698f28 was archived
failed/incomplete without an acceptance marker. Test executable SHA256
8ab9b7004693a21d4c0386465531e16219a080898e9ab5cb74b704478785ba23.

Selected follow correctly admitted42parts instead of the full scene. Reopening
the preceding native recording restored37confirmed parts, original stages,
material inputs and post-blend DAC palette. Driving/steering changed the front
wheel at the fixed camera. HUD104–134FPS, freshposes4.5/s before correspondence
stopped; those are distinct measurements, not a completed live-motion gate.
The newer51-part frame retained80readbackcopies/895shares; this is an acquisition
count, not a performance-gain measurement.

Owned comparison: body1522vertices kept topology/source range but736positions
changed, maximum normalized delta0.00808741. Another276-vertex part was replaced
by67/342-vertex draws. Raw captured UV inputs changed too. The old follower
required byte-identical positions. Failed/native game bytes and images remain
private; no title assets or generated game programs are committed.

## Correction

- Explicit continuous LiveDrawInputs acquisition keeps a bounded3-frame window
  plus unresolved whole GPU frames, under the existing byte/event budgets.
  No forensic admission or dependency retention policy changes.
- Exact claim token returns the latest closed, fully completed owned frame.
  Pending/current frames cannot be published; retained snapshots survive eviction.
- Selected worker consumes completed frames while recording continues, eliminating
  per-request rearm gaps and50ms completion polling in selected follow.
- Scope/selection change, freeze, recorder stop and budgets stop only the owned
  acquisition and retain the completed display.
- Bounded deformation needs unchanged topology/source layout/range, same stage
  pairing and unique placement. Address reuse alone never establishes ownership.
  Split/LOD changes still hold the last coherent assembly and identify the
  specific part requiring reconfirmation.

## Verification

Runtime RED: service stopped at its second guest boundary; selected mode was
not continuous; stopped recorder left live enabled; changed vertex bytes held
frame1instead of frame2. See the four retained RED logs.

Final strict and ASan/UBSan23controller cases pass, model12cases pass, original
forensic session suite passes, four actual-flags product TUs compile with-Werror.
ASan uses detect_leaks=0 because leak checking cannot run under this environment's
ptrace constraints. No leak-check or native performance result is inferred.
Additional test setup corrections: uint64draw ordinal required explicituint32
conversion; a range-mismatch test initially used identical geometry, which the
existing exact-geometry policy legitimately accepts independently of address.
The corrected test changes both geometry and range, and preserves its prior
assertions for bounded deformation and incompatible topology/large changes.

New-head CI and a fresh immutable Windows driving/cadence test are required.
PR260 remains draft, stacked on draft259; no merge/release or Deck gain claim.
