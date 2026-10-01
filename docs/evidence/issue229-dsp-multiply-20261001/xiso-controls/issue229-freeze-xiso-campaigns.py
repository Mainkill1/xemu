"""Freeze ABBA and reversed-reference BAAB CPU control campaigns; no start."""
from pathlib import Path
import json,subprocess,sys
R=Path('/home/codex/src/steamdeck-xemu');scripts=R/'worktrees/runner-plan-timeout/scripts';sys.path.insert(0,str(scripts))
from runner_transport import RunnerApi
api=RunnerApi('http://10.0.0.123:9368');out=R/'artifacts/issue229-dsp-multiply/xiso-preparation'
for renderer in ['vulkan','opengl']:
 for order in ['abba','baab']:
  identity=f'issue229-deck-cpu-{renderer}-{order}-v1'
  app,ref=('candidate','parent') if order=='abba' else ('parent','candidate')
  request={'id':identity,'suite':f'issue229-deck-xiso-cpu-{renderer}-v1-suite','application':'issue229-'+app+'-native-linux-v1','referenceApplication':'issue229-'+ref+'-native-linux-v1','tests':['cpu_floating_point.sse_scalar','cpu_translation_blocks.direct_loop']}
  # Use the maintained selector's request schema and its server-frozen ordering.
  cmd=[sys.executable,str(scripts/'runner_xiso.py'),'--url','http://10.0.0.123:9368','select',request['application'],'--id',identity,'--suite',request['suite'],'--reference',request['referenceApplication']]
  for test in request['tests']:cmd.extend(['--test',test])
  created=json.loads(subprocess.check_output(cmd,text=True));(out/f'{renderer}-{order}-created.json').write_text(json.dumps(created,indent=2)+'\n')
  plan=api.json('/api/v1/xiso-campaigns/'+identity+'?view=plan');(out/f'{renderer}-{order}-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
  actual=['A' if x['application']=='issue229-parent-native-linux-v1' else 'B' for x in plan['attempts']]
  assert ''.join(actual)==order.upper(),actual
  assert plan['tests']==request['tests'] and not plan['addedDependencies']
  assert plan['settings']=={'warmup_iterations':3,'measurement_iterations_multiplier':4,'gpu_completion_mode':'per_iteration'}
  assert all(not x['runtimeState']['isolation']['allowUncontrolledDriverCache'] for x in plan['chunks'])
  print(identity,''.join(actual),len(plan['attempts']),'attempts',flush=True)
