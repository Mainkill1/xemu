test_nv2a_ptimer = executable(
  'test-nv2a-ptimer',
  files(
    'test-nv2a-ptimer.c',
    'mock-nv2a-ptimer.c',
    meson.project_source_root() / 'hw/xbox/nv2a/ptimer.c',
    meson.project_source_root() / 'hw/xbox/nv2a/ptimer_core.c'
  ),
  c_args: [
    '-DCOMPILING_PER_TARGET',
    '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
    '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
  ],
  include_directories: [
    include_directories('../../../target/i386'),
  ],
  dependencies: [qemuutil],
  link_args: host_os == 'windows' ? ['-Wl,--stack,16777216'] : [],
  build_by_default: false,
)

test_env = environment()
if host_os == 'darwin'
  macos_arch = host_machine.cpu() == 'aarch64' ? 'arm64' : host_machine.cpu()
  test_env.prepend('DYLD_FALLBACK_LIBRARY_PATH', meson.project_source_root() / 'macos-libs' / macos_arch / 'opt/local/lib')
  test_env.prepend('DYLD_LIBRARY_PATH', meson.project_source_root() / 'macos-libs' / macos_arch / 'opt/local/lib')
endif

test('test-nv2a-ptimer', test_nv2a_ptimer,
     args: ['--tap', '-k'],
     protocol: 'tap',
     env: test_env,
     suite: ['xbox'])

test_nv2a_guest_lock = executable(
  'test-nv2a-guest-lock',
  files(
    'test-nv2a-guest-lock.c',
    meson.project_source_root() / 'hw/xbox/nv2a/guest-lock.c'
  ),
  c_args: [
    '-DCOMPILING_PER_TARGET',
    '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
    '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
  ],
  include_directories: [include_directories('../../../target/i386')],
  dependencies: [qemuutil],
  build_by_default: false,
)
test('test-nv2a-guest-lock', test_nv2a_guest_lock,
     args: ['--tap', '-k'],
     protocol: 'tap',
     env: test_env,
     suite: ['xbox'])

test_nv2a_guest_lock_timer = executable(
  'test-nv2a-guest-lock-timer',
  files(
    'test-nv2a-guest-lock-timer.c',
    meson.project_source_root() / 'hw/xbox/nv2a/guest-lock.c',
    meson.project_source_root() / 'hw/xbox/nv2a/ptimer.c',
    meson.project_source_root() / 'hw/xbox/nv2a/ptimer_core.c',
    meson.project_source_root() / 'system/cpu-timers.c'
  ),
  c_args: [
    '-DCOMPILING_PER_TARGET',
    '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
    '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
    '-ffunction-sections', '-fdata-sections',
  ],
  include_directories: [include_directories('../../../target/i386')],
  dependencies: [qemuutil],
  link_args: host_os == 'darwin' ? ['-Wl,-dead_strip'] : ['-Wl,--gc-sections'],
  build_by_default: false,
)
test('test-nv2a-guest-lock-timer', test_nv2a_guest_lock_timer,
     args: ['--tap', '-k'],
     protocol: 'tap',
     env: test_env,
     suite: ['xbox'])
