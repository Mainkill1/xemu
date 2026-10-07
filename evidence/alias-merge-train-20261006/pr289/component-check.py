import json,shlex,subprocess
from pathlib import Path
r=Path.cwd();out=r/'.scratch/alias-train-20261006/pr289'; spec=json.loads((r/'.scratch/alias-train-20261006/pr282/native-commands.json').read_text());a=spec['commands'][0];flags=[];i=0
while i<len(a):
 if a[i] in ['-o','-MF','-MT','-MQ','-c']: i+=2;continue
 if a[i] in ['-MD','-MMD']: i+=1;continue
 flags.append(a[i]);i+=1
flags[1:1]=['-iquote',str(r/'xemu-pr289')];common=[str(r/'xemu-pr289/hw/xbox/nv2a/pgraph/vk/surface-alias-map.c')];libs=shlex.split(subprocess.check_output(['pkg-config','--libs','glib-2.0'],text=True));commands=[]
for name,src,extra in [('morton',r/'xemu-pr289/tests/unit/test-xbox-vk-surface-alias-map.c',[str(r/'xemu-pr289/hw/xbox/nv2a/pgraph/swizzle.c')]),('sequence',r/'xemu-pr289/tests/unit/test-xbox-vk-alias-convert-sequence.c',[]),('generate',r/'.scratch/pr282-latest-qualification/pr286-generate.c',[])]:
 cmd=flags+['-Wl,--gc-sections',str(src)]+common+extra+libs+['-o',str(out/name)];commands.append(cmd);subprocess.run(cmd,cwd=spec['cwd'],check=True)
with (out/'checks.log').open('w') as log:
 subprocess.run([str(out/'morton')],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(out/'sequence')],stdout=log,stderr=subprocess.STDOUT,check=True)
 for size in [1,256]:
  shader=out/f'workgroup-{size}.comp'
  shader.write_bytes(subprocess.check_output([str(out/'generate'),str(size)]))
  cmd=['glslc',str(shader),'-o',str(out/f'workgroup-{size}.spv')];commands.append(cmd);subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
  cmd=['spirv-val',str(out/f'workgroup-{size}.spv')];commands.append(cmd);subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'commands.json').write_text(json.dumps({'cwd':spec['cwd'],'commands':commands},indent=2))
