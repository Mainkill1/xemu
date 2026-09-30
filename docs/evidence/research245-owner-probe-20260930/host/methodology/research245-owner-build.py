from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import datetime, hashlib, json, os, shutil, subprocess
ROOT=Path('/home/codex/src/steamdeck-xemu'); WT=ROOT/'worktrees/research245-jump-cache'
OUT=ROOT/'artifacts/research245-owner-probe'; HEAD=subprocess.check_output(['git','rev-parse','HEAD'],cwd=WT).decode().strip()
identity=ROOT/'scratch/research245-validity-build-identity.git'
subprocess.run(['git','--git-dir='+str(identity),'update-ref','refs/heads/main',HEAD],check=True)
def build(host):
    windows=host=='windows'; directory='build-windows-probe' if windows else 'build-probe'
    executable='qemu-system-i386.exe' if windows else 'qemu-system-i386'
    env=os.environ.copy();env['GIT_DIR']=str(identity);env['TMPDIR']=str(ROOT/'scratch/compiler-tmp')
    if windows:
        env['PATH']='/opt/xemu-toolchain/mxe/usr/bin:'+env['PATH'];env['VULKAN_SDK']='/opt/xemu-toolchain/mxe/usr/x86_64-w64-mingw32.static'
    checks=[]; binaries=[]
    for enabled in ([True,False] if windows else [False,True]):
        mode='enabled' if enabled else 'disabled'
        cmds=[[str(WT/directory/'pyvenv/bin/meson'),'configure',directory,'-Dxemu_tcg_jump_cache_probe='+str(enabled).lower()],['ninja','-j','12','-C',directory,executable]]
        for n,cmd in enumerate(cmds):
            log=OUT/f'{host}-{mode}-{n}.log'
            with log.open('wb') as f:p=subprocess.run(cmd,cwd=WT,env=env,stdout=f,stderr=subprocess.STDOUT)
            checks.append(dict(argv=cmd,exitCode=p.returncode,log=log.name));assert p.returncode==0,log
        package=ROOT/'artifacts'/f'research245-owner-{host}-{HEAD[:8]}-{mode}'
        shutil.copytree(ROOT/'artifacts'/f'research245-light-probe-{"windows" if windows else "deck"}-3a3d3c39',package,dirs_exist_ok=True)
        target=package/('xemu.exe' if windows else 'xemu');shutil.copy2(WT/directory/executable,target)
        binaries.append(dict(mode=mode,package=str(package),executableSha256=hashlib.sha256(target.read_bytes()).hexdigest(),bytes=target.stat().st_size))
        print(host,mode,'BUILT',binaries[-1]['executableSha256'],flush=True)
    receipt=dict(sourceCommit=HEAD,baseCommit=subprocess.check_output(['git','merge-base',HEAD,'origin/main'],cwd=WT).decode().strip(),host=host,checks=checks,binaries=binaries,utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    (OUT/f'{host}-build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(build,['deck','windows']))
