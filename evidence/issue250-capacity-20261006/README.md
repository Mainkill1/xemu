# Graphics descriptor capacity experiment

Parent main e15b180; candidate 31159c52c6. Actual shaders.c pool construction, descriptor allocation, update and rollover logic included. Driver calls and buffer append/completion use narrow test doubles. Distinct descriptor handles cannot be republished before completion; pool counts must match all sets, including hybrid dynamic controls. A 2,000-publication batch triggers one capacity finish on the parent (retained failing assertion); candidate completes without a capacity finish then completes/reset before set reuse at its actual bound. Three cases pass: specialized-only layout, hybrid-capable specialized route, actual uber controls.

This is a focused publication/allocation check, not GPU visibility or native performance proof. Build uses configured headers/libraries from the existing local build and candidate production source with warnings as errors. The first edit accidentally targeted the compute array; the check still failed and that edit was removed before the candidate commit. Compute capacity remains 1,024.

Only renderer.h graphics descriptor bound changes (1,024 to 2,048). Extra 1,024 handles plus driver descriptors; no asynchronous ownership or guest-clock/input changes. Native A/B and broader acceptance remain required. Prior current-main PGR2 attribution is under issue250-pgr2-20261006.
