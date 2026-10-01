"""Compare selected instruction sequences while resolving relocated targets."""
from pathlib import Path
import bisect,json,re,subprocess,hashlib
R=Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply/control-regression');summary=json.loads((R/'layout-summary.json').read_text());maps={};reports=[];extras={}
for variant in ['parent','candidate']:
 symbols=[]
 for line in (R/(variant+'-symbols.txt')).read_text().splitlines():
  m=re.fullmatch(r'([a-f0-9]+) ([a-f0-9]+) ([a-zA-Z]) (.+)',line)
  if m:symbols.append((int(m[1],16),int(m[2],16),m[4]))
 symbols.sort(key=lambda x:(x[0],x[2]));maps[variant]=(symbols,[x[0] for x in symbols])
 section_rows={}
 for line in (R/(variant+'-sections.txt')).read_text().splitlines():
  m=re.search(r'\[\s*\d+\]\s+(\S+)\s+\S+\s+([a-f0-9]+)\s+([a-f0-9]+)\s+([a-f0-9]+)',line)
  if m:section_rows[m[1]]=tuple(int(m[i],16) for i in [2,3,4])
 exe=R.parent/f'native-builds/{variant}-linux/squashfs-root/usr/bin/xemu'
 relocations=subprocess.check_output(['readelf','-rW',str(exe)],text=True)
 imports=[m[1] for m in re.finditer(r'R_X86_64_JUMP_SLOT\s+[a-f0-9]+\s+(\S+)',relocations)]
 plt=section_rows['.plt'];assert plt[2]==16*(len(imports)+1)
 extras[variant]={'sections':section_rows,'bytes':exe.read_bytes(),'pltImports':imports}

def target(variant,address,self_start,self_size):
 if self_start<=address<self_start+self_size:return 'self+'+hex(address-self_start)
 symbols,addresses=maps[variant];i=bisect.bisect_right(addresses,address)-1
 if i>=0:
  a,size,name=symbols[i]
  if size and a<=address<a+size:return name+'+'+hex(address-a)
 extra=extras[variant];plt=extra['sections']['.plt']
 if plt[0]+16<=address<plt[0]+plt[2] and (address-plt[0])%16==0:
  return extra['pltImports'][(address-plt[0])//16-1]+'@plt'
 ro=extra['sections']['.rodata']
 if ro[0]<=address<ro[0]+ro[2]:
  offset=ro[1]+address-ro[0];blob=extra['bytes'][offset:offset+4096];end=blob.find(b'\0')
  if 0<end<4096:
   value=blob[:end]
   if all(x in [9,10,13] or 32<=x<=126 for x in value):
    return 'rodata-string:'+hashlib.sha256(value).hexdigest()
 return 'unresolved@'+hex(address)
def normalize(variant,row):
 name=row['symbol'];entry=row[variant];path=R/(variant+'-'+re.sub('[^a-zA-Z0-9_.-]','_',name)+'.asm');out=[]
 for line in path.read_text().splitlines():
  m=re.fullmatch(r'\s*([a-f0-9]+):\s+([a-z][a-z0-9.]*)\s*(.*)',line)
  if not m:continue
  address=int(m[1],16);assert entry['address']<=address<entry['address']+entry['size'];mnemonic=m[2];operands=m[3];comment=None
  if '#' in operands:operands,comment=operands.split('#',1)
  operands=re.sub(r'\s*<[^>]+>','',operands).strip()
  if mnemonic.startswith('j') or mnemonic.startswith('call'):
   branch=re.search(r'(?<![$\w])0x([a-f0-9]+)',operands)
   if branch:operands=operands[:branch.start()]+target(variant,int(branch[1],16),entry['address'],entry['size'])+operands[branch.end():]
  if '(%rip)' in operands and comment:
   ref=re.search(r'0x([a-f0-9]+)',comment)
   if ref:operands=re.sub(r'-?0x[a-f0-9]+\(%rip\)',target(variant,int(ref[1],16),entry['address'],entry['size'])+'(%rip)',operands)
  out.append(mnemonic+' '+operands)
 (R/(variant+'-'+re.sub('[^a-zA-Z0-9_.-]','_',name)+'.normalized.txt')).write_text('\n'.join(out)+'\n')
 return out
for row in summary['symbols']:
 a=normalize('parent',row);b=normalize('candidate',row);differences=[{'index':i,'parent':x,'candidate':y} for i,(x,y) in enumerate(zip(a,b)) if x!=y]
 report={'symbol':row['symbol'],'instructionCountParent':len(a),'instructionCountCandidate':len(b),'equalAfterTargetResolution':a==b,'addressDifference':row['addressDifference'],'parentStartMod64':row['parent']['address']%64,'candidateStartMod64':row['candidate']['address']%64,'differences':differences};reports.append(report)
(R/'cpu-assembly-comparison.json').write_text(json.dumps({'method':'Keep instructions/registers/constants; resolve branch/RIP targets by matching debug symbols, validated x86-64 PLT slots, or identical printable NUL-terminated rodata strings. Unknown targets stay literal. This is limited instruction-sequence comparison, not a whole-binary equivalence proof.','functions':reports},indent=2)+'\n')
for row in reports:print(row['symbol'],row['instructionCountParent'],row['instructionCountCandidate'],row['equalAfterTargetResolution'],'mod64',row['parentStartMod64'],row['candidateStartMod64'], 'differences',len(row['differences']))
