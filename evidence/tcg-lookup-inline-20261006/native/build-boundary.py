import json,shlex,subprocess
from pathlib import Path
root=Path.cwd();out=Path(__file__).resolve().parent
entry=next(e for e in json.loads((root/'xemu-pr296/build/compile_commands.json').read_text()) if e['file'].endswith('/accel/tcg/cpu-exec.c') and '-DCONFIG_SOFTMMU' in e['command'])
args=shlex.split(entry['command']);args=args[:args.index('-MD')]
args[1:1]=['-iquote',str(root/'xemu-tcg-lookup-inline'),'-iquote',str(root/'xemu-tcg-lookup-inline/include'),'-iquote',str(root/'xemu-tcg-lookup-inline/accel/tcg')]
args+=['-ffunction-sections','-fdata-sections']
parent=out/'parent-cpu-exec.c';parent.write_bytes(subprocess.check_output(['git','-C','xemu-tcg-lookup-inline','show','e15b180:accel/tcg/cpu-exec.c']))
for compiler in ['cc','clang']:
 a=args.copy();a[0]=compiler
 if compiler=='clang':a=[x for x in a if x not in ['-Wimplicit-fallthrough=2','-Wold-style-declaration','-Wshadow=local']]
 for tag,src in [('before',parent),('after',root/'xemu-tcg-lookup-inline/accel/tcg/cpu-exec.c')]:
  obj=out/(tag+'-'+compiler+'-bench.o');exe=out/(tag+'-'+compiler+'-boundary')
  subprocess.run(a+['-Werror','-c',str(src),'-o',str(obj)],cwd=entry['directory'],check=True)
  subprocess.run(a+['-Werror','-no-pie',str(out/'boundary-check.c'),str(obj),'-Wl,--gc-sections','-o',str(exe)],cwd=entry['directory'],check=True)
  print(exe.name,subprocess.check_output([str(exe)],text=True).strip())
