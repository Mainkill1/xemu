xemu_ss.add(files(
  'actions.cc',
  'animation.cc',
  'compat.cc',
  'debug.cc',
  'font-manager.cc',
  'gl-helpers.cc',
  'input-manager.cc',
  'main-menu.cc',
  'main-menu-dlss.cc',
  'main.cc',
  'misc.cc',
  'menubar.cc',
  'monitor.cc',
  'notifications.cc',
  'popup-menu.cc',
  'reporting.cc',
  'scene-components.cc',
  'scene-manager.cc',
  'scene.cc',
  'snapshot-manager.cc',
  'viewport-manager.cc',
  'welcome.cc',
  'widgets.cc',
))

if host_os == 'windows'
  xemu_ss.add(files('update.cc', 'win32-dxgi-present.cc'))
endif

# SDK-free DLSS configuration/status and generated-config roundtrip tests.
dlss_test_threads = dependency('threads')
dlss_test_dir = meson.project_source_root() / 'tests/unit'

dlss_test = executable(
  'test-xemu-dlss',
  dlss_test_dir / 'test-xemu-dlss.cc',
  files('../xemu-dlss.cc'),
  include_directories: include_directories('../..'),
  dependencies: [dlss_test_threads])
test('test-xemu-dlss', dlss_test, suite: ['unit'], protocol: 'tap')

dlss_config_test = executable(
  'test-xemu-dlss-config',
  dlss_test_dir / 'test-xemu-dlss-config.cc',
  include_directories: include_directories('../..'),
  dependencies: [genconfig, tomlplusplus])
test('test-xemu-dlss-config', dlss_config_test, suite: ['unit'])
