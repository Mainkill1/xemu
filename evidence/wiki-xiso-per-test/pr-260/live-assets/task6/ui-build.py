import pathlib, shlex, subprocess, sys
root = pathlib.Path('/home/codex/xemu-shader-workbench-handoff/xemu-pr260')
kind = 'ui'
san = 'sanitized' in sys.argv
base = ['asset-browser-model.cc', 'asset-browser-decode.cc']
sources = base
if kind == 'ui':
    sources += ['asset-browser-export.cc','asset-browser-glb.cc','shader-browser-capture-inspection.cc','asset-browser.cc','asset-browser-material.cc','asset-browser-viewport.cc','asset-browser-controller.cc','asset-browser-live.cc','shader-browser-capture-session.cc','shader-browser-capture-resources.cc','shader-browser-draw-capture.cc','shader-browser-model.cc']
output = pathlib.Path('/tmp/pr260-assets-' + kind + ('-sanitized' if san else ''))
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'glib-2.0','epoxy','sdl3'], text=True))
cmd = ['c++', '-std=c++17', '-O1' if san else '-O2', '-g', '-Wall', '-Wextra', '-Werror',
       '-I/tmp/pr259-imgui', '-I.', '-Iinclude', '-Iui/xui',
       '-I/home/codex/pr238-source/subprojects/xxHash-0.8.3',
       '-I/home/codex/pr239-source/subprojects/nlohmann_json/single_include',
       '-I/tmp/pr259-timing-review-include',
       'tests/unit/test-xemu-asset-browser-' + kind + '.cc']
cmd += ['ui/xui/' + s for s in sources] + flags + ['-Wl,-l:libxxhash.so.0', '-pthread', '-o', str(output)]
cmd += ['/home/codex/pr238-source/build/ui/thirdparty/libfpng.a']
cmd += [str(pathlib.Path('/tmp/pr259-imgui') / ('imgui'+suffix+'.cpp')) for suffix in ['', '_draw', '_tables', '_widgets']]
if san: cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
print(shlex.join(cmd), flush=True)
subprocess.run(cmd, cwd=root, check=True)
