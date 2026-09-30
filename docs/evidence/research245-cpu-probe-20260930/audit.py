#!/usr/bin/env python3
"""Audit exact runner outcomes, guest source hashes and planned JIT windows."""
from pathlib import Path
from datetime import datetime
import hashlib, json, re
ROOT = Path(__file__).resolve().parent

def load(p):
    return json.loads(p.read_text())

def rows(text):
    parsed = {}
    for line in text.splitlines():
        if not line.startswith('jc ') or line.startswith('jc diagnostic:'):
            continue
        name = line.split()[1]
        if '=' in name:
            name = 'totals' if name.startswith('lookup_started') else 'targeted'
        target = parsed.setdefault(name, {})
        for key, value in re.findall(r'(\w+)=([\d,]+)', line):
            target[key] = [int(x) for x in value.split(',')] if key == 'occupancy' else int(value)
    return parsed


def main():
    identity = load(ROOT / 'input-identity.json')
    outcomes = {}
    names = {
        '20260930-151149660-90bb6726facc4b7ab1dc841f714ba0f1': ('Stable-code original procedure', 'incomplete', 'code_stable'),
        '20260930-151640365-5fa535de9f3144aa82fbbeff2c133f46': ('Stable-code corrected procedure', 'complete', 'code_stable'),
        '20260930-151740512-a75c05789bc44022a728a53904910590': ('Code-rewrite corrected procedure', 'complete', 'code_rewrite'),
    }
    assert {p.name for p in (ROOT/'runs').iterdir()} == set(names)
    for rid, (name, evidence, leaf) in names.items():
        run = ROOT/'runs'/rid
        result, manifest = load(run/'result.json'), load(run/'input-manifest.json')
        assert result['status'] == 'completed' and result['exitCode'] == 0
        assert (result['correctnessStatus'], result['evidenceStatus'], result['comparisonStatus']) == ('passed', evidence, 'ineligible')
        assert result['executableSha256'] == manifest['ExecutableSha256'] == identity['files']['xemu']
        for item in manifest['Inputs']:
            assert item['Verified'] and item['Sha256'] == item['ExpectedSha256'] == identity['files'][item['Path']]
        normalized = load(run/'guest/normalized-results.json')
        assert hashlib.sha256((run/'guest/results.txt').read_bytes()).hexdigest() == normalized['sourceSha256']
        record, = normalized['records']
        assert record['Id'] == 'cpu_translation_blocks.'+leaf and record['Correct'] and record['Samples'] == 10
        assert normalized['measurements'] == result['workload']['Measurements']
        coverage = load(run/'guest/xiso-coverage.json')
        assert coverage['complete'] and coverage['receiptMatches'] and coverage['selected'] == 1 and not coverage['missing'] and not coverage['extra']
        assert coverage['IsoSha256'] == '74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63'
        assert result['operatorActivity']['Diagnostics'] == 3 and result['operatorActivity']['Intervened']
        assert all(value == 0 for key,value in result['operatorActivity'].items() if key not in ('Diagnostics','Intervened'))
        snapshots = []
        for stage in ('early','middle','late'):
            path, = run.glob('diagnostics/*-jit-'+stage+'/monitor.txt')
            receipt = load(path.with_name('result.json'))
            assert receipt['Id'] == 'jit-'+stage and receipt['Status'] == 'completed'
            assert receipt in result['diagnostics']
            text = path.read_text()
            parsed = rows(text)
            assert parsed and int(re.search(r'^TB flush count\s+(\d+)',text,re.M)[1]) == 0
            snapshots.append((receipt,parsed,int(re.search(r'^TB invalidate count\s+(\d+)',text,re.M)[1])))
        windows = []
        for index in (0,1):
            before, after = snapshots[index:index+2]
            seconds=(datetime.fromisoformat(after[0]['StartedUtc'])-datetime.fromisoformat(before[0]['StartedUtc'])).total_seconds()
            delta = {}
            for category, fields in before[1].items():
                delta[category] = {}
                for key, value in fields.items():
                    if key == 'maximum_nonnull':
                        continue # Difference of cumulative maxima is not a window maximum.
                    new = after[1][category][key]
                    diff = [b-a for a,b in zip(value,new,strict=True)] if isinstance(value,list) else new-value
                    assert all(x>=0 for x in diff) if isinstance(diff,list) else diff>=0
                    delta[category][key]=diff
            pc=delta['pcrel_flush']
            assert pc['calls'] == after[2]-before[2]
            for category in ('pcrel_flush','other_flush'):
                clear=delta[category]
                assert sum(clear['occupancy']) == clear['calls'] and clear['slots'] == 4096*clear['calls']
            completed=sum(delta[x]['calls'] for x in ('hit','global_hit','global_miss'))
            windows.append({'from':before[0]['Id'],'to':after[0]['Id'],'approximateSeconds':seconds,'pcRelativeClears':pc['calls'],'clearsPerSecond':pc['calls']/seconds,'meanNonNullObservationsPerClear':pc['observed_nonnull']/pc['calls'],'completedLookups':completed,'startedMinusCompleted':delta['totals']['lookup_started']-completed,'delta':delta})
        outcomes[rid]={'name':name,'evidence':evidence,'comparison':'ineligible','measurementRecord':record,'windows':windows}
    (ROOT/'summary.json').write_text(json.dumps(outcomes,indent=2,sort_keys=True)+'\n')
    print('Verified three retained CPU outcomes, guest hashes, exact coverage and nine completed JIT snapshots; original incomplete evidence preserved.')

if __name__ == '__main__':
    main()
