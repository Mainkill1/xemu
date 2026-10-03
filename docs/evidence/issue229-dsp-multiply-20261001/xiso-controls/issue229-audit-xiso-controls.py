"""Audit all recorded #229 CPU control attempts, without recomputing comparisons."""
from pathlib import Path
import csv,hashlib,json
R=Path('/home/codex/src/steamdeck-xemu/artifacts/issue229-dsp-multiply');C=R/'xiso-controls';P=R/'xiso-preparation'
expected={'parent':'4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a','candidate':'84ac46913180a55b2fce18cf304fd820d3ff21a828477f7b73c0136401e17418'}
reference_path=R.parent/'research245-cpu-main-2d289cb-deck-v3/cpu-code-rewrite-suite-reference.json'
assert hashlib.sha256(reference_path.read_bytes()).hexdigest()=='fe7ea361d2427e81e58e6addfd745f037023497bebd9ea59bf68e90dbc13848f'
reference={r['id']:r for r in json.loads(reference_path.read_text())};selected=['cpu_floating_point.sse_scalar','cpu_translation_blocks.direct_loop'];records=[];fixed={}
for renderer in ['vulkan','opengl']:
 for order in ['abba','baab']:
  id=f'issue229-deck-cpu-{renderer}-{order}-v1';directory=C/id
  status=json.loads((directory/'status.json').read_text());assert status['terminal'] and status['passed']==status['comparisonEligible']==status['attemptCount']==4
  plan=json.loads((directory/'plan.json').read_text());attempts=json.loads((directory/'attempts.json').read_text())['items']
  for declaration,attempt in zip(plan['attempts'],attempts):
   variant='parent' if declaration['application']=='issue229-parent-native-linux-v1' else 'candidate';d=directory/attempt['runId'];result=json.loads((d/'result.json').read_text());assessment=json.loads((d/'assessment.json').read_text());manifest=json.loads((d/'input-manifest.json').read_text());storage=json.loads((d/'diagnostics/run-state/report.json').read_text())
   assert result['status']=='completed' and result['exitCode']==0
   assert assessment['Correctness']=='passed' and assessment['Evidence']=='complete' and assessment['Comparison']=='eligible'
   assert result['executableSha256']==manifest['ExecutableSha256']==expected[variant]
   assert result['host']['runnerVersion']=='0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e'
   assert hashlib.sha256((d/'job.json').read_bytes()).hexdigest()==manifest['JobManifestSha256']
   assert not result['operatorActivity']['Intervened']
   assert storage['Schema']==2 and storage['DriverNamespaceVerified'] and storage['ComparisonReady'] and storage['TargetStopped'] and not storage['AllowUncontrolledDriverCache'] and not storage['Issues']
   fixed_inputs={r['Path']:r['Sha256'] for r in manifest['Inputs'] if r['Path']!='xemu'}
   if renderer in fixed:assert fixed_inputs==fixed[renderer]
   else:fixed[renderer]=fixed_inputs
   coverage=json.loads((d/'guest/xiso-coverage.json').read_text());assert coverage['complete'] and coverage['receiptMatches'] and not coverage['missing'] and not coverage['extra'] and coverage['selected']==2
   raw=json.loads((d/'guest/results.txt').read_text());assert [r['id'] for r in raw]==selected
   normalized=json.loads((d/'guest/normalized-results.json').read_text());assert hashlib.sha256((d/'guest/results.txt').read_bytes()).hexdigest()==normalized['sourceSha256']
   for r in raw:
    assert r['outcome']=='PASS' and r['kind']=='leaf' and len(r['raw_results'])==r['sample_count']==10
    for k in ['revision','iterations','sample_count','measurement_iterations_multiplier','warmup_iterations','gpu_completion_mode','framebuffer_fnv1a64']:assert r[k]==reference[r['id']][k],(attempt['runId'],r['id'],k)
   records.append({'renderer':renderer,'order':order.upper(),'variant':variant,'attempt':attempt['id'],'runId':attempt['runId'],'executableSha256':result['executableSha256'],'assessment':{k:assessment[k] for k in ['Execution','Correctness','Evidence','Comparison']},'leafTimings':normalized['records'],'storageQualification':storage['DriverQualification'],'driverFilesBefore':len(storage['DriverBefore']['Files']),'driverFilesAfter':len(storage['DriverAfter']['Files']),'driverBytesAfter':storage['DriverAfter']['Bytes']})
assert len(records)==16
(P/'attempts-audit.json').write_text(json.dumps({'attempts':16,'leafRecords':32,'allExpectedHashesAndSettingsVerified':True,'fixedInputsPerRenderer':fixed,'records':records},indent=2)+'\n')
print('Verified 16 completed/eligible attempts, 32 exact leaf oracles, all fixed inputs and private Mesa state.')
