import hashlib,json,statistics,subprocess
from pathlib import Path
root=Path.cwd(); out=root/'.scratch/pr190-followup'
order=['current','inline','inline','current','inline','current','current','inline']
binaries={'current':out/'compiler-current-v3','inline':out/'compiler-inline-v3'}
rows=[]
manifest={'purpose':'Isolate forced voice_get_samples inlining on current PR275 stack','currentCommit':subprocess.check_output(['git','-C','xemu-pr275-shared','rev-parse','HEAD'],text=True).strip(),'sourceChange':'static int -> static inline __attribute__((always_inline)) int voice_get_samples; no other changes','fixtureObjectSha256':hashlib.sha256((out/'throughput.o').read_bytes()).hexdigest(),'fixtureCommit':subprocess.check_output(['git','-C','xemu-pr303','rev-parse','HEAD'],text=True).strip(),'order':order,'cpuAffinity':list(range(9)),'frames':20000,'warmupFrames':256,'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0],'executables':{k:{'path':str(v.relative_to(root)),'sha256':hashlib.sha256(v.read_bytes()).hexdigest()} for k,v in binaries.items()},'measurement':'Production VP-only; full output/state checks outside timing; fixed work, not game FPS'}
(out/'inline-comparison-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for workers in [1,8]:
 for payload,route in [('mono-pcm','--benchmark-v3'),('mono-adpcm','--benchmark-v3'),('stereo-adpcm','--benchmark-v3-changing')]:
  for cell,variant in enumerate(order,1):
   command=['taskset','-c','0-8',str(binaries[variant]),route,'sinc',payload,'20000',str(workers)]
   p=subprocess.run(command,capture_output=True,text=True,timeout=60)
   (out/f'inline-{workers}-{payload}-{cell}-{variant}.log').write_text(p.stdout+p.stderr)
   assert p.returncode==0,p.stdout+p.stderr
   data=json.loads(next(s for s in p.stdout.splitlines() if s.startswith('{')))
   assert data['correctness']=='PASS' and data['workload_version']==3
   assert data['actual_workers']==workers and data['busy_workers']==workers
   rows.append({'workers':workers,'payload':payload,'route':route,'cell':cell,'variant':variant,'orderBlock':'ABBA' if cell<=4 else 'BAAB','command':command,'result':data})
   (out/'inline-comparison-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
  subset=[r for r in rows if r['workers']==workers and r['payload']==payload]
  med={v:statistics.median(r['result']['elapsed_us'] for r in subset if r['variant']==v) for v in binaries}
  print(json.dumps({'workers':workers,'payload':payload,'median_us':med,'time_reduction_percent':100*(1-med['inline']/med['current'])}),flush=True)
summary=[]
for workers in [1,8]:
 for payload in ['mono-pcm','mono-adpcm','stereo-adpcm']:
  subset=[r for r in rows if r['workers']==workers and r['payload']==payload]
  values={v:[r['result']['elapsed_us'] for r in subset if r['variant']==v] for v in binaries}
  median={v:statistics.median(a) for v,a in values.items()}
  blocks={}
  for block in ['ABBA','BAAB']:
   m={v:statistics.median(r['result']['elapsed_us'] for r in subset if r['variant']==v and r['orderBlock']==block) for v in binaries}
   blocks[block]={'median_us':m,'time_reduction_percent':100*(1-m['inline']/m['current'])}
  summary.append({'workers':workers,'payload':payload,'currentMedianUs':median['current'],'inlineMedianUs':median['inline'],'timeReductionPercent':100*(1-median['inline']/median['current']),'orderBlocks':blocks,'ranges':{v:[min(a),max(a)] for v,a in values.items()},'checks':'8/8 PASS'})
(out/'inline-comparison-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('Complete: 48/48 measurements and output/state checks PASS',flush=True)
