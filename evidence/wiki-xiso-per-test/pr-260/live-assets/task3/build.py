import subprocess,shlex
from pathlib import Path
root=Path('/home/codex/xemu-shader-workbench-handoff/xemu-pr260')
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','glib-2.0','epoxy','sdl3'],text=True))
cmd=['c++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-I.','-Iinclude','-Iui/xui','-I/tmp/pr259-timing-review-include','-I/home/codex/pr238-source/subprojects/xxHash-0.8.3','tests/unit/test-xemu-asset-browser-viewport.cc','ui/xui/asset-browser-viewport.cc','ui/xui/asset-browser-material.cc',*flags,'-o','/tmp/pr260-asset-viewport']
print(shlex.join(cmd),flush=True)
subprocess.run(cmd,cwd=root,check=True)

