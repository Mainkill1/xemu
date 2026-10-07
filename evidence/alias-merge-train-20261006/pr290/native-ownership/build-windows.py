import json,shlex,subprocess,os
from pathlib import Path
r=Path.cwd();o=r/'.scratch/alias-train-20261006/pr290/native-ownership';b=r/'.scratch/upstream-workers4-20261005/local-win/source/xemu/build';tool=Path('/opt/xemu-toolchain/mxe/usr');env=dict(os.environ,PATH=str(tool/'bin')+':'+os.environ['PATH']);spec=next(x for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].endswith('pgraph/vk/surface-compute.c'));a=shlex.split(spec['command']);flags=[];i=0
while i<len(a):
 if a[i] in ['-o','-MF','-MT','-MQ','-c']:i+=2;continue
 if a[i] in ['-MD','-MMD','-mneeded','-no-pie'] or a[i].startswith('-flto'):i+=1;continue
 if a[i]==spec['file']:i+=1;continue
 flags.append(a[i].replace('/usr/local/mxe/usr',str(tool)).replace('/work/source/xemu',str(b.parent)));i+=1
flags[1:1]=['-iquote',str(r/'xemu-pr290'),'-iquote',str(r/'xemu-pr290/include')];flags=[x for x in flags if x not in ['-Wold-style-declaration','-Wshadow=local']];flags=[x.replace('-Wimplicit-fallthrough=2','-Wimplicit-fallthrough') for x in flags];flags+=['-ffunction-sections','-fdata-sections','-Werror','-Wno-nullability-completeness','-Wno-ignored-attributes']
trace=o/'trace';trace.mkdir(exist_ok=True);(trace/'trace-hw_xbox_nv2a.h').write_text((r/'xemu-pr282/build/trace/trace-hw_xbox_nv2a.h').read_text());flags[1:1]=['-iquote',str(o)];
commands=[];sources={'event':r/'xemu-pr290/util/event.c','notify':r/'xemu-pr290/util/notify.c','tracestate':o/'windows-tracestate.c','thread':r/'xemu-pr290/util/qemu-thread-win32.c','diagnostics':o/'windows-diagnostics.c','swizzle':r/'xemu-pr290/hw/xbox/nv2a/pgraph/swizzle.c','volk':b.parent/'subprojects/volk/volk.c','native':o/'native-portable.c','compute':r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/surface-compute.c','map':r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/surface-alias-map.c','convert':r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/surface-alias-convert.c','image':r/'xemu-pr290/hw/xbox/nv2a/pgraph/vk/image.c'}
with (o/'windows-build.log').open('w') as log:
 for name,src in sources.items():
  cmd=flags+['-c',str(src),'-o',str(o/(name+'-win.o'))];commands.append(cmd);subprocess.run(cmd,cwd=b,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 libs=shlex.split(subprocess.check_output(['x86_64-w64-mingw32.static-pkg-config','--libs','--static','glib-2.0'],env=env,text=True))
 cmd=['x86_64-w64-mingw32.static-g++','-static','-Wl,--gc-sections','-o',str(o/'native.exe')]+[str(o/(name+'-win.o')) for name in sources]+[str(b/'libqemuutil.a')]+libs+['-lsynchronization']
 commands.append(cmd);subprocess.run(cmd,cwd=b,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
(o/'windows-commands.json').write_text(json.dumps({'cwd':str(b),'commands':commands},indent=2))
