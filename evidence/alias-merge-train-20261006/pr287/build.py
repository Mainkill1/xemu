import json,subprocess
from pathlib import Path
root=Path.cwd();out=root/'.scratch/alias-train-20261006/pr287';spec=json.loads((root/'.scratch/alias-train-20261006/pr282/native-commands.json').read_text());base=spec['commands'][0];commands=[]
# Reuse configured main dependency headers/libraries, compile actual refreshed #283 sources.
for label,source in [('native',out/'native.c'),('compute',root/'xemu-pr287/hw/xbox/nv2a/pgraph/vk/surface-compute.c'),('map',root/'xemu-pr287/hw/xbox/nv2a/pgraph/vk/surface-alias-map.c'),('swizzle',root/'xemu-pr287/hw/xbox/nv2a/pgraph/swizzle.c')]:
 a=list(base);a[a.index('-o')+1]=str(out/(label+'.o'));a[a.index('-c')+1]=str(source);a=['-iquote'+str(root/'xemu-pr287') if False else x for x in a];a[1:1]=['-iquote',str(root/'xemu-pr287')];commands.append(a)
link=['c++','-Wl,--gc-sections','-o',str(out/'native'),*[str(out/(n+'.o')) for n in ['native','compute','map','swizzle']],str(root/'xemu-pr282/build/subprojects/volk/libvolk.a'),str(root/'xemu-pr282/build/libqemuutil.a'),'-lshaderc','-lglib-2.0','-ldl','-lm'];commands.append(link)
(out/'commands.json').write_text(json.dumps({'cwd':spec['cwd'],'commands':commands},indent=2))
with (out/'build.log').open('w') as log:
 for a in commands:subprocess.run(a,cwd=spec['cwd'],check=True,stdout=log,stderr=subprocess.STDOUT)
