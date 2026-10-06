from pathlib import Path
import json,shlex,subprocess,os
root=Path.cwd();p=Path(__file__).resolve().parent;src=root/'xemu-tcg-lookup-inline'
e=next(x for x in json.loads((root/'xemu-pr296/build/compile_commands.json').read_text()) if x['file'].endswith('/tcg/tcg-op.c') and '-DCONFIG_SOFTMMU' in x['command'])
a=shlex.split(e['command']);a=a[:a.index('-MD')];a[1:1]=['-iquote',str(src),'-iquote',str(src/'include')];a+=['-ffunction-sections','-fdata-sections']
r=subprocess.run(a+['-c',str(src/'tcg/tcg-op.c'),'-o',str(p/'emitted-tcg.o')],cwd=e['directory'],capture_output=True,text=True);assert r.returncode==0,r.stderr
r=subprocess.run(a+[str(p/'emitted-ir-check.c'),str(p/'emitted-tcg.o'),'-Wl,--gc-sections','-lglib-2.0','-o',str(p/'emitted-ir-check')],cwd=e['directory'],capture_output=True,text=True);(p/'emitted-ir-link.log').write_text(r.stdout+r.stderr);print(r.stderr[-5500:]);assert r.returncode==0
r=subprocess.run([str(p/'emitted-ir-check')],env=os.environ|{'XEMU_EXPERIMENTAL_INLINE_JUMP_CACHE':'1','XEMU_TB_LOOKUP_LOG':'test'},capture_output=True,text=True);(p/'emitted-ir.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0

r=subprocess.run([str(p/"emitted-ir-check")],env={k:v for k,v in os.environ.items() if k!="XEMU_EXPERIMENTAL_INLINE_JUMP_CACHE"},capture_output=True,text=True);(p/"emitted-disabled.log").write_text(r.stdout+r.stderr);print("disabled",r.stdout+r.stderr);assert r.returncode==0
