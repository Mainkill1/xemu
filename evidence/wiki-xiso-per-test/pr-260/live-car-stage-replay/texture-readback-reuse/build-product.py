import json,shlex,subprocess
from pathlib import Path
root=Path('/home/codex/xemu-shader-workbench-handoff/xemu-pr260');build=Path('/home/codex/pr238-source/build');out=Path('/tmp/pr260-production')
rows=json.loads((build/'compile_commands.json').read_text())
for name,template in [('hw/xbox/nv2a/pgraph/vk/draw.c','pgraph/vk/draw.c'),('hw/xbox/nv2a/pgraph/vk/texture.c','pgraph/vk/texture.c'),('hw/xbox/nv2a/pgraph/vk/command.c','pgraph/vk/command.c'),('ui/xui/asset-browser.cc','ui/xui/shader-browser.cc')]:
 spec=next(x for x in rows if x['file'].endswith(template));args=shlex.split(spec['command']);args=args[:args.index('-MD')]
 args[1:1]=['-I'+str(out),'-iquote',str(out),'-I/tmp/pr259-imgui','-I'+str(root),'-I'+str(root/'include'),'-iquote',str(root),'-iquote',str(root/'include')]
 print('Compile '+name,flush=True)
 subprocess.run([*args,'-Werror','-O0','-c',str(root/name),'-o',str(out/(Path(name).stem+'-texture-reuse.o'))],cwd=build,check=True)
print('Four changed product translation units passed',flush=True)
