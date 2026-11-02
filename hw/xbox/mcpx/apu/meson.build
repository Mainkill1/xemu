mcpx_ss.add(sdl, files(
	'apu.c',
	'debug.c',
	'monitor.c',
	))

subdir('vp')
subdir('dsp')

dsp_replay_archive = dsp56300_emu_ffi.get_variable(
  internal: 'xemu_fallback_archive', default_value: '')
dsp_replay_version = dsp56300_emu_ffi.get_variable(
  internal: 'xemu_fallback_version', default_value: '')
if dsp_replay_archive != ''
  dsp_replay_identity = custom_target('dsp-replay-library-identity',
    input: dsp_replay_archive,
    output: 'dsp-replay-library-identity.h',
    command: [python, files('../../../../tests/xbox/mcpx-apu/gen-dsp-build-identity.py'),
              '@OUTPUT@', '@INPUT@', dsp_replay_version])
else
  dsp_replay_identity = custom_target('dsp-replay-library-identity',
    output: 'dsp-replay-library-identity.h',
    command: [python, files('../../../../tests/xbox/mcpx-apu/gen-dsp-build-identity.py'),
              '@OUTPUT@'])
endif

if get_option('xemu_dsp_capture')
  mcpx_ss.add(files('dsp/capture_frame.c', 'dsp/capture_ring.c',
                    'dsp/capture_stream.c', 'dsp/capture_wire.c') +
              [dsp_replay_identity])
endif
