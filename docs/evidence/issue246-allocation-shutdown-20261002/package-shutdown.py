"""Publish one failed collector diagnostic, exact raw hashes and its local correction.

Cache payloads and local symbol symlinks are omitted explicitly. Run once into
an exclusive destination; original runner outcomes are never rewritten.
"""
from pathlib import Path
import gzip,hashlib,json,tarfile
r=Path('/home/codex/src/steamdeck-xemu');w=r/'worktrees/issue246-allocation-attribution'
p=w/'docs/evidence/issue246-allocation-shutdown-20261002';p.mkdir(parents=True,exist_ok=False)
b=r/'artifacts/issue246-texture-allocation/native-collector/morrowind-v1'
receipt=json.loads((b/'collection.json').read_text());assert receipt['complete'] and receipt['excluded']==0
included=[];omitted=[]
for f in sorted(b.rglob('*')):
 rel=f.relative_to(b)
 if 'symfs' in rel.parts:continue
 assert not f.is_symlink()
 if not f.is_file():continue
 row={'path':'native/'+str(rel),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
 if 'state' in rel.parts:omitted.append(row)
 else:included.append((f,row))
a=r/'artifacts/issue246-texture-allocation/trace-unit'
for f in sorted(a.iterdir()):
 if f.suffix in ['.log','.json','.jsonl']:
  included.append((f,{'path':'local/'+f.name,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}))
c=r/'artifacts/issue246-texture-allocation/native-builds/candidate-linux'
for name in ['build-identity.json','library-comparison.json','ci-unit.log','ci-linux-release.log']:
 f=c/name;included.append((f,{'path':'build/'+name,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}))
with (p/'records.tar.gz').open('xb') as output:
 with gzip.GzipFile(fileobj=output,mode='wb',mtime=0) as comp:
  with tarfile.open(fileobj=comp,mode='w') as t:
   for f,row in included:
    info=t.gettarinfo(str(f),arcname=row['path']);info.mtime=info.uid=info.gid=0;info.uname=info.gname=''
    with f.open('rb') as stream:t.addfile(info,stream)
with tarfile.open(p/'records.tar.gz') as t:
 assert len(t.getmembers())==len(included)
 for _,row in included:
  assert hashlib.file_digest(t.extractfile(row['path']),'sha256').hexdigest()==row['sha256']
  assert t.getmember(row['path']).size==row['bytes']
(p/'records-inventory.json.gz').write_bytes(gzip.compress((json.dumps({'included':[x for _,x in included],'omittedCachePayloads':omitted,'omission':'Cache payloads retained local/server-side; symfs links omitted; original diagnostic ZIP/profiles/JSONL/captures/outcomes included'},indent=2)+'\n').encode(),mtime=0))
source=['hw/xbox/nv2a/pgraph/vk/renderer.c','hw/xbox/nv2a/pgraph/vk/texture-allocation-trace.c','hw/xbox/nv2a/pgraph/vk/texture-allocation-trace.h','scripts/performance/vk-texture-allocation-summary.py','tests/unit/test-xbox-vk-texture-allocation-trace.c','tests/unit/test-vk-texture-allocation-summary.py']
(p/'tested-source-inventory.json').write_text(json.dumps([{'path':f,'sha256':hashlib.sha256((w/f).read_bytes()).hexdigest()} for f in source],indent=2)+'\n')
row={'archiveBytes':(p/'records.tar.gz').stat().st_size,'archiveSha256':hashlib.sha256((p/'records.tar.gz').read_bytes()).hexdigest(),'payloadFilesRehashed':len(included),'omittedCacheFiles':len(omitted),'originalCollection':receipt,'nativeCollectorCommit':'0ef8d9bf5c8dbf9b09895827ea615a3a789a82c0','nativeCollectorFullValidation':'FAIL: Missing final lifecycle summary','localCorrection':'Uncommitted source hashes included; publishing commit binds current correction','localChecks':'Product build, 27 focused C subtests, new reader11, prior reader11, actual production checkpoint writer->reader passed','nativeCorrection':'Not yet run','performanceComparison':'Not measured'}
(p/'verification.json').write_text(json.dumps(row,indent=2)+'\n')
(p/'package-shutdown.py').write_bytes(Path(__file__).read_bytes())
print(json.dumps(row,indent=2))
