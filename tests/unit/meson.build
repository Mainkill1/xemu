
testblock = declare_dependency(dependencies: [block], sources: 'iothread.c')

tests = {
  'check-block-qdict': [],
  'check-qdict': [],
  'check-qnum': [],
  'check-qstring': [],
  'check-qlist': [],
  'check-qnull': [],
  'check-qobject': [],
  'check-qjson': [],
  'check-qlit': [],
  'test-error-report': [],
  'test-qobject-output-visitor': [testqapi],
  'test-clone-visitor': [testqapi],
  'test-qobject-input-visitor': [testqapi],
  'test-forward-visitor': [testqapi],
  'test-string-input-visitor': [testqapi],
  'test-string-output-visitor': [testqapi],
  'test-visitor-serialization': [testqapi],
  'test-bitmap': [],
  'test-xbox-pgraph-texture-state': [],
  'test-xbox-pgraph-uniform-dirty': [],
  'test-xbox-pgraph-uniform-source': [],
  'test-xbox-pgraph-reports': [
    meson.project_source_root() / 'hw/xbox/nv2a/pgraph/reports.c',
  ],
  'test-xbox-vk-blend-constants': [],
  'test-xbox-vk-bc-layout': [],
  'test-xbox-vk-failure-state': [],
  'test-xbox-vk-failpoint': [
    meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/failpoint.c',
    declare_dependency(
      compile_args: ['-DXEMU_VK_DIAGNOSTIC_FAILURES'],
    ),
  ],
  'test-xbox-vk-pipeline-lifetime': [],
  'test-xbox-nv2a-ptimer': [
    'ptimer-test-stubs.c',
    meson.project_source_root() / 'hw/xbox/nv2a/ptimer.c',
    declare_dependency(
      compile_args: [
        '-include',
        meson.project_source_root() / 'tests/unit/xbox-nv2a-ptimer-test-shim.h',
      ],
    ),
  ],
  'test-resv-mem': [],
  # all code tested by test-x86-topo is inside topology.h
  'test-x86-topo': [],
  'test-cutils': [],
  'test-div128': [],
  'test-shift128': [],
  'test-mul64': [],
  # all code tested by test-int128 is inside int128.h
  'test-int128': [],
  'rcutorture': [],
  'test-rcu-list': [],
  'test-rcu-simpleq': [],
  'test-rcu-tailq': [],
  'test-rcu-slist': [],
  'test-qdist': [],
  'test-qht': [],
  'test-qtree': [],
  'test-bitops': [],
  'test-bitcnt': [],
  'test-qgraph': ['../qtest/libqos/qgraph.c'],
  'check-qom-interface': [qom],
  'check-qom-proplist': [qom],
  'test-qemu-opts': [],
  'test-keyval': [testqapi],
  'test-logging': [],
  'test-qapi-util': [],
  'test-interval-tree': [],
  'test-fifo': [],
  'test-lru-active': [],
}

# These integration units retain only the production report call chains from
# large target-specific translation units.  Section GC keeps their test link
# closure bounded without copying the wrapper, decoder, finish, or drainer.
if 'i386-softmmu' in target_dirs
  nv2a_report_section_args = cc.get_supported_arguments(
    '-ffunction-sections',
    '-fdata-sections',
  )
  if host_os == 'darwin'
    nv2a_report_gc_link_args = cc.get_supported_link_arguments(
      '-Wl,-dead_strip',
    )
  else
    nv2a_report_gc_link_args = cc.get_supported_link_arguments(
      '-Wl,--gc-sections',
    )
  endif

  if nv2a_report_section_args.length() == 2 and \
     nv2a_report_gc_link_args.length() == 1
    nv2a_report_test = declare_dependency(
      sources: config_target_h['i386-softmmu'],
      compile_args: [
        '-DCOMPILING_PER_TARGET',
        '-DCONFIG_TARGET="i386-softmmu-config-target.h"',
        '-include',
        meson.project_source_root() / 'tests/unit/xbox-nv2a-report-test-shim.h',
      ] + nv2a_report_section_args,
      link_args: nv2a_report_gc_link_args,
      include_directories: include_directories('../../target/i386'),
    )
    nv2a_report_sources = [
      'xbox-pgraph-report-test-support.c',
      meson.project_source_root() / 'hw/xbox/nv2a/nv2a.c',
      meson.project_source_root() / 'hw/xbox/nv2a/pgraph/pgraph.c',
      meson.project_source_root() / 'hw/xbox/nv2a/pgraph/reports.c',
      nv2a_report_test,
      nv2a_vsh_cpu.partial_dependency(compile_args: true, includes: true),
    ]

    tests += {
      'test-xbox-pgraph-report-wrapper': nv2a_report_sources,
    }

    if vulkan.found()
      nv2a_vk_report_test_headers = [
        volk.partial_dependency(compile_args: true, includes: true),
        libglslang.partial_dependency(compile_args: true, includes: true),
        vma.partial_dependency(compile_args: true, includes: true),
        vulkan.partial_dependency(compile_args: true, includes: true),
        spirv_reflect.partial_dependency(compile_args: true, includes: true),
      ]
      tests += {
        'test-xbox-pgraph-vk-reports': nv2a_report_sources + [
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/command.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/draw.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/perf.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/reports.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/vk/surface-compute.c',
          meson.project_source_root() / 'hw/xbox/nv2a/pgraph/profile.c',
        ] + nv2a_vk_report_test_headers,
      }
    endif
  else
    message('Skipping NV2A report integration tests: section GC unsupported')
  endif
endif

# This real graphics lifecycle test skips unless explicitly enabled on a host
# with an interactive SDL/OpenGL session.
if sdl.found() and epoxy.found()
  tests += {
    'test-xbox-gloffscreen': [
      meson.project_source_root() / 'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/sdl.c',
      sdl, epoxy,
    ],
  }
endif

if have_system or have_tools
  tests += {
    'test-qmp-event': [testqapi],
  }

  if seccomp.found()
    tests += {'test-seccomp': ['../../system/qemu-seccomp.c', seccomp]}
  endif
endif

if have_block
  tests += {
    'test-coroutine': [testblock],
    'test-aio': [testblock],
    'test-aio-multithread': [testblock],
    'test-throttle': [testblock],
    'test-thread-pool': [testblock],
    'test-hbitmap': [testblock],
    'test-bdrv-drain': [testblock],
    'test-bdrv-graph-mod': [testblock],
    'test-blockjob': [testblock],
    'test-blockjob-txn': [testblock],
    'test-block-backend': [testblock],
    'test-block-iothread': [testblock],
    'test-write-threshold': [testblock],
    'test-crypto-hash': [crypto],
    'test-crypto-hmac': [crypto],
    'test-crypto-cipher': [crypto],
    'test-crypto-akcipher': [crypto],
    'test-crypto-secret': [crypto, keyutils],
    'test-crypto-der': [crypto],
    'test-authz-simple': [authz],
    'test-authz-list': [authz],
    'test-authz-listfile': [authz],
    'test-io-task': [testblock],
    'test-io-channel-socket': ['socket-helpers.c', 'io-channel-helpers.c', io],
    'test-io-channel-file': ['io-channel-helpers.c', io],
    'test-io-channel-command': ['io-channel-helpers.c', io],
    'test-io-channel-buffer': ['io-channel-helpers.c', io],
    'test-io-channel-null': [io],
    'test-crypto-ivgen': [io],
    'test-crypto-afsplit': [io],
    'test-crypto-block': [io],
    'test-timed-average': [],
    'test-uuid': [],
  }
  if gnutls.found() and \
     tasn1.found() and \
     host_os != 'windows'
    tests += {
      'test-crypto-tlscredsx509': ['crypto-tls-x509-helpers.c',
                                   tasn1, crypto, gnutls],
      'test-crypto-tlssession': ['crypto-tls-x509-helpers.c', 'crypto-tls-psk-helpers.c',
                                 tasn1, crypto, gnutls],
      'test-io-channel-tls': ['io-channel-helpers.c', 'crypto-tls-x509-helpers.c',
                              tasn1, io, crypto, gnutls]}
  endif
  if pam.found()
    tests += {'test-authz-pam': [authz]}
  endif
  if host_os != 'windows'
    tests += {
      'test-image-locking': [testblock],
      'test-nested-aio-poll': [],
    }
  endif
  if config_host_data.get('CONFIG_REPLICATION')
    tests += {'test-replication': [testblock]}
  endif
  tests += {'test-crypto-pbkdf': [io]}
endif

if have_system
  tests += {
    'ptimer-test': ['ptimer-test-stubs.c', meson.project_source_root() / 'hw/core/ptimer.c'],
    'test-iov': [],
    'test-opts-visitor': [testqapi],
    'test-xs-node': [qom],
    'test-virtio-dmabuf': [meson.project_source_root() / 'hw/display/virtio-dmabuf.c'],
    'test-qmp-cmds': [testqapi],
    'test-xbzrle': [migration],
    'test-util-sockets': ['socket-helpers.c'],
    'test-base64': [],
    'test-bufferiszero': [],
    'test-smp-parse': [qom, meson.project_source_root() / 'hw/core/machine-smp.c'],
    'test-vmstate': [migration, io],
    'test-yank': ['socket-helpers.c', qom, io, chardev]
  }
  if config_host_data.get('CONFIG_INOTIFY1')
    tests += {'test-util-filemonitor': []}
  endif

  # Some tests: test-char, test-qdev-global-props, and test-qga,
  # are not runnable under TSan due to a known issue.
  # https://github.com/google/sanitizers/issues/1116
  if not get_option('tsan')
    if host_os != 'windows'
        tests += {
          'test-char': ['socket-helpers.c', qom, io, chardev]
        }
    endif

    tests += {
      'test-qdev-global-props': [qom, hwcore]
    }
  endif
endif

if have_ga and host_os == 'linux'
  tests += {'test-qga': ['../qtest/libqmp.c']}
  test_deps += {'test-qga': qga}
endif

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

slow_tests = {
  'test-aio-multithread' : 120,
  'test-bufferiszero': 60,
  'test-crypto-block' : 300,
  'test-crypto-tlscredsx509': 90,
  'test-crypto-tlssession': 90,
  'test-replication': 60,
}

foreach test_name, extra: tests
  src = [test_name + '.c']
  deps = [qemuutil]
  if extra.length() > 0
    # use a sourceset to quickly separate sources and deps
    test_ss = ss.source_set()
    test_ss.add(extra)
    src += test_ss.all_sources()
    deps += test_ss.all_dependencies()
  endif
  exe = executable(test_name, src, genh, dependencies: deps)

  test(test_name, exe,
       depends: test_deps.get(test_name, []),
       env: test_env,
       args: ['--tap', '-k'],
       protocol: 'tap',
       timeout: slow_tests.get(test_name, 30),
       priority: slow_tests.get(test_name, 30),
       suite: ['unit'])
endforeach
