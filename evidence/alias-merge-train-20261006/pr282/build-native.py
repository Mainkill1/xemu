import json,shlex,subprocess
from pathlib import Path
root=Path.cwd();out=root/'.scratch/alias-train-20261006/pr282';build=root/'xemu-pr282/build'
rows=json.loads((build/'compile_commands.json').read_text());r=next(x for x in rows if x['file'].endswith('/test-xbox-vk-stencil-write.c'));a=shlex.split(r['command']);a[a.index('-o')+1]=str(out/'native.o');a[-1]=str(out/'native.c');a+=['-Werror','-Wno-misleading-indentation'];subprocess.run(a,cwd=build,check=True,stdout=(out/'native-build.log').open('w'),stderr=subprocess.STDOUT)
link=shlex.split(subprocess.check_output(['ninja','-t','commands','tests/unit/test-xbox-vk-stencil-write'],cwd=build,text=True).splitlines()[-1]);link[link.index('-o')+1]=str(out/'native');link=[str(out/'native.o') if x.endswith('/test-xbox-vk-stencil-write.c.o') else x for x in link];link+=['-lshaderc'];subprocess.run(link,cwd=build,check=True,stdout=(out/'native-build.log').open('a'),stderr=subprocess.STDOUT)
(out/'native-commands.json').write_text(json.dumps({'cwd':str(build),'commands':[a,link]},indent=2))
