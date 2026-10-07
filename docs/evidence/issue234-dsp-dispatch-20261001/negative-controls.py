import pathlib,shutil,subprocess,json
r=pathlib.Path('/home/codex/src/steamdeck-xemu');a=r/'artifacts/issue234-dsp-dispatch';source=r/'worktrees/issue234-dsp-dispatch/hw/xbox/mcpx/apu/dsp'
results=[]
for label,old,new,test in [('no-invalidate','dsp->pram_opcache[address] = NULL;','/* mutation: omit P-write invalidation */','overwrite'),('wrong-handler','handler(dsp);','emu_nop(dsp);','overwrite')]:
 target=a/(label+'-source');shutil.copytree(source,target,dirs_exist_ok=True);f=target/'interp/dsp_cpu.c';s=f.read_text();assert s.count(old)==1;f.write_text(s.replace(old,new))
 subprocess.run(['python3',str(r/'scratch/issue234-build.py'),label,str(f)],check=True)
 p=subprocess.run([str(a/label),'-p','/dsp/dispatch/'+test],capture_output=True,text=True)
 (a/(label+'-unit.log')).write_text(p.stdout+p.stderr)
 assert p.returncode!=0 and 'assertion failed' in p.stdout+p.stderr
 results.append({'mutation':label,'test':test,'returncode':p.returncode})
(a/'negative-controls.json').write_text(json.dumps(results,indent=2)+'\n');print(results)
