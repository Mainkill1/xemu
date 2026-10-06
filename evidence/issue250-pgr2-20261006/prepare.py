from pathlib import Path
import json,sys
root=Path.cwd();p=Path(__file__).resolve().parent/'native/a1'
sys.path.insert(0,str(root/'xemu-test-runner-upload-config/scripts'))
from runner_transport import RunnerApi
api=RunnerApi('http://10.0.0.42:9368')
s=api.json('/api/v1/status');assert not s.get('ProcessId') and not s['Queue']['Pending'] and not s['Queue']['Testing']
selected=next(r for r in json.loads((root/'.scratch/pr296-native/deck-selected-procedures.json').read_text()) if 'pgr2' in r['id'])
original=api.json('/api/v1/test-configs/'+selected['id']+'/'+selected['revision'])
(p/'original.json').write_text(json.dumps(original,indent=2))
app=json.loads((root/'.scratch/pr296-current-20261006/native/parent-apps.json').read_text())['deck-parent-pgr2']
app['scope']='Current-main PGR2 Vulkan hot-path attribution only; not matched game performance qualification.'
(p/'deck-app.json').write_text(json.dumps(app,indent=2))
d=json.loads(json.dumps(original['definition']));d.pop('files',None);d.pop('id',None)
identity='main-deck-pgr2-vk-attribution-20261006-v1';j=d['job'];j['id']=identity
j['expectedExecutableSha256']=app['sha256'];j['environment']['XEMU_VK_PERF_LOG']='{resultDir}/vk-perf.ndjson'
j['operations']=dict(mode='diagnostic',allowManualInput=False,allowPauseResume=False,allowDiagnostics=True,allowPreview=True)
j['experiment']=dict(id='pgr2-main-vk-attribution',variant='main',allowOperatorIntervention=False)
for item in j['inputs']:
 if item['path']=='xemu':item['expectedSha256']=app['sha256']
 if item['path']=='xemu.toml':item['expectedSha256']=app['configSha256']
j['workload']['evidenceRequirements'].append(dict(name='vulkan-attribution',scope='result',path='vk-perf.ndjson',mustExist=True,minimumBytes=100))
d['description']='Main Vulkan attribution only. Original PGR2 controller/inputs/waits/timeout/runtime state/measurement boundary unchanged. Known initial flyover prevents stationary comparison; this is no A/B performance claim.'
for field in ['plan','controllerInput','timeoutSeconds','runtimeState']:
 assert j.get(field)==original['definition']['job'].get(field),field
(p/'definition.json').write_text(json.dumps(d,indent=2))
saved=api.json('/api/v1/test-configs/'+identity,'POST',d)
(p/'deck-saved.json').write_text(json.dumps(saved,indent=2));print(saved['id'],saved['revision'])
