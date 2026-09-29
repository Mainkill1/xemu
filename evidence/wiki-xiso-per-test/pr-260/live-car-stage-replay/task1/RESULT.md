# Sampled Y16 capture regression

The guest label0x30 was rejected despite the actual sampled image being Vulkan
R16_UNORM. The native production adapter predicate now admits the supported host
color representation while true D16 remains unsupported. An owned staged snapshot
retains original bytes after the mapped source is mutated.

The shared material model owns bounded exact base-level storage and component
mapping alongside RGBA inspection images. Packet validation rejects malformed
storage; digest and memory accounting cover it. Both replay upload adapters use
R16 storage rather than quantized RGBA inspection pixels. Native Vulkan execution
proved adjacent0x8000/0x8001 samples select opposite comparison outcomes and the
captured R/zero/zero/zero mapping remains intact.

Commands: `/tmp/pr260-vk-depth-build.py` + built test; `/tmp/pr259-captured-extent-model-build.py depth-final`;
`/tmp/pr260-preview-depth-native-build.py` + native preview-vk; `/tmp/pr259-vk-batch-native-compile.py`.
All final commands exited0, with strict compiler warnings. The production fixture
and precision regression are committed under tests/unit. These local fixtures are
not a new Windows PGR2 or complete asset-viewer acceptance result. Native HUD GL
material precision is exercised with the connected-stage viewport milestone.
