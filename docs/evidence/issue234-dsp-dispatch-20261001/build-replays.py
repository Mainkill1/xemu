import json,pathlib,subprocess,sys,os
root=pathlib.Path('/home/codex/src/steamdeck-xemu'); wt=root/'worktrees/issue234-dsp-dispatch'; out=root/'artifacts/issue234-dsp-dispatch'
label=sys.argv[1]; source=sys.argv[2] if len(sys.argv)>2 else str(wt/'hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c')
args=json.loads((root/'artifacts/issue229-dsp-multiply/candidate-gcc-release-v2-compile-argv.json').read_text())
args=[a.replace('issue229-dsp-multiply','issue234-dsp-dispatch').replace('test-xbox-mcpx-dsp-mul.c','test-xbox-mcpx-dsp-dispatch.c') for a in args]
args[args.index('-o')+1]=str(out/label)
args.insert(1,'-DXEMU_DSP_CPU_SOURCE="'+source+'"')
if 'clang' in label:args[0]='clang';args.remove('-flto=auto');args.insert(1,'-flto=thin');args.insert(1,'-fuse-ld=lld')
if 'ubsan' in label:args.insert(1,'-fsanitize=undefined');args.insert(1,'-fno-sanitize-recover=all')
(out/(label+'-argv.json')).write_text(json.dumps(args,indent=2)+'\n')
env=dict(os.environ,TMPDIR=str(root/'scratch/compiler-tmp'))
with (out/(label+'-compile.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,env=env)
print(label,r.returncode)
if r.returncode:print((out/(label+'-compile.log')).read_text()[-5000:])
sys.exit(r.returncode)
