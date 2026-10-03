from pathlib import Path
import json,os,subprocess,shlex
out=Path(__file__).resolve().parent
root=Path('/home/codex/src/steamdeck-xemu/worktrees/issue197-sample-memory')
build=Path('/home/codex/src/steamdeck-xemu/worktrees/issue246-allocation-attribution/build-trace')
a=json.loads((out.parent/'local-regression/build-command.json').read_text())['argv']
i=a.index(str(root/'tests/unit/test-xbox-mcpx-apu-sample-memory.c'));a[i]=str(out/'test-memory.c');a[a.index('-o')+1]=str(out/'test-memory.o');a=a[:-1]+['-c']
env=dict(os.environ,TMPDIR='/home/codex/src/steamdeck-xemu/scratch/compiler-tmp')
r=subprocess.run(a,env=env,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0
lines=(build/'build.ninja').read_text().splitlines();i=next(i for i,s in enumerate(lines) if s.startswith('build qemu-system-i386: cpp_LINKER_RSP '));inputs=shlex.split(lines[i].split('cpp_LINKER_RSP ',1)[1].split(' | ',1)[0]);args=[];links=[]
for line in lines[i+1:]:
 if line and not line.startswith(' '):break
 if line.startswith(' ARGS = '):args=shlex.split(line[len(' ARGS = '):])
 if line.startswith(' LINK_ARGS = '):links=shlex.split(line[len(' LINK_ARGS = '):])
cmd=['c++']+args+['-o',str(out/'test-real-memory')]+inputs+[str(out/'test-memory.o')]+links+['-Wl,--wrap=main']
(out/'commands.json').write_text(json.dumps({'compile':a,'link':cmd,'cwd':str(build),'scope':'Existing local full emulator objects; production reader from current PR header. Not a matched performance build.'},indent=2)+'\n')
r=subprocess.run(cmd,cwd=build,env=env,capture_output=True,text=True);(out/'link.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0
