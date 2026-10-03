import json, os, pathlib, shlex, subprocess
root = pathlib.Path('/home/codex/src/steamdeck-xemu/worktrees/issue197-sample-memory')
build = pathlib.Path('/home/codex/src/steamdeck-xemu/worktrees/issue246-allocation-attribution/build-trace')
out = pathlib.Path(__file__).resolve().parent
args = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Werror', '-Wno-unused-parameter', '-Wno-shift-negative-value', '-D_GNU_SOURCE', '-DCOMPILING_PER_TARGET', '-DCONFIG_TARGET="i386-softmmu-config-target.h"', '-DCONFIG_DEVICES="i386-softmmu-config-devices.h"']
for p in [root, root/'include', root/'target/i386',root/'host/include/x86_64',root/'host/include/generic',build]:
    args.extend(['-iquote', str(p)])
args += shlex.split(subprocess.check_output(['pkg-config','--cflags','glib-2.0'],text=True))
args += [str(root/'tests/unit/test-xbox-mcpx-apu-sample-memory.c'), '-o', str(out/'test-sample-memory')]
args += shlex.split(subprocess.check_output(['pkg-config','--libs','glib-2.0'],text=True))
(out/'build-command.json').write_text(json.dumps({'argv':args,'note':'Focused unit uses generated headers from ee5ce48 + issue246 build; not a native performance build.'},indent=2)+'\n')
env=dict(os.environ,TMPDIR='/home/codex/src/steamdeck-xemu/scratch/compiler-tmp')
r=subprocess.run(args,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'build-output.txt').write_text(r.stdout)
print(r.stdout,end='');raise SystemExit(r.returncode)
