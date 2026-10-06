import json,shlex,subprocess,sys
from pathlib import Path
root=Path.cwd();out=Path(__file__).resolve().parent
entry=next(e for e in json.loads((root/'xemu-pr296/build/compile_commands.json').read_text()) if e['file'].endswith('/accel/tcg/cpu-exec.c') and '-DCONFIG_SOFTMMU' in e['command'])
args=shlex.split(entry['command']);args=args[:args.index('-MD')]
args[1:1]=['-iquote',str(root/'xemu-tcg-lookup-inline'),'-iquote',str(root/'xemu-tcg-lookup-inline/include')]
for compiler in ['cc','clang']:
 a=args.copy();a[0]=compiler
 if compiler=='clang':a=[x for x in a if x not in ['-Wimplicit-fallthrough=2','-Wold-style-declaration','-Wshadow=local']]
 obj=out/(sys.argv[1]+'-'+compiler+'.o')
 subprocess.run(a+['-Werror','-c',str(root/'xemu-tcg-lookup-inline/accel/tcg/cpu-exec.c'),'-o',str(obj)],cwd=entry['directory'],check=True)
 dis=subprocess.check_output(['objdump','-dr','--disassemble=helper_lookup_tb_ptr_i32',str(obj)],text=True)
 (out/(sys.argv[1]+'-'+compiler+'.asm')).write_text(dis)
 print(compiler,'calls common core:', 'lookup_tb_ptr_common' in dis)

assert all('lookup_tb_ptr_common' not in (out/(sys.argv[1]+'-'+c+'.asm')).read_text() for c in ['cc','clang']), 'internal lookup call remains'
