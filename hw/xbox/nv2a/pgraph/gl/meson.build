specific_ss.add([sdl, gloffscreen, files(
	'blit.c',
	'debug.c',
	'display.c',
	'draw-lifecycle.c',
	'draw-stage3.c',
	'gpuprops.c',
	'renderer.c',
	'reports.c',
	'shaders-stage3.c',
	'override-interface.c',
	'surface.c',
	'texture.c',
	'texture-stage.c',
	'vertex.c',
	)])

gl_override_egl = dependency('egl', required: false)
if host_os == 'linux' and gl_override_egl.found()
  gl_override_interface_test = executable(
    'test-xbox-gl-override-interface',
    meson.project_source_root() / 'tests/unit/test-xbox-gl-override-interface.c',
    files('override-interface.c'),
    include_directories: include_directories('../../../../..'),
    dependencies: [glib, epoxy, gl_override_egl])
  test('test-xbox-gl-override-interface', gl_override_interface_test,
       suite: ['unit'])
endif
