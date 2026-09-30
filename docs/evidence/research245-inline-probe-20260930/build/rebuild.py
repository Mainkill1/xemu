from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib,json,os,shutil,subprocess
ROOT=Path('/home/codex/src/steamdeck-xemu');WT=ROOT/'worktrees/research245-jump-cache';OUT=ROOT/'artifacts/research245-inline-probe';OUT.mkdir(exist_ok=True)
HEAD=subprocess.check_output(['git','rev-parse','HEAD'],cwd=WT).decode().strip();identity=ROOT/'scratch/research245-validity-build-identity.git'
subprocess.run(['git','--git-dir='+str(identity),'update-ref','refs/heads/main',HEAD],check=True)
def build(host):
 windows=host=='windows';directory='build-windows-probe' if windows else 'build-probe';exe='qemu-system-i386.exe' if windows else 'qemu-system-i386'
 env=os.environ.copy();env['GIT_DIR']=str(identity);env['TMPDIR']=str(ROOT/'scratch/compiler-tmp')
 if windows:env['PATH']='/opt/xemu-toolchain/mxe/usr/bin:'+env['PATH'];env['VULKAN_SDK']='/opt/xemu-toolchain/mxe/usr/x86_64-w64-mingw32.static'
 commands=[]
 for i,cmd in enumerate([[str(WT/directory/'pyvenv/bin/meson'),'configure',directory,'-Dxemu_tcg_jump_cache_probe=true'],['ninja','-j12','-C',directory,exe]]):
  with (OUT/f'{host}-{i}.log').open('wb') as f:p=subprocess.run(cmd,cwd=WT,env=env,stdout=f,stderr=subprocess.STDOUT)
  commands.append(dict(argv=cmd,cwd=str(WT),exitCode=p.returncode));assert p.returncode==0
 package=ROOT/'artifacts'/f'research245-inline-{host}-{HEAD[:8]}';shutil.copytree(ROOT/'artifacts'/f'research245-light-probe-{"windows" if windows else "deck"}-3a3d3c39',package,dirs_exist_ok=True)
 binary=package/('xemu.exe' if windows else 'xemu');shutil.copy2(WT/directory/exe,binary)
 nm='/opt/xemu-toolchain/mxe/usr/bin/x86_64-w64-mingw32.static-nm' if windows else 'nm'
 symbols=subprocess.check_output([nm,'-S','--defined-only',str(binary)]).decode().splitlines();selected=[l for l in symbols if l.split()[-1] in ['tb_lookup','cpu_exec_loop']]
 assert not any(l.split()[-1]=='tb_lookup' for l in symbols),selected
 receipt=dict(sourceCommit=HEAD,parentCommit='458730bf53',host=host,enabled=True,commands=commands,package=str(package),executableSha256=hashlib.sha256(binary.read_bytes()).hexdigest(),symbols=selected)
 (OUT/f'{host}-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(host,'BUILT',HEAD[:8],receipt['executableSha256'],selected,flush=True)
with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(build,['deck','windows']))
