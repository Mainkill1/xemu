import json, subprocess, shlex
from pathlib import Path
r=Path.cwd(); b=r/'xemu-pr296/build'; src=r/'xemu-vk-cubemap-surfaces'; o=r/'.scratch/voice-state-mapping-20261006'
e=next(x for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].endswith('/vp/vp.c'))
base=shlex.split(e['command']); base=base[:base.index('-MD')]
base[1:1]=['-I'+str(src),'-I'+str(src/'include'),'-iquote',str(src),'-iquote',str(src/'include')]
base+=['-Werror','-I'+str(src/'hw/xbox/mcpx/apu/vp')]
link=shlex.split(json.loads((o/'link.json').read_text())['command'])
fixture=o/'throughput.o'
p=subprocess.run(base+['-c',str(src/'tests/xbox/mcpx-apu/test-resampler-throughput.c'),'-o',str(fixture)],cwd=b,capture_output=True,text=True);(o/'fixture-build.log').write_text(p.stdout+p.stderr)
assert p.returncode==0,p.stderr
for variant,flags in [('baseline',[])]:
 obj=o/(variant+'-vp.o'); a=base+flags+['-c',str(o/'baseline-vp.c'),'-o',str(obj)];p=subprocess.run(a,cwd=b,capture_output=True,text=True);(o/(variant+'-build.log')).write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
 a=[str(obj) if x=='libqemu-i386-softmmu.a.p/hw_xbox_mcpx_apu_vp_vp.c.o' else x for x in link]
 a[a.index('-o')+1]=str(o/(variant+'-throughput'));a.insert(a.index('-o'),str(fixture));a+=['-Wl,--wrap=main','-Wl,--wrap=src_callback_new','-Wl,--wrap=qemu_cond_broadcast','-Wl,--wrap=SDL_GetNumLogicalCPUCores']
 p=subprocess.run(a,cwd=b,capture_output=True,text=True);(o/(variant+'-link.log')).write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
 print(variant,'production VP linked',flush=True)
 if variant=='profile':
  a=[str(obj) if x=='libqemu-i386-softmmu.a.p/hw_xbox_mcpx_apu_vp_vp.c.o' else x for x in link];a[a.index('-o')+1]=str(o/'profile-xemu');p=subprocess.run(a,cwd=b,capture_output=True,text=True);(o/'profile-xemu-link.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
