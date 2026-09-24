specific_ss.add(files('device-selection.c'))

if vulkan.found()

specific_ss.add([volk, libglslang, vma, vulkan, spirv_reflect, 
	files(
		'blit.c',
		'background-worker-priority.c',
		'buffer.c',
		'command.c',
		'debug.c',
		'demand-executable.c',
		'demand-executable-runtime.c',
		'device-inventory.c',
		'display.c',
		'draw.c',
		'draw-lifecycle.c',
		'failpoint.c',
		'glsl.c',
		'gpuprops.c',
		'hybrid-compiler.c',
		'hybrid-family.c',
		'hybrid-family-codec.c',
		'hybrid-family-history.c',
		'hybrid-pipeline-builder.c',
		'hybrid-prewarm.c',
		'hybrid-prewarm-runtime.c',
		'hybrid-trace.c',
		'hybrid-policy.c',
		'image.c',
		'instance.c',
		'perf.c',
		'pipeline-cache-data.c',
		'pipeline-probe.c',
		'readiness-attribution.c',
		'renderer.c',
		'reports.c',
		'spirv-prewarm.c',
		'shaders.c',
		'surface-compute.c',
		'surface.c',
		'texture.c',
		'ubershader-controls.c',
		'vertex.c',
		)
	])

endif
