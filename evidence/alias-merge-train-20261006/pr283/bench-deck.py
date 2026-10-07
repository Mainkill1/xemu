import json,subprocess,sys,re,statistics
from pathlib import Path
root=Path.cwd();out=root/'.scratch/alias-train-20261006/pr283';sys.path.insert(0,str(root/'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi
api=RunnerApi('http://10.0.0.42:9368');status=api.json('/api/v1/status');assert not status.get('ProcessId') and not status['Queue']['Pending'] and not status['Queue']['Testing']
options=['-o','ControlPath='+str(root/'.scratch/pr283-bench-deck.sock'),'-o','BatchMode=yes'];target='deck@10.0.0.42';remote='/home/deck/xemu-performance/pr283-upload-qualification'
def ssh(cmd):return subprocess.run(['ssh',*options,target,cmd],capture_output=True,text=True,timeout=60)
r=ssh('mkdir -p '+remote);assert r.returncode==0,r.stderr
files=[out/'bench-A',out/'bench-B',Path('/lib/x86_64-linux-gnu/libshaderc.so.1')];r=subprocess.run(['scp',*options,*map(str,files),target+':'+remote+'/'],capture_output=True,text=True);assert r.returncode==0,r.stderr
rows=[]
try:
 for i,variant in enumerate('ABBABAAB'):
  r=ssh('ulimit -c 0; env LD_LIBRARY_PATH='+remote+' timeout 45 '+remote+'/bench-'+variant)
  (out/f'bench-deck-{i+1}-{variant}.log').write_text(r.stdout+r.stderr)
  assert r.returncode==0,(i,variant,r.stderr)
  matches=re.findall(r'PASS upload format=(\d+) scale=(\d+) pixels=(\d+) bufferToImage=(\d+) imageCopy=(\d+) blit=(\d+) host_us=(\d+)',r.stdout)
  assert len(matches)==256,len(matches)
  groups={}
  for fmt,scale,pixels,uploads,copies,blit,cost in matches:
   groups.setdefault((int(fmt),int(scale)),[]).append(dict(cost_us=int(cost),copies=int(copies),blits=int(blit)))
  for (fmt,scale),items in groups.items():
   assert len(items)==32
   warm=items[1:]
   rows.append(dict(run=i+1,variant=variant,format=fmt,scale=scale,warm_samples=31,median_us=statistics.median(x['cost_us'] for x in warm),mean_us=statistics.mean(x['cost_us'] for x in warm),image_copies=sorted({x['copies'] for x in warm}),blits=sorted({x['blits'] for x in warm})))
  print(i+1,variant,'PASS 256 upload/readback oracles',flush=True)
finally:
 r=ssh('rm -f '+remote+'/bench-A '+remote+'/bench-B '+remote+'/libshaderc.so.1');assert r.returncode==0,r.stderr
summary=[]
for key in sorted({(r['format'],r['scale']) for r in rows}):
 a=statistics.median(r['median_us'] for r in rows if (r['format'],r['scale'])==key and r['variant']=='A');b=statistics.median(r['median_us'] for r in rows if (r['format'],r['scale'])==key and r['variant']=='B');summary.append(dict(format=key[0],scale=key[1],parent_us=a,candidate_us=b,improvement_pct=100*(a-b)/a))
record=dict(order='ABBA BAAB',rows=rows,summary=summary,method='Production upload wall latency; first upload excluded per format/scale, 31 subsequent samples; per-run medians then median of four runs per variant',limits='Synthetic 32x32 upload costs, not FPS or game CPU; driver cache not reset. Both variants verified exact pixel oracles with explicitly counted inherited zero-depth canonicalization.')
(out/'bench-deck.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(summary,indent=2))
