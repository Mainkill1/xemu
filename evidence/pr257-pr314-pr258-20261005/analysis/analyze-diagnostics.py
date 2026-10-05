"""Bind native diagnostics to retained runner records; never waive missing proof."""
import csv
import hashlib
import importlib.util
import json
import ntpath
import posixpath
import statistics
from pathlib import Path

p = Path(__file__).resolve().parent
d = p / 'diagnostics'
spec = importlib.util.spec_from_file_location('admission', Path.cwd() / 'xemu-pr94-stack/scripts/validate-shortcut-comparison.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

def pin(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def camel(value):
    if isinstance(value, dict):
        return {k[0].lower() + k[1:]: camel(v) for k, v in value.items()}
    if isinstance(value, list):
        return [camel(v) for v in value]
    return value

reports = {}
all_rows = []
for host in ['win', 'deck']:
    outcome_file = d / (host + '-outcomes.json')
    if not outcome_file.exists():
        continue
    template = json.loads((d / (host + '-template.json')).read_text())
    procedure = {k: template[k] for k in ['originalTestId', 'originalRevision', 'originalJob']}
    catalog = {'schema': 'native-procedure-catalog/v1', 'tests': [template['originalTestId']], 'procedure': procedure}
    (d / (host + '-native-catalog.json')).write_text(json.dumps(catalog, indent=2))
    runs = []
    rows = []
    for cell in json.loads(outcome_file.read_text()):
        rid = cell['result'].get('runId')
        if not rid:
            continue
        folder = d / (host + '-evidence') / rid
        if not (folder / 'shortcut-evidence.json').exists():
            continue
        evidence = json.loads((folder / 'shortcut-evidence.json').read_text())
        result = json.loads((folder / 'result.json').read_text())
        job = json.loads((folder / 'job.json').read_text())
        manifest = json.loads((folder / 'input-manifest.json').read_text())
        state = json.loads((folder / 'diagnostics/run-state/report.json').read_text())
        assert camel(job['Plan']) == template['originalJob']['plan'], rid
        assert evidence['input_sha256'] == template['planSha256'], rid
        assert evidence['session_id'] == rid and result['executableSha256'] == evidence['executable_sha256'], rid
        assert evidence['order'] == ('ABBA' if cell['cell'] <= 4 else 'BAAB')
        assert evidence['position'] == (cell['cell'] - 1) % 4 + 1
        path_api = ntpath if host == 'win' else posixpath
        report_path = state.get('EffectiveConfigPath')
        if report_path:
            result_directory = path_api.dirname(path_api.dirname(path_api.dirname(report_path)))
        else:
            # Launch argv contains the canonical result path, even when Windows
            # has no run-state-managed input catalog. Do not invent its proof.
            argv = result['arguments']
            evidence_path = argv[argv.index('-xemu-shortcut-evidence') + 1]
            result_directory = path_api.dirname(evidence_path)
        bindings = {}
        proofs = []
        for role, path in evidence['input_paths'].items():
            bound = {'path': path, 'sha256': None, 'verified': False}
            if path:
                for resource in state.get('ReadOnlyAssets', []):
                    if resource['Field'] == role + '_path' and resource['Path'] == path:
                        bound.update(sha256=resource['ExpectedSha256'], verified=resource['BeforeSha256'] == resource['AfterSha256'] == resource['ExpectedSha256'])
                        proofs.append({'role': role, 'source': 'run-state ReadOnlyAssets before/after', 'assetId': resource['AssetId']})
                if role in ['eeprom', 'hdd']:
                    for seed in manifest['RuntimeFiles']:
                        actual = path_api.join(result['runtimeDirectory'], seed['Destination'])
                        if path_api.normcase(path_api.normpath(actual)) == path_api.normcase(path_api.normpath(path)):
                            bound.update(sha256=seed['Sha256'], verified=True)
                            proofs.append({'role': role, 'source': 'canonical RuntimeFiles seed and exact launch path', 'destination': seed['Destination']})
                if role == 'eeprom':
                    for source in manifest['Inputs']:
                        if source['Path'] == path and source.get('Verified') is True:
                            bound.update(sha256=source['Sha256'], verified=True)
                            proofs.append({'role': role, 'source': 'canonical package input, exact relative startup path'})
            bindings[role] = bound
        requested_tests = [template['originalTestId']]
        actual_tests = requested_tests if result['status'] == 'completed' else []
        sidecar = {
            'run_id': rid, 'session_id': evidence['session_id'],
            'executable_sha256': result['executableSha256'],
            'input_sha256': template['planSha256'],
            'result_directory': result_directory,
            'screenshot_directory': evidence['screenshot_directory'],
            'resource_bindings': bindings,
            'comparison_eligible': result['comparisonStatus'] == 'eligible',
            'correctness': result['correctnessStatus'],
            'requested_tests': requested_tests, 'actual_tests': actual_tests,
            'baseline_sha256': evidence['executable_sha256'],
            'procedure_revision': pin(procedure),
            'catalog_sha256': pin(catalog),
            'guest_settings_sha256': evidence['comparison_config_sha256'],
            'cache_policy': {k: state.get(k) for k in ['CacheMode', 'CacheShaders', 'DriverCache', 'DriverNamespaceVerified', 'DriverQualification', 'ComparisonReady']},
            'start_policy': {'snapshot': job['SnapshotName'], 'startPaused': job['StartPaused'], 'requireInput': job['RequireInput']},
        }
        (folder / 'runner-sidecar.json').write_text(json.dumps(sidecar, indent=2))
        (folder / 'derivation.json').write_text(json.dumps({'schema': 'native-runner-binding/v1', 'actualSavedRevision': cell['revision'], 'actualJobManifestSha256': manifest['JobManifestSha256'], 'sharedProcedureMeaning': 'Original registered native job and revision; actual serialized guest plan compared exactly. Per-cell diagnostic argv/revisions retained separately.', 'catalogMeaning': 'One native procedure, not an XISO leaf catalog.', 'requestedActualMeaning': 'Declared native procedure and verified plan completion; not guest acknowledgment of each input.', 'resourceProofs': proofs, 'unverifiedRoles': [k for k,v in bindings.items() if v['path'] and not v['verified']], 'outputProof': 'Canonical launch argument or run-state effective config under this result directory'}, indent=2))
        evidence['runner'] = sidecar
        runs.append(evidence)
        counters = evidence['counters']
        eligible = counters['vk.texture.clean_stage_eligible']
        assert eligible == counters['vk.texture.clean_stage_skips'] + counters['vk.texture.clean_stage_forced_reference']
        assert counters['vk.texture.mixed_policy_intervals'] == counters['vk.texture.mixed_perf_intervals'] == 0
        assert counters['vk.texture.clean_stage_skips' if cell['policy'] == 'disabled' else 'vk.texture.clean_stage_forced_reference'] == 0
        row = {'host': host, 'cell': cell['cell'], 'policy': cell['policy'], 'runId': rid, 'complete': evidence['complete'], 'eligibleStages': eligible, 'skips': counters['vk.texture.clean_stage_skips'], 'forcedReference': counters['vk.texture.clean_stage_forced_reference'], 'windowMs': evidence['wall_interval_ns'] / 1e6, 'runnerComparison': result['comparisonStatus']}
        perf_path = folder / 'performance.json'
        if perf_path.exists():
            a = json.loads(perf_path.read_text())['analysis']
            intervals = a['frames']['intervalsMs']
            row.update(cpu=a['monitoring']['cpu']['mean'], guestFlipCadence=a['flips']['cadenceFps'], meanMs=intervals['mean'], p95Ms=intervals['p95'], p99Ms=intervals['p99'], maxMs=intervals['max'])
        rows.append(row)
    identity = {k: runs[0][k] for k in ['commit', 'executable_sha256']} if runs else {}
    contract = {'schema': 'xemu-shortcut-comparison/v1', 'kind': 'setting_ab', 'reference': identity, 'candidate': identity, 'policy_changes': {'vk_skip_clean_texture_stages': ['auto', 'disabled']}, 'effective_changes': {'vk_skip_clean_texture_stages': ['enabled', 'disabled']}, 'required_counters': ['vk.texture.clean_stage_eligible', 'vk.texture.dirty_range_checks', 'vk.texture.cache_walks']}
    admission = validator.validate(contract, runs)
    (d / (host + '-admission.json')).write_text(json.dumps(admission, indent=2))
    report = {'schema': 'native-setting-diagnostic/v1', 'host': host, 'rows': rows, 'admission': admission, 'counterBoundary': 'NV097 SET_FLIP_STALL method, not completed flip/present/input acknowledgment', 'metrics': {}}
    if len(rows) == 8 and all('cpu' in row for row in rows):
        for key in ['cpu', 'guestFlipCadence', 'meanMs', 'p95Ms', 'p99Ms', 'maxMs', 'windowMs']:
            before = statistics.median(r[key] for r in rows if r['policy'] == 'auto')
            after = statistics.median(r[key] for r in rows if r['policy'] == 'disabled')
            report['metrics'][key] = {'auto': before, 'disabled': after, 'disabledVsAutoPercent': 100*(after-before)/before}
    report['timingMeaning'] = 'Descriptive canonical per-run medians, CPU one core=100%; no timing verdict from counter admission. Owner-window and scene-monitoring intervals differ.'
    (d / (host + '-comparison.json')).write_text(json.dumps(report, indent=2))
    reports[host] = report
    all_rows.extend(rows)
    print(host, len(rows), 'cells;', 'admission eligible:', admission['eligible'], '; errors:', admission['errors'][:8])
    print(json.dumps(report['metrics']))
if all_rows:
    fields = sorted(set().union(*(r.keys() for r in all_rows)))
    with (d / 'current-counter-results.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields);writer.writeheader();writer.writerows(all_rows)
