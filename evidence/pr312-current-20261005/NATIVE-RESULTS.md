# Current-main PR #312 qualification

Parent `afdde9eb62accd07686be104ebf4a9381785aaab`; candidate `e428a42bc3e0f2f60caccc9b738bde47e7a2f800`. This evidence branch is excluded from main. Both fresh official builds identify version0.8.136 and their source commit. The twelve title packages preserve identical paired non-executable files, frozen inputs, waits and immediate final-input measurement boundaries. No downstream resampler, UI or depth-alias child code is included.

## Native game results

48/48 runs completed: ABBA thenBAAB, four cells per side/title/host. Per-run canonical API metrics are primary. This table is the median of four per-run values per side; it does not pool frame samples. READ_3D cadence is not SDL-presented FPS. CPU100%=one logical core. GPU busy comes from the actual host collector; supported process-GPU metrics are separate. Negative improvement means increased cost or lower cadence.

| Host/title | CPU core% A→B | GPU busy% A→B | READ_3D/s A→B | Mean ms A→B | p95 ms A→B | p99 ms A→B | Max ms A→B |
|---|---:|---:|---:|---:|---:|---:|---:|
| deck/morrowind | 219.633→226.724 | 72.407→70.758 | 12.513→12.776 | 77.244→75.389 | 99.999→100.019 | 108.267→116.685 | 141.974→141.688 |
| deck/conker | 187.481→189.903 | 52.513→60.810 | 8.305→8.407 | 119.903→118.794 | 150.091→150.191 | 174.781→166.705 | 183.370→183.471 |
| deck/pgr2 | 401.364→406.242 | 49.905→56.260 | 6.803→7.385 | 120.306→117.040 | 275.092→277.026 | 339.680→347.473 | 378.621→431.363 |
| win/morrowind | 281.118→275.065 | 67.500→67.516 | 32.540→32.470 | 30.676→30.734 | 33.709→33.719 | 50.034→50.028 | 50.340→50.311 |
| win/conker | 248.032→250.638 | 56.782→56.881 | 30.000→30.000 | 33.333→33.333 | 33.505→33.590 | 33.723→33.753 | 34.355→33.993 |
| win/pgr2 | 323.199→322.331 | 43.857→42.967 | 30.000→30.000 | 33.333→33.333 | 33.596→33.636 | 33.902→34.096 | 34.299→34.411 |

Deck: AMD Custom APU0405,8logical CPUs,15,524,212,736bytes RAM, amdgpu1002:163F. Windows: Ryzen9 6900HX,16logical CPUs,16,361,480,192bytes RAM, NVIDIA RTX3070Ti Laptop GPU plus AMD1681 iGPU, High performance power plan. Exact inventory is retained per run.

Windows Morrowind is comparison-ineligible because driver-cache state is unmanaged. PGR2 is diagnostic: allDeck recordings start in the Hong Kong flyover and allWindows recordings start at countdown3; all finish parked0mph at the same start lane. No waits were added to hide this. Morrowind screenshots retain the same camera/location with changing water/NPC/fog; Conker screenshots all show the New File bar menu. These reviewed screenshots do not prove identical dynamic workloads.

Material observations remain visible: Deck Morrowind CPU cost+3.23%, p99 cost+7.78%, with p99 order effects−0.17%/−34.88% and cadence directions opposed. Deck PGR2 p99 cost+2.29%, opposite-order directions. Deck/Windows Conker CPU costs+1.29%/+1.05%. The function costs nanoseconds and ordinary1024-limit GPUs select the same workgroup/shader/dispatch as main; these data do not establish causality or a game speedup. No samples were discarded. All48 actual measurement segments have zero collector overruns and errors; GPU power/temperature/clocks are unavailable on Deck, so background/thermal causality is not proven.

## Physical GPU conversion

The production adapter fixture passed32/32 conversions on Steam Deck AMD Custom GPU0405, RADV VANGOGH. All57,008 output depth/stencil pixels were checked. Restricted property limits only decrease the physical device properties for the fixture; actualX/invocation limits are1024. No system libraries were installed; the scoped executable and private shaderc library were removed afterward. This retains the shaderc/hash/marker substitutions described in REFINEMENT.md and is not whole-renderer ownership or FPS proof. Native Windows conversion-fixture execution is not established.

## Evidence correction

The first production-differential.c run quoted a header shadowed by an older local fixture copy. Its logs/binaries do not validate the current helper and remain retained as INAPPLICABLE. The earlier probe-selector.c measured a copied downward policy; it is motivation, not direct current-production proof. production-differential-exact.c uses an angle include with only the current worktree include path. Both compiler dependency manifests name the actual e428 header, and both pass3,146,496 cases. Exact-production-selector raw timings and commands are retained separately; they supersede the copied-policy timing claim.

## Bounded limitations

Independent source review found no new correctness blocker. The inherited single X-dispatch assertion remains: with maxInv128 and maxCountX65535, a640×480 surface at6× scaling needs86,400 groups and is unsupported. Chunking is a separate change. Current fixture covers scale1/2, not this boundary.

Full paired XISO requests reject22/171 missing pinned oracles on both hosts. Supported focused ABBA/BAAB and full unpaired diagnostics retain all attempts and actual per-leaf work timings; their completed state is identified by campaign reports. Full unpaired timings are descriptive, not a qualified speedup campaign. The catalog has no dedicated depth/stencil conversion leaf, so the adapter fixture supplies targeted coverage instead of claiming color-only tests exercise it.

No game image, firmware, HDD, save payload, executable, private library, process dump, PCM or raw texture data is published.
