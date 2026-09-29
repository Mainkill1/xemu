import json,shlex,subprocess
from pathlib import Path
root=Path('/home/codex/xemu-shader-workbench-handoff/xemu-pr260')
base=Path('/home/codex/pr238-source');build=base/'build';out=Path('/tmp/pr260-production');out.mkdir(exist_ok=True)
subprocess.run(['python3',str(base/'subprojects/genconfig/gen_config.py'),str(root/'config_spec.yml'),str(out/'xemu-config.h')],check=True)
rows=json.loads((build/'compile_commands.json').read_text())
spec=next(x for x in rows if x['file'].endswith('ui/xui/shader-browser.cc'))
args=shlex.split(spec['command']);args=args[:args.index('-MD')]
args[1:1]=['-I'+str(out),'-iquote',str(out),'-I/tmp/pr259-imgui','-I'+str(root),'-I'+str(root/'include'),'-iquote',str(root),'-iquote',str(root/'include')]
for name in ['asset-browser-platform.cc','asset-browser.cc','shader-browser.cc','shader-browser-session-provider.cc','menubar.cc','main.cc']:
 cmd=[*args,'-Werror','-O0','-c',str(root/'ui/xui'/name),'-o',str(out/(name+'.o'))]
 print('Compile '+name,flush=True);subprocess.run(cmd,cwd=build,check=True)
print('Changed production translation units passed',flush=True)
