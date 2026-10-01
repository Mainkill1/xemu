from pathlib import Path
import re,json,subprocess,hashlib
R=Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply');O=R/'control-regression';O.mkdir(exist_ok=True);results={}
for v in ['parent','candidate']:
 root=R/f'native-builds/{v}-linux';exe=root/'squashfs-root/usr/bin/xemu';build=re.search('Build ID: ([a-f0-9]+)',subprocess.check_output(['readelf','-n',str(exe)],text=True))[1];debug=root/f'debug-root/usr/lib/debug/.build-id/{build[:2]}/{build[2:]}.debug'
 raw=subprocess.check_output(['nm','-S','--defined-only',str(debug)],text=True);(O/f'{v}-symbols.txt').write_text(raw);symbols={}
 for line in raw.splitlines():
  m=re.fullmatch(r'([a-f0-9]+) ([a-f0-9]+) ([Tt]) (.+)',line)
  if m:symbols[m[4]]={'address':int(m[1],16),'size':int(m[2],16)}
 sections=subprocess.check_output(['readelf','-SW',str(exe)],text=True);(O/f'{v}-sections.txt').write_text(sections)
 results[v]={'buildId':build,'symbols':symbols,'executableSha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
shared=results['parent']['symbols'].keys() & results['candidate']['symbols'].keys();wanted=['cpu_exec','cpu_tb_exec','cpu_loop_exec_tb','tb_lookup','cpu_get_tb_cpu_state','dsp_c_run','dsp56k_execute_instruction','helper_addss','helper_mulss','helper_divss','helper_flds_FT0','helper_fadds_FT0_ST0','tcg_qemu_tb_exec']
rows=[]
for name in sorted(shared):
 if any(name==w or name.startswith(w+'.') for w in wanted):
  a=results['parent']['symbols'][name];b=results['candidate']['symbols'][name];row={'symbol':name,'parent':a,'candidate':b,'addressDifference':b['address']-a['address'],'sizeDifference':b['size']-a['size']};rows.append(row)
  for v,entry in [('parent',a),('candidate',b)]:
   exe=R/f'native-builds/{v}-linux/squashfs-root/usr/bin/xemu'
   text=subprocess.check_output(['llvm-objdump-19','--disassemble','--no-show-raw-insn','--start-address='+hex(entry['address']),'--stop-address='+hex(entry['address']+entry['size']),str(exe)],text=True)
   file=O/(v+'-'+re.sub('[^a-zA-Z0-9_.-]','_',name)+'.asm');file.write_text(text)
(O/'layout-summary.json').write_text(json.dumps({'variants':{k:{f:v[f] for f in ['buildId','executableSha256']} for k,v in results.items()},'sharedTextSymbols':len(shared),'symbols':rows},indent=2)+'\n')
print(json.dumps(rows,indent=2))
