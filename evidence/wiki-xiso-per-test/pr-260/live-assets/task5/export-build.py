import pathlib, shlex, subprocess, sys
root = pathlib.Path('/home/codex/xemu-shader-workbench-handoff/xemu-pr260')
kind = 'export'
san = 'sanitized' in sys.argv
base = ['asset-browser-model.cc', 'asset-browser-decode.cc']
sources = base
if kind == 'export':
    sources += ['asset-browser-export.cc','asset-browser-glb.cc','asset-browser-material.cc','shader-browser-capture-inspection.cc','asset-browser-controller.cc','asset-browser-live.cc','shader-browser-capture-session.cc','shader-browser-capture-resources.cc','shader-browser-draw-capture.cc','shader-browser-model.cc']
output = pathlib.Path('/tmp/pr260-assets-' + kind + ('-sanitized' if san else ''))
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'glib-2.0'], text=True))
cmd = ['c++', '-std=c++17', '-O1' if san else '-O2', '-g', '-Wall', '-Wextra', '-Werror',
       '-I.', '-Iinclude', '-Iui/xui',
       '-I/home/codex/pr238-source/subprojects/xxHash-0.8.3',
       '-I/home/codex/pr239-source/subprojects/nlohmann_json/single_include',
       '-I/tmp/pr259-timing-review-include',
       'tests/unit/test-xemu-asset-browser-' + kind + '.cc']
subprocess.run(['c++','-std=c++17','-O2','-msse4.1','-mpclmul','-c',str(root/'ui/thirdparty/fpng/fpng.cpp'),'-o','/tmp/pr260-fpng.o'],check=True)
cmd += ['/tmp/pr260-fpng.o']
cmd += ['ui/xui/' + s for s in sources] + flags + ['-Wl,-l:libxxhash.so.0', '-msse4.1', '-mpclmul', '-pthread', '-o', str(output)]
if san: cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
print(shlex.join(cmd), flush=True)
subprocess.run(cmd, cwd=root, check=True)
env = dict(__import__('os').environ)
if san: env['ASAN_OPTIONS'] = 'detect_leaks=0'
subprocess.run([str(output), '--tap'], cwd=root, env=env, check=True)
