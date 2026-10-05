import csv
import json
import math
import statistics
from pathlib import Path

p = Path(__file__).resolve().parent
reference = json.loads((Path.cwd() / '.scratch/pr256-qualification-20261002/packages/windows/reference-results.txt').read_text())
oracle = {r['id']: r for r in reference}


def read_json(path, default):
    return json.loads(path.read_text()) if path.exists() else default


def raw_records(path):
    if not path.exists():
        return [], False
    text = path.read_text()
    try:
        return json.loads(text), True
    except json.JSONDecodeError:
        records = []
        position = text.index('[') + 1
        decoder = json.JSONDecoder()
        while position < len(text):
            while position < len(text) and (text[position].isspace() or text[position] == ','):
                position += 1
            try:
                item, position = decoder.raw_decode(text, position)
            except json.JSONDecodeError:
                break
            records.append(item)
        return records, False


summary = {}
comparisons = []
leaf_rows = []
attempt_rows = []
for host in ['deck', 'win']:
    variants = {}
    for side in ['parent', 'candidate']:
        version = 'v2' if host == 'deck' else 'v1'
        name = f'p258-{host}-vk-full-{side}-{version}'
        plan = read_json(p / (name + '-plan.json'), {})
        attempts = read_json(p / (name + '-attempts.json'), [])
        resumed = []
        if host == 'deck' and side == 'candidate':
            recovery = read_json(p / 'deck-missing-work-mapping.json', {})
            if recovery:
                remap = {r['resumeChunk']: r for r in recovery['mapping']}
                resumed_plan = read_json(p / (recovery['resumeCampaign'] + '-plan.json'), {})
                frozen_chunks = {r['index']: r for r in resumed_plan['chunks']}
                for item in read_json(p / (recovery['resumeCampaign'] + '-attempts.json'), []):
                    original = remap[item['chunk']]
                    assert frozen_chunks[item['chunk']]['planId'] == original['planId']
                    resumed.append({**item, 'chunk': original['originalChunk'],
                                    'resumedCampaign': recovery['resumeCampaign'],
                                    'resumedChunk': item['chunk']})
        by_chunk = {a['chunk']: a for a in attempts}
        for a in resumed:
            by_chunk[a['chunk']] = a
        selected = plan['tests']
        leaves = {}
        failures = []
        wall_ms = 0
        for chunk, attempt in sorted(by_chunk.items()):
            rid = attempt.get('runId')
            if not rid:
                continue
            folder = p / f'stack-{host}-xiso-vk-evidence' / rid
            result = read_json(folder / 'result.json', {})
            if not result:
                continue
            raw, complete = raw_records(folder / 'guest/results.txt')
            checks = {c['Name'][6:]: c for c in read_json(folder / 'assessment.json', {}).get('Checks', []) if c['Name'].startswith('guest:')}
            execution = result['status']
            wall_ms += result['durationMs']
            if execution != 'completed':
                failures.append(dict(chunk=chunk, runId=rid, exitCode=result.get('exitCode'), detail=result.get('detail')))
            attempt_rows.append(dict(host=host, side=side, chunk=chunk, runId=rid,
                                     processWallMs=result['durationMs'], execution=execution,
                                     rawResultsComplete=complete,
                                     recoveredLeafCount=sum(r.get('kind') == 'leaf' for r in raw)))
            for r in raw:
                if r.get('kind') != 'leaf':
                    continue
                key = r['id']
                assert key in selected and key not in leaves
                samples = r.get('raw_results', [])
                timing = statistics.mean(samples) if samples else None
                ordered = sorted(samples)
                metadata = r.get('metadata') or {}
                row = dict(host=host, side=side, test=key, runId=rid,
                           meanUs=timing,
                           minSampleMeanUs=min(samples) if samples else None,
                           medianSampleMeanUs=statistics.median(samples) if samples else None,
                           p95SampleMeanUs=ordered[math.ceil(len(ordered)*.95)-1] if ordered else None,
                           p99SampleMeanUs=ordered[math.ceil(len(ordered)*.99)-1] if ordered else None,
                           maxSampleMeanUs=max(samples) if samples else None,
                           guestTotalWorkUs=r.get('total_us'),
                           sharedChunkProcessWallMs=result['durationMs'],
                           samples=len(samples), rawResultsComplete=complete,
                           guestOutcome=r.get('outcome'),
                           runnerCheck=('passed' if checks[key]['Passed'] else 'failed') if key in checks else 'not evaluated',
                           oraclePresent=key in oracle,
                           oracleApplicable=metadata.get('oracle_applicable'),
                           framebufferComparisonEligible=metadata.get('framebuffer_comparison_eligible'),
                           framebufferComparisonReason=metadata.get('framebuffer_comparison_reason'),
                           framebuffer=r.get('framebuffer_fnv1a64'),
                           execution=execution,
                           fixedWork={k: r.get(k) for k in ['revision', 'iterations', 'sample_count', 'measurement_iterations_multiplier', 'warmup_iterations', 'gpu_completion_mode', 'unit', 'direction']})
                leaves[key] = row
                leaf_rows.append(row)
        variants[side] = leaves
        summary.setdefault(host, {})[side] = dict(
            selectedLeaves=len(selected), receivedLeaves=len(leaves),
            missingLeaves=sorted(set(selected) - set(leaves)),
            unexecutedOriginalAttempts=[{'chunk': a['chunk'], 'request': a['id'],
                                        'runId': a.get('runId'), 'state': a['state'],
                                        'execution': a.get('execution'),
                                        'correctness': a.get('correctness'),
                                        'error': a.get('error')}
                                       for a in attempts if not a.get('runId')],
            resumedChunks=[{'originalChunk': a['chunk'],
                            'resumeChunk': a['resumedChunk'],
                            'campaign': a['resumedCampaign'],
                            'runId': a.get('runId')} for a in resumed],
            processFailures=failures, summedProcessWallMs=wall_ms,
            summedGuestWorkUs=sum(r['guestTotalWorkUs'] or 0 for r in leaves.values()),
            guestNonPass=[r['test'] for r in leaves.values() if r['guestOutcome'] != 'PASS'],
            missingOracles=[r['test'] for r in leaves.values() if not r['oraclePresent']],
            runnerFailed=[r['test'] for r in leaves.values() if r['runnerCheck'] == 'failed'])
    for test in selected:
        a = variants['parent'].get(test)
        b = variants['candidate'].get(test)
        matched = bool(a and b and a['fixedWork'] == b['fixedWork'])
        before = a['meanUs'] if a else None
        after = b['meanUs'] if b else None
        comparisons.append(dict(host=host, backend='Vulkan', test=test,
                                beforeUs=before, afterUs=after,
                                differenceUs=after-before if before is not None and after is not None else None,
                                improvementPercent=100*(before-after)/before if before and after is not None and matched else None,
                                fixedWorkSame=matched,
                                beforeGuestOutcome=a['guestOutcome'] if a else 'unavailable',
                                afterGuestOutcome=b['guestOutcome'] if b else 'unavailable',
                                beforeRunnerCheck=a['runnerCheck'] if a else 'unavailable',
                                afterRunnerCheck=b['runnerCheck'] if b else 'unavailable',
                                framebufferEqual=a['framebuffer']==b['framebuffer'] if a and b else None,
                                oraclePresent=test in oracle,
                                framebufferComparisonEligible=(a['framebufferComparisonEligible'] is not False and b['framebufferComparisonEligible'] is not False) if a and b else None,
                                framebufferComparisonReason=(a['framebufferComparisonReason'] or b['framebufferComparisonReason']) if a and b else None,
                                beforeRunId=a['runId'] if a else None,
                                afterRunId=b['runId'] if b else None))
    summary[host]['differentHashes'] = [r['test'] for r in comparisons if r['host'] == host and r['framebufferEqual'] is False]
    summary[host]['changesOverOnePercent'] = [r['test'] for r in comparisons if r['host'] == host and r['improvementPercent'] is not None and abs(r['improvementPercent']) > 1]
summary['qualification'] = 'Unpaired full diagnostics; descriptive timings only. Missing pinned oracles block full paired admission. Preserve process failures and unavailable leaves. Twenty-four balanced focused CPU/texture chunk attempts are reported separately. Shared process wall time is per chunk, not individual leaf wall time. Raw timing samples are iteration means; sample-mean p95/p99 are nearest-rank and are not individual iteration tails. Eight samples cannot resolve a distinct 99th percentile.'
groups = []
for host in ['deck', 'win']:
    for family in sorted({row['test'].split('.')[0] for row in comparisons}):
        rows = [r for r in comparisons if r['host'] == host and r['test'].split('.')[0] == family]
        matched = [r for r in rows if r['beforeUs'] is not None and r['afterUs'] is not None and r['fixedWorkSame']]
        before = sum(r['beforeUs'] for r in matched)
        after = sum(r['afterUs'] for r in matched)
        groups.append(dict(host=host, backend='Vulkan', family=family,
                           selectedLeaves=len(rows), recoveredMatchedLeaves=len(matched),
                           sumLeafMeanBeforeUs=before, sumLeafMeanAfterUs=after,
                           differenceUs=after-before,
                           improvementPercent=100*(before-after)/before if before else None,
                           beforeGuestNonPass=sum(r['beforeGuestOutcome'] != 'PASS' for r in matched),
                           afterGuestNonPass=sum(r['afterGuestOutcome'] != 'PASS' for r in matched),
                           qualification='Sum of retained matched leaf means; descriptive, not an independently timed group or balanced performance result'))
(p / 'current-full-summary.json').write_text(json.dumps(summary, indent=2))
for name, rows in [('current-full-leaf-times', leaf_rows), ('current-full-before-after', comparisons), ('current-full-process-times', attempt_rows), ('current-full-groups', groups)]:
    with (p / (name + '.csv')).open('w') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
print(json.dumps({host: {side: {k: v for k, v in summary[host][side].items() if k not in ['missingLeaves', 'missingOracles', 'runnerFailed']} for side in ['parent', 'candidate']} for host in ['deck', 'win']}, indent=2))
