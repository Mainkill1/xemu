import json,subprocess,shlex
from pathlib import Path
r=Path.cwd();out=r/'.scratch/alias-train-20261006/pr290';spec=json.loads((r/'.scratch/alias-train-20261006/pr282/native-commands.json').read_text());a=spec['commands'][0];flags=[];i=0
while i<len(a):
 if a[i] in ['-o','-MF','-MT','-MQ','-c']:i+=2;continue
 if a[i] in ['-MD','-MMD']:i+=1;continue
 flags.append(a[i]);i+=1
flags[1:1]=['-iquote',str(r/'xemu-pr290')];libs=shlex.split(subprocess.check_output(['pkg-config','--libs','glib-2.0'],text=True));commands=[]
with (out/'checks.log').open('w') as log:
 for name in ['surface-alias-map','alias-convert-sequence','alias-producer-lookup','alias-blit','surface-ownership']:
  extra=[str(r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/surface-alias-map.c')]
  if name=='surface-alias-map':extra+=[str(r/'xemu-pr290/hw/xbox/nv2a/pgraph/swizzle.c')]
  if name=='surface-ownership':extra += [str(r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/draw.c'),str(r/'xemu-pr290/hw/xbox/nv2a/pgraph/swizzle.c'),'-Wl,--wrap=pgraph_vk_finish','-Wl,--wrap=pgraph_vk_ensure_not_in_render_pass']
  if name=='surface-ownership':extra += [str(x) for pattern in ['libevent-loop-base.a.p/*.o','libqom.a.p/*.o'] for x in (r/'xemu-pr282/build').glob(pattern)]
  cmd=flags+['-Wl,--gc-sections',str(r/f'xemu-pr290/tests/unit/test-xbox-vk-{name}.c')]+extra+[str(r/'xemu-pr282/build/libqemuutil.a')]+[str(r/'xemu-pr282/build/thirdparty/libvma.a'),str(r/'xemu-pr282/build/subprojects/volk/libvolk.a')]+libs+['-lstdc++','-ldl','-lm','-o',str(out/name)];commands.append(cmd)
  subprocess.run(cmd,cwd=spec['cwd'],stdout=log,stderr=subprocess.STDOUT,check=True);subprocess.run([str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'commands.json').write_text(json.dumps({'cwd':spec['cwd'],'commands':commands},indent=2))
