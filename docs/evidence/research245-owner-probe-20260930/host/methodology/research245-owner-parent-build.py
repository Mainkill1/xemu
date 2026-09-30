from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib,json,os,shutil,subprocess
ROOT=Path('/home/codex/src/steamdeck-xemu');WT=ROOT/'worktrees/research245-parent-458730bf';CAND=ROOT/'worktrees/research245-jump-cache';OUT=ROOT/'artifacts/research245-owner-probe'
HEAD=subprocess.check_output(['git','rev-parse','HEAD'],cwd=WT).decode().strip()
identity=ROOT/'scratch/research245-owner-parent-identity.git'
if not identity.exists():
    subprocess.run(['git','init','--bare',str(identity)],check=True,capture_output=True)
    common=subprocess.check_output(['git','rev-parse','--path-format=absolute','--git-common-dir'],cwd=WT).decode().strip()
    (identity/'objects/info/alternates').write_text(common+'/objects\n')
    subprocess.run(['git','--git-dir='+str(identity),'symbolic-ref','HEAD','refs/heads/main'],check=True)
    subprocess.run(['git','--git-dir='+str(identity),'update-ref','refs/heads/main',HEAD],check=True)
dependencies=[]
for p in (CAND/'subprojects').iterdir():
    target=WT/'subprojects'/p.name
    if p.is_dir() and not target.exists():
        target.symlink_to(p,target_is_directory=True);dependencies.append(dict(path=p.name,source=str(p),reuse='same downloaded dependency source as candidate'))
(OUT/'parent-dependency-reuse.json').write_text(json.dumps(dependencies,indent=2)+'\n')
def build(host):
    windows=host=='windows';directory=WT/('build-windows' if windows else 'build-native');directory.mkdir(exist_ok=True)
    env=os.environ.copy();env['GIT_DIR']=str(identity);env['TMPDIR']=str(ROOT/'scratch/compiler-tmp')
    if windows:env['PATH']='/opt/xemu-toolchain/mxe/usr/bin:'+env['PATH'];env['VULKAN_SDK']='/opt/xemu-toolchain/mxe/usr/x86_64-w64-mingw32.static'
    configure=['../configure','--target-list=i386-softmmu','--disable-werror','--extra-cflags=-DXBOX','--extra-cxxflags=-DXBOX']
    if windows:configure+=['--cross-prefix=x86_64-w64-mingw32.static-','--static']
    exe='qemu-system-i386.exe' if windows else 'qemu-system-i386';commands=[]
    for i,cmd in enumerate([configure,['ninja','-j','12',exe]]):
        with (OUT/f'parent-{host}-{i}.log').open('wb') as f:p=subprocess.run(cmd,cwd=directory,env=env,stdout=f,stderr=subprocess.STDOUT)
        commands.append(dict(argv=cmd,cwd=str(directory),exitCode=p.returncode));print('PARENT',host,i,p.returncode,flush=True)
        assert p.returncode==0
    package=ROOT/'artifacts'/f'research245-owner-{host}-{HEAD[:8]}-parent'
    shutil.copytree(ROOT/'artifacts'/f'research245-light-probe-{"windows" if windows else "deck"}-3a3d3c39',package,dirs_exist_ok=True)
    binary=package/('xemu.exe' if windows else 'xemu');shutil.copy2(directory/exe,binary)
    (OUT/f'parent-{host}-receipt.json').write_text(json.dumps(dict(sourceCommit=HEAD,commands=commands,package=str(package),executableSha256=hashlib.sha256(binary.read_bytes()).hexdigest()),indent=2)+'\n')
with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(build,['deck','windows']))
