import os,json,shlex,subprocess
repo=os.getcwd(); build='/home/codex/pr238-source/build'; out='/tmp/pr260-vk-texture-reuse'
os.makedirs(out,exist_ok=True)
specs=json.load(open(build+'/compile_commands.json'))
def native(source,name,template):
 s=next(v for v in specs if v['file'].endswith(template)); args=shlex.split(s['command']); clean=[];i=0
 while i<len(args):
  a=args[i]
  if a in ('-o','-MF','-MQ'): i+=2;continue
  if a in ('-MD','-c') or a==s['file']: i+=1;continue
  clean.append(a);i+=1
 clean[1:1]=['-I'+repo,'-I'+repo+'/include','-iquote',repo,'-iquote',repo+'/include','-I/tmp/pr259-timing-review-include','-Werror','-ffunction-sections','-fdata-sections']
 subprocess.run(clean+['-c',source,'-o',out+'/'+name+'.o'],cwd=build,check=True)
native(repo+'/tests/unit/test-xemu-shader-browser-vk-texture-reuse-native.c','fixture','pgraph/vk/draw.c')
native(repo+'/hw/xbox/nv2a/pgraph/s3tc.c','s3tc','pgraph/vk/draw.c')
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','glib-2.0'],text=True))
src=['tests/unit/test-xemu-shader-browser-vk-texture-reuse.cc','ui/xui/shader-browser-draw-request.cc','ui/xui/shader-browser-capture-session.cc','ui/xui/shader-browser-capture-resources.cc','ui/xui/shader-browser-draw-capture.cc','ui/xui/shader-browser-model.cc']
subprocess.run(['c++','-std=c++17','-O2','-DNDEBUG','-g','-Wall','-Wextra','-Werror','-I.','-Iinclude','-Iui/xui','-I/tmp/pr259-timing-review-include','-I/home/codex/pr239-source/subprojects/nlohmann_json/single_include',*src,*[out+'/'+x+'.o' for x in ['fixture','s3tc']],*flags,'-Wl,--gc-sections','-Wl,-l:libxxhash.so.0','/home/codex/pr238-source/build/subprojects/volk/libvolk.a','-ldl','-pthread','-o',out+'/test'],check=True)
print(out+'/test')

subprocess.run([out+'/test','--tap'],check=True)
