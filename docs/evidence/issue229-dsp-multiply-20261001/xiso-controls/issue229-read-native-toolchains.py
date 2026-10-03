from pathlib import Path
import json,re,subprocess,tarfile,hashlib
r=Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply');o=r/'xiso-preparation';results={}
for variant in ['parent','candidate']:
 root=r/f'native-builds/{variant}-linux';exe=root/'squashfs-root/usr/bin/xemu'
 build_id=re.search(r'Build ID: ([a-f0-9]+)',subprocess.check_output(['readelf','-n',str(exe)],text=True))[1]
 debugroot=root/'debug-root';debug=debugroot/f'usr/lib/debug/.build-id/{build_id[:2]}/{build_id[2:]}.debug'
 if not debug.exists():
  with tarfile.open(root/'xemu-ubuntu-x86_64-release.tgz') as t:
   m=next(x for x in t.getmembers() if x.name.endswith('.ddeb'));pkg=o/f'{variant}-debug.ddeb'
   with t.extractfile(m) as src,pkg.open('wb') as dst:
    import shutil;shutil.copyfileobj(src,dst)
  subprocess.run(['dpkg-deb','-x',str(pkg),str(debugroot)],check=True)
 proc=subprocess.Popen(['readelf','--debug-dump=info',str(debug)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 units=[];current=None
 for line in proc.stdout:
  if '(DW_TAG_compile_unit)' in line:
   if current:units.append(current)
   current={}
  elif current is not None:
   for field in ['producer','name','comp_dir']:
    m=re.search(r'DW_AT_'+field+r'\s*:\s*(.*)',line)
    if m and field not in current:current[field]=m[1]
 if current:units.append(current)
 stderr=proc.stderr.read();assert proc.wait()==0,stderr
 producers=sorted(set(x.get('producer','') for x in units));dsp=[x for x in units if x.get('name','').endswith('apu/dsp/interp/dsp_cpu.c') or x.get('name','').endswith('dsp_cpu.c')]
 results[variant]={'executableSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'buildId':build_id,'debugSha256':hashlib.sha256(debug.read_bytes()).hexdigest(),'compileUnits':len(units),'producers':producers,'dspTranslationUnits':dsp,'readelfStderr':stderr}
(o/'native-toolchains.json').write_text(json.dumps(results,indent=2)+'\n')
for k,v in results.items():print(k,v['buildId'],v['compileUnits'],'units',v['dspTranslationUnits'])
