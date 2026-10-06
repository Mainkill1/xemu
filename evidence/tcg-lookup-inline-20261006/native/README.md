# PR329 native development pair and boundary checks

Parent main e15b180 (tree-equivalent release197654), candidate9debac1aeb.
One Deck Vulkan Conker pair, original frozen procedure255bdff7. Both start/end
images inspected: Xbox Live & Co menu. No changed waits, inputs, configuration,
audio mode or measurement boundaries. Single pair and uncontrolled driver cache;
not balanced acceptance, no stable speedup or 30 FPS claim.

|Metric|Parent|Candidate|
|---|---:|---:|
|Guest flip cadence/s|8.291|8.532|
|Process CPU (one core100%)|172.47%|171.98%|
|Mean frame interval ms|120.787|117.364|
|p95 ms|152.641|150.046|
|p99 ms|182.427|173.818|
|Maximum ms|195.214|183.469|

Both finish; both log the same GLib g_source_destroy shutdown assertion.
Private images remain outside git. Numeric identities and measurements in JSON.

Boundary fixture links actual before/after production CPU-exec objects:
22 cases each, GCC14.2 and Clang19.1.7, both helpers, wrapped32-bit PC/nonzero
CS base/flags, full key mismatch, invalid and empty entries, logging gate,
single-step and breakpoint-page/current-PC behavior. I/O permission checked
before the stubbed page lookup and debug exit. The page resolver deliberately
returns failure; this does not exercise real MMU, QHT hit, save/load or SMC.
All4 executions pass with Werror. Initial redundant fixture declaration warnings
retained, then removed before the strict final run. Production unchanged.

Build script uses configured xemu-pr296/build headers as the original linked
fixture does; it is development evidence, not a portable standalone test suite.

## Cumulative renderer-tree check

Reference65db507 (main+290+291), candidate890fa7954d (same tree plus329).
Same original uninstrumented Conker definition/configuration as the main pair.
Both images per arm show intended menu. One pair, uncontrolled driver cache;
no stable performance or 30FPS claim. Not an isolated renderer PR comparison.

|Metric|Before lookup inlining|After|
|---|---:|---:|
|Guest flip cadence/s|26.339|27.233|
|CPU (one core100%)|201.04%|200.73%|
|Mean interval ms|38.019|36.898|
|Median ms|33.368|33.355|
|p95 ms|50.112|50.065|
|p99 ms|66.633|66.575|
|Maximum ms|66.856|100.075|

Directionally +3.39% cadence; maximum worsens. Both retain the same GLib
shutdown assertion. The merged renderer prerequisites and full acceptance
remain unresolved; this diagnostic branch is not an omnibus merge proposal.
