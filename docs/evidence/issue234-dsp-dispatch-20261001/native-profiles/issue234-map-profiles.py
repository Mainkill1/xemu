from pathlib import Path
import subprocess,json,hashlib,os,re,collections
r=Path('/home/codex/src/steamdeck-xemu');a=r/'artifacts/issue234-dsp-dispatch';old=r/'artifacts/issue229-dsp-multiply/native-builds/parent-linux';out=a/'native-profile-mapped';out.mkdir(exist_ok=True)
binary=old/'squashfs-root/usr/bin/xemu';debug=next((old/'debug-root').rglob('c47cc0deb81956e198d71935d1a198116cdb02.debug'))
assert hashlib.sha256(binary.read_bytes()).hexdigest()=='4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a'
results={}
for b in ['c','jit']:
 p=next((a/'native-profiles'/b/'collected').glob('*'));data=next(p.glob('diagnostics/*/dsp-cpu-profile.data'))
 ids=subprocess.check_output(['perf','buildid-list','-i',str(data)],text=True)
 matches=[x for x in ids.splitlines() if x.endswith('/xemu')];assert len(matches)==1 and matches[0].split()[0]=='7cc47cc0deb81956e198d71935d1a198116cdb02'
 remote=matches[0].split(maxsplit=1)[1];s=out/'symfs'/remote.lstrip('/');s.parent.mkdir(parents=True,exist_ok=True)
 for target,source in [(s,binary),(s.parent/debug.name,debug)]:
  if not target.exists():target.symlink_to(source)
 for lib in (old/'squashfs-root/usr/lib').iterdir():
  t=s.parent/'lib'/lib.name;t.parent.mkdir(exist_ok=True)
  if lib.is_file() and not t.exists():t.symlink_to(lib)
 command=['perf','script','-G','-i',str(data),'--symfs',str(out/'symfs'),'-F','event,ip,sym,symoff,dso,period']
 (out/(b+'-command.json')).write_text(json.dumps(command,indent=2)+'\n')
 with (out/(b+'-leaf.txt')).open('w') as f,(out/(b+'-stderr.txt')).open('w') as err:subprocess.run(command,stdout=f,stderr=err,check=True)
 total=0;n=0;hist=collections.defaultdict(lambda:[0,0]);groups=collections.defaultdict(lambda:[0,0]);symbols=collections.defaultdict(lambda:[0,0])
 for line in (out/(b+'-leaf.txt')).read_text().splitlines():
  m=re.match(r'\s*(\d+) cycles:Pu:\s+\S+\s+(.+?) \((.+)\)$',line)
  if not m:raise ValueError(line)
  w=int(m[1]);sym=m[2];dso=m[3];total+=w;n+=1
  symbols[sym.split('+')[0]][0]+=1;symbols[sym.split('+')[0]][1]+=w
  if dso==remote and re.match(r'^(dsp_c_|dsp56k_|emu_)',sym):groups['named-C-interpreter'][0]+=1;groups['named-C-interpreter'][1]+=w
  if dso==remote and re.match(r'^dsp_jit_',sym):groups['named-JIT-wrapper'][0]+=1;groups['named-JIT-wrapper'][1]+=w
  q=re.match(r'dsp56k_execute_instruction\+0x([0-9a-f]+)',sym)
  if q:hist[int(q[1],16)][0]+=1;hist[int(q[1],16)][1]+=w
 regions={'selection/classification':[0x78,0xa0],'normal decode miss':[0xa0,0x135],'normal handler indirection':[0x135,0x13e],'parallel selection':[0x18e,0x1a1],'retained call':[0x1a1,0x1a6]}
 regionstats={}
 for name,(lo,hi) in regions.items():
  count=sum(v[0] for k,v in hist.items() if lo<=k<hi);weight=sum(v[1] for k,v in hist.items() if lo<=k<hi)
  regionstats[name]={'samples':count,'period':weight,'processPercent':100*weight/total}
 frames=[]
 for line in (p/'guest-frames.log').read_text().splitlines():
  q=re.match(r'timestamp_us=(\d+) frame=(\d+) delta_us=(\d+)',line)
  if q:frames.append(tuple(map(int,q.groups())))
 tail=[x for x in frames if x[0]>=frames[-1][0]-90000000 and x[2]>0]
 results[b]={'runId':p.name,'binarySha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'elfBuildId':matches[0].split()[0],'perfDataSha256':hashlib.sha256(data.read_bytes()).hexdigest(),'samples':n,'period':total,'canonicalOutcome':json.loads((a/'native-profiles'/b/'result-summary.json').read_text())['outcome'],'groups':{key:{'samples':v[0],'period':v[1],'processPercent':100*v[1]/total} for key,v in groups.items()},'executeFunction':{'samples':sum(v[0] for v in hist.values()),'period':sum(v[1] for v in hist.values()),'processPercent':100*sum(v[1] for v in hist.values())/total},'executeRegions':regionstats,'frameRecords':len(frames),'positiveIntervalsLast90s':len(tail),'requiredIntervals':160,'topSymbols':[{'symbol':key,'samples':v[0],'period':v[1],'processPercent':100*v[1]/total} for key,v in sorted(symbols.items(),key=lambda x:-x[1][1])[:25]]}
 (out/(b+'-ip-histogram.json')).write_text(json.dumps({hex(k):v for k,v in sorted(hist.items())},indent=2)+'\n')
(out/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
for b,x in results.items():print(b,'samples',x['samples'],'C exec%',x['executeFunction']['processPercent'],'C groups',x['groups'],'positive frame tail',x['positiveIntervalsLast90s'])
