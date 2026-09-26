xemu_ss.add(files(
  'actions.cc',
  'animation.cc',
  'compat.cc',
  'debug.cc',
  'shader-browser-model.cc',
  'shader-browser-details-model.cc',
  'shader-browser-details-store.cc',
  'shader-browser-details-bridge.cc',
  'shader-browser-recipe-inspector.cc',
  'shader-browser-overrides.cc',
  'shader-browser-override-store.cc',
  'shader-browser-override-lifecycle.cc',
  'shader-browser-override-scoped.cc',
  'shader-browser-override-effect.cc',
  'shader-browser-saved-rules.cc',
  'shader-browser-presets.cc',
  'shader-browser-replacement-library.cc',
  'shader-browser-stage3-ui.cc',
  'shader-browser-database.cc',
  'shader-browser-artifact-store.cc',
  'shader-browser-session-provider.cc',
  'shader-browser-preview-model.cc',
  'shader-browser-preview-adapter.cc',
  'shader-browser-preview-service.cc',
  'shader-browser-preview-service-work.cc',
  'shader-browser-preview-service-slots.cc',
  'shader-browser.cc',
  'font-manager.cc',
  'gl-helpers.cc',
  'input-manager.cc',
  'main-menu.cc',
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

# Shader Browser focused tests live with the production source declaration so
# the exact translation units are compiled by supported platform CI.
shader_browser_test_inc = include_directories('.')
shader_browser_threads = dependency('threads')
shader_browser_test_dir = meson.project_source_root() / 'tests/unit'

shader_browser_model_test = executable(
  'test-xemu-shader-browser-model',
  shader_browser_test_dir / 'test-xemu-shader-browser-model.cc',
  files('shader-browser-model.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash])
test('test-xemu-shader-browser-model', shader_browser_model_test,
     suite: ['unit'])

shader_browser_override_test = executable(
  'test-xemu-shader-browser-overrides',
  shader_browser_test_dir / 'test-xemu-shader-browser-overrides.cc',
  files('shader-browser-model.cc', 'shader-browser-overrides.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash])
test('test-xemu-shader-browser-overrides', shader_browser_override_test,
     suite: ['unit'])

shader_browser_override_store_test = executable(
  'test-xemu-shader-browser-override-store',
  shader_browser_test_dir / 'test-xemu-shader-browser-override-store.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-overrides.cc',
    'shader-browser-override-store.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-override-store',
     shader_browser_override_store_test, suite: ['unit'])

shader_browser_saved_rules_test = executable(
  'test-xemu-shader-browser-saved-rules',
  shader_browser_test_dir / 'test-xemu-shader-browser-saved-rules.cc',
  files('shader-browser-model.cc', 'shader-browser-overrides.cc',
        'shader-browser-override-store.cc', 'shader-browser-saved-rules.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash, sqlite3, json])
test('test-xemu-shader-browser-saved-rules',
     shader_browser_saved_rules_test, suite: ['unit'])

shader_browser_presets_test = executable(
  'test-xemu-shader-browser-presets',
  shader_browser_test_dir / 'test-xemu-shader-browser-presets.cc',
  files('shader-browser-model.cc', 'shader-browser-overrides.cc',
        'shader-browser-override-store.cc', 'shader-browser-saved-rules.cc',
        'shader-browser-presets.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash, sqlite3, json])
test('test-xemu-shader-browser-presets',
     shader_browser_presets_test, suite: ['unit'])

shader_browser_override_scoped_test = executable(
  'test-xemu-shader-browser-override-scoped',
  shader_browser_test_dir / 'test-xemu-shader-browser-override-scoped.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-overrides.cc',
    'shader-browser-override-store.cc',
    'shader-browser-override-scoped.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-override-scoped',
     shader_browser_override_scoped_test, suite: ['unit'])

shader_browser_override_effect_test = executable(
  'test-xemu-shader-browser-override-effect',
  shader_browser_test_dir / 'test-xemu-shader-browser-override-effect.cc',
  files('shader-browser-override-effect.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads])
test('test-xemu-shader-browser-override-effect',
     shader_browser_override_effect_test, suite: ['unit'])

shader_browser_replacement_library_test = executable(
  'test-xemu-shader-browser-replacement-library',
  shader_browser_test_dir / 'test-xemu-shader-browser-replacement-library.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-overrides.cc',
    'shader-browser-override-store.cc',
    'shader-browser-replacement-library.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-replacement-library',
     shader_browser_replacement_library_test, suite: ['unit'])

shader_browser_override_lifecycle_test = executable(
  'test-xemu-shader-browser-override-lifecycle',
  shader_browser_test_dir / 'test-xemu-shader-browser-override-lifecycle.cc',
  files('shader-browser-model.cc', 'shader-browser-overrides.cc',
        'shader-browser-override-store.cc',
        'shader-browser-replacement-library.cc',
        'shader-browser-saved-rules.cc',
        'shader-browser-override-lifecycle.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash, sqlite3, json])
test('test-xemu-shader-browser-override-lifecycle',
     shader_browser_override_lifecycle_test, suite: ['unit'])

shader_browser_details_bridge_test = executable(
  'test-xemu-shader-browser-details-bridge',
  shader_browser_test_dir / 'test-xemu-shader-browser-details-bridge.cc',
  files('shader-browser-model.cc',
        'shader-browser-details-model.cc',
        'shader-browser-details-store.cc',
        'shader-browser-details-bridge.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash])
test('test-xemu-shader-browser-details-bridge',
     shader_browser_details_bridge_test, suite: ['unit'])

shader_browser_recipe_inspector_test = executable(
  'test-xemu-shader-browser-recipe-inspector',
  shader_browser_test_dir / 'test-xemu-shader-browser-recipe-inspector.cc',
  files('shader-browser-model.cc',
        'shader-browser-recipe-inspector.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash, json])
test('test-xemu-shader-browser-recipe-inspector',
     shader_browser_recipe_inspector_test, suite: ['unit'])

shader_browser_database_test = executable(
  'test-xemu-shader-browser-database',
  shader_browser_test_dir / 'test-xemu-shader-browser-database.cc',
  files('shader-browser-model.cc', 'shader-browser-database.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash, sqlite3])
test('test-xemu-shader-browser-database', shader_browser_database_test,
     suite: ['unit'], timeout: 60)

shader_browser_artifact_test = executable(
  'test-xemu-shader-browser-artifact-store',
  shader_browser_test_dir / 'test-xemu-shader-browser-artifact-store.cc',
  files('shader-browser-model.cc', 'shader-browser-artifact-store.cc'),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-artifact-store', shader_browser_artifact_test,
     suite: ['unit'])

shader_browser_session_test = executable(
  'test-xemu-shader-browser-session-provider',
  shader_browser_test_dir / 'test-xemu-shader-browser-session-provider.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-database.cc',
    'shader-browser-artifact-store.cc',
    'shader-browser-session-provider.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash, sqlite3])
test('test-xemu-shader-browser-session-provider',
     shader_browser_session_test, suite: ['unit'], timeout: 60)

shader_browser_details_test = executable(
  'test-xemu-shader-browser-details-store',
  shader_browser_test_dir / 'test-xemu-shader-browser-details-store.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-details-model.cc',
    'shader-browser-details-store.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-details-store',
     shader_browser_details_test, suite: ['unit'])

shader_browser_preview_model_test = executable(
  'test-xemu-shader-browser-preview-model',
  shader_browser_test_dir / 'test-xemu-shader-browser-preview-model.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-preview-model.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash])
test('test-xemu-shader-browser-preview-model',
     shader_browser_preview_model_test, suite: ['unit'])

shader_browser_preview_adapter_test = executable(
  'test-xemu-shader-browser-preview-adapter',
  shader_browser_test_dir / 'test-xemu-shader-browser-preview-adapter.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-preview-model.cc',
    'shader-browser-preview-adapter.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [xxhash])
test('test-xemu-shader-browser-preview-adapter',
     shader_browser_preview_adapter_test, suite: ['unit'])

shader_browser_preview_service_test = executable(
  'test-xemu-shader-browser-preview-service',
  shader_browser_test_dir / 'test-xemu-shader-browser-preview-service.cc',
  files(
    'shader-browser-model.cc',
    'shader-browser-preview-model.cc',
    'shader-browser-preview-service.cc',
    'shader-browser-preview-service-work.cc',
    'shader-browser-preview-service-slots.cc',
  ),
  include_directories: shader_browser_test_inc,
  dependencies: [shader_browser_threads, xxhash])
test('test-xemu-shader-browser-preview-service',
     shader_browser_preview_service_test, suite: ['unit'])

shader_browser_c_header_test = executable(
  'test-xemu-shader-browser-c-header',
  shader_browser_test_dir / 'test-xemu-shader-browser-c-header.c',
  include_directories: shader_browser_test_inc)
test('test-xemu-shader-browser-c-header', shader_browser_c_header_test,
     suite: ['unit'])
