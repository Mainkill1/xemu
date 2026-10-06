import json, subprocess, statistics
from pathlib import Path
p=Path(__file__).resolve().parent
rows=[]
for payload in ['mono-adpcm','stereo-adpcm','mono-pcm']:
 for workers in [1,4]:
  for n,variant in enumerate(['baseline','candidate','candidate','baseline','candidate','baseline','baseline','candidate'],1):
   command=[str(p/(variant+'-throughput')),'--benchmark-v3','sinc',payload,'4000',str(workers)]
   r=subprocess.run(command,capture_output=True,text=True,check=True)
   (p/f'{payload}-w{workers}-{n}-{variant}.log').write_text(r.stdout+r.stderr)
   values=[json.loads(l) for l in r.stdout.splitlines() if l.startswith('{')]
   assert len(values)==1, r.stdout
   rows.append(dict(variant=variant,order=n,payload=payload,workers=workers,command=command,result=values[0]))
   (p/'benchmark.json').write_text(json.dumps(rows,indent=2))
  print(payload,workers,[r['result'] for r in rows[-2:]],flush=True)
