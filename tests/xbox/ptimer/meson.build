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

test_pgraph_control = executable(
  'test-pgraph-control',
  files(
    'test-pgraph-control.c',
    meson.project_source_root() / 'hw/xbox/nv2a/pgraph/pgraph.c'
  ),
  c_args: [
    '-DCOMPILING_PER_TARGET',
    '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
    '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
    '-ffunction-sections', '-fdata-sections',
  ],
  include_directories: [include_directories('../../../target/i386')],
  dependencies: [qemuutil, nv2a_vsh_cpu],
  link_args: host_os == 'darwin' ? ['-Wl,-dead_strip'] : ['-Wl,--gc-sections'],
  build_by_default: false,
)
test('test-pgraph-control', test_pgraph_control,
     args: ['--tap', '-k'], protocol: 'tap', env: test_env, suite: ['unit', 'xbox'])

if 'i386-softmmu' in target_dirs and vulkan.found()
  test_pgraph_bql_surface_download = executable(
    'test-pgraph-bql-surface-download',
    files('test-pgraph-bql-surface-download.c'),
    c_args: [
      '-DCOMPILING_PER_TARGET',
      '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
      '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
      '-DXBOX=1',
      '-ffunction-sections', '-fdata-sections',
    ],
    include_directories: [include_directories('../../../target/i386')],
    dependencies: [qemuutil, nv2a_vsh_cpu, volk, libglslang, vma, vulkan,
                   spirv_reflect],
    link_args: host_os == 'darwin' ? ['-Wl,-dead_strip'] : ['-Wl,--gc-sections'],
    build_by_default: false,
  )
  test('test-pgraph-bql-surface-download', test_pgraph_bql_surface_download,
       args: ['--tap', '-k'], protocol: 'tap', env: test_env,
       suite: ['unit', 'xbox'], timeout: 15)
endif

test_pfifo_flip = executable(
  'test-pfifo-flip', files('test-pfifo-flip.c'),
  c_args: [
    '-DCOMPILING_PER_TARGET',
    '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
    '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
    '-ffunction-sections', '-fdata-sections',
  ],
  include_directories: [include_directories('../../../target/i386')],
  dependencies: [qemuutil, nv2a_vsh_cpu],
  link_args: host_os == 'darwin' ? ['-Wl,-dead_strip'] : ['-Wl,--gc-sections'],
  build_by_default: false,
)
test('test-pfifo-flip', test_pfifo_flip,
     args: ['--tap', '-k'], protocol: 'tap', env: test_env,
     suite: ['unit', 'xbox'])

# The fixture includes the unmodified pgraph.c and pfifo.c to reach private
# Kelvin methods and the actual FIFO worker/stall/wait boundaries.
# Keep this private-state exception scoped; ordinary tests use public interfaces.
test_pgraph_ptimer_contention = executable(
  'test-pgraph-ptimer-contention',
  files('test-pgraph-ptimer-contention.c',
        meson.project_source_root() / 'hw/xbox/nv2a/ptimer.c',
        meson.project_source_root() / 'hw/xbox/nv2a/ptimer_core.c'),
  c_args: ['-DCOMPILING_PER_TARGET',
           '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
           '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
           '-ffunction-sections', '-fdata-sections'],
  include_directories: [include_directories('../../../target/i386')],
  dependencies: [qemuutil, nv2a_vsh_cpu],
  link_args: host_os == 'darwin' ? ['-Wl,-dead_strip'] : ['-Wl,--gc-sections'],
  build_by_default: false,
)
test('test-pgraph-ptimer-contention', test_pgraph_ptimer_contention,
     args: ['--tap', '-k'], protocol: 'tap', env: test_env,
     suite: ['unit', 'xbox'], timeout: 60)

# CPU-time diagnostic with scheduling hooks compiled out and real profiling.
# This is not a performance acceptance test or a new runtime candidate.
if host_os == 'linux'
  executable(
    'benchmark-pgraph-control-cost',
    files('test-pgraph-ptimer-contention.c',
          meson.project_source_root() / 'hw/xbox/nv2a/ptimer.c',
          meson.project_source_root() / 'hw/xbox/nv2a/ptimer_core.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/profile.c'),
    c_args: ['-DCOMPILING_PER_TARGET', '-DNV2A_CONTENTION_COST=1',
             '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
             '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"',
             '-ffunction-sections', '-fdata-sections'],
    include_directories: [include_directories('../../../target/i386')],
    dependencies: [qemuutil, nv2a_vsh_cpu],
    link_args: ['-Wl,--gc-sections'],
    build_by_default: false,
  )
endif
