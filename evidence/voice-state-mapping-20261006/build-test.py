import json,subprocess,shlex,sys
from pathlib import Path
r=Path.cwd();b=r/'xemu-pr296/build';src=r/'xemu-vk-cubemap-surfaces';o=r/'.scratch/voice-state-mapping-20261006';e=next(x for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].endswith('/vp/vp.c'));a=shlex.split(e['command']);a=a[:a.index('-MD')];a[1:1]=(['-I'+str(o/'reference')] if '--reference' in sys.argv else [])+['-I'+str(src),'-iquote',str(src),'-iquote',str(src/'include')];obj=o/'voice-memory-test.o'
p=subprocess.run(a+['-Werror','-Wno-unused-function','-c',str(src/'tests/xbox/mcpx-apu/test-voice-memory.c'),'-o',str(obj)],cwd=b,capture_output=True,text=True);(o/'voice-memory-compile.log').write_text(p.stdout+p.stderr);print('compile',p.returncode,flush=True)
if p.returncode:print(p.stderr);raise SystemExit(p.returncode)
a=shlex.split(json.loads((o/'link.json').read_text())['command']);a[a.index('-o')+1]=str(o/'voice-memory-test');a.insert(a.index('-o'),str(obj));a+=['-Wl,--wrap=main'];p=subprocess.run(a,cwd=b,capture_output=True,text=True);(o/'voice-memory-link.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(o/'voice-memory-test'),'--tap'],capture_output=True,text=True,timeout=90);(o/'voice-memory-test.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);raise SystemExit(p.returncode)
