import json,os,pathlib,shlex,subprocess
root=pathlib.Path('/home/codex/src/steamdeck-xemu/worktrees/issue197-sample-memory')
build=pathlib.Path('/home/codex/src/steamdeck-xemu/worktrees/issue246-allocation-attribution/build-trace')
out=pathlib.Path(__file__).resolve().parent
entry=next(x for x in json.loads((build/'compile_commands.json').read_text()) if x['file'].endswith('/apu/vp/vp.c'))
a=shlex.split(entry['command']);a=a[:1]+['-I'+str(root),'-I'+str(root/'include'),'-Werror']+a[1:]
for flag,suffix in [('-o','vp.o'),('-MQ','vp.o'),('-MF','vp.o.d'),('-c',None)]:
 i=a.index(flag);a[i+1]=str(root/'hw/xbox/mcpx/apu/vp/vp.c') if suffix is None else str(out/suffix)
(out/'vp-compile-command.json').write_text(json.dumps({'argv':a,'cwd':str(build),'note':'Production VP compiled with current source headers, generated baseline headers and external dependency includes from existing issue246 local build; not a matched native performance build.'},indent=2)+'\n')
r=subprocess.run(a,cwd=build,env=dict(os.environ,TMPDIR='/home/codex/src/steamdeck-xemu/scratch/compiler-tmp'),capture_output=True,text=True)
(out/'vp-compile-output.txt').write_text(r.stdout+r.stderr+f'\nExit: {r.returncode}\n');print((out/'vp-compile-output.txt').read_text());raise SystemExit(r.returncode)
