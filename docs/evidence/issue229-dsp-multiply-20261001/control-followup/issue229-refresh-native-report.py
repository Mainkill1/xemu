"""Refresh presentation from verified campaign summary; never alter run data."""
from pathlib import Path
import json

ROOT = Path('/home/codex/src/steamdeck-xemu')
SOURCE = ROOT / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
DOCS = ROOT / 'worktrees/issue229-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001'
summary = json.loads((SOURCE / 'summary.json').read_text())
lines = ['### Native attempts and comparison', '',
         'A=parent, B=candidate. The procedure pilot is excluded. All predeclared attempts remain visible.', '',
         '| Order / position | Build | Cadence flips/s | Mean ms | p95 ms | p99 ms | CPU core-percent | Canonical result |',
         '|---|---|---:|---:|---:|---:|---:|---|']
for a in summary['attempts']:
    v = a.get('values', {})
    values = [f'{v[k]:.3f}' if k in v else ('Unavailable' if a['state'] == 'archived' else 'Pending') for k in ['flipControlFps','frameControlMean','frameControlP95','frameControlP99','cpuCorePercent']]
    result = a.get('assessment', {}).get('Comparison', a['state'])
    lines.append('| ' + ' | '.join([a['order'].upper() + ' ' + str(a['position']), a['variant'], *values, result]) + ' |')
lines += ['', f"{summary['archivedAttempts']} of {summary['plannedAttempts']} attempts archived. Comparisons require all eight canonical outcomes and matching frozen measurement identities.", '']
if summary['comparisons']:
    lines += ['Medians of per-attempt metrics; four launches per build overall, two per build in each order. Positive improvement is better. CPU core-percent is diagnostic, not fixed-work cost.', '',
              '| Order | Metric | Parent median | Candidate median | After − before | Improvement % |',
              '|---|---|---:|---:|---:|---:|']
    for c in summary['comparisons']:
        improvement = 'N/A' if c['improvementPercent'] is None else f"{c['improvementPercent']:+.2f}%"
        lines.append(f"| {c['order'].upper()} | {c['metric']} ({c['unit']}) | {c['parentMedian']:.3f} | {c['candidateMedian']:.3f} | {c['afterMinusBefore']:+.3f} | {improvement} |")
else:
    lines += ['The comparison array remains empty; incomplete or mismatched evidence cannot establish a gain.', '']
lines += ['', 'Parent and candidate both vary substantially. The slow ABBA candidate B2 and later unchanged parent A2 remain in the table. No thermal, scheduler, cache, or alignment cause is established. The resource audit covers the first three attempts only; it cannot explain later variation.', '',
          'These are host-clock flip-control MMIO observations, not necessarily rendered FPS. CPU uses the named 300 s segment, intervals use the final 300 s tail, and cadence uses the final 40 aggregate records. Windows differ, so no CPU-per-frame estimate is derived. Cold private Mesa launch plus 60 s scene warmup is qualified; fully warm shaders, fixed power/frequency, driver RAM, PCM parity and default JIT game controls remain unqualified. Coarse image gates do not establish complete game/audio correctness.', '',
          '[Per-attempt identities and comparisons](native-balanced-v3/summary.json), [complete verified archives](native-balanced-v3/raw/), [source-statistic verification](native-balanced-v3/verified-statistics.log), and [recipe review/corrections](evidence-recipe-review.md) preserve the evidence. Each successful archive contains 865 files. No attempt was replaced or excluded.', '']
path = DOCS / 'control-followup/README.md'
s = path.read_text()
start = s.find('All four ABBA attempts are archived and eligible.')
end = s.index('This native-title sequence', start) if start >= 0 else -1
if start >= 0:
    s = s[:start] + 'Submit only the next frozen ID after the previous run is archived and the owned target stopped. Failed outcomes remain evidence; no replacements or exclusions are permitted. Normal execution and collection use the maintained HTTP client.\n\n' + s[end:]
start = s.find('### First two campaign attempts: descriptive, incomplete')
if start < 0:
    start = s.index('### Native attempts and comparison')
end = s.index('### Windows readiness: separate qualification gap', start)
s = s[:start] + '\n'.join(lines) + '\n' + s[end:]
s = s.replace('Windows remains reachable, idle, unlocked and on AC with battery saver off.', 'The retained Windows readiness observation found the host reachable, idle, unlocked and on AC with battery saver off.')
path.write_text(s)
path = DOCS / 'REPORT.md'
s = path.read_text()
start = s.index('The frozen eight-run native full-C **ABBA then BAAB** comparison')
end = s.index('\n', start)
paragraph = f"The frozen eight-run native full-C **ABBA then BAAB** comparison has {summary['archivedAttempts']}/8 archived attempts. The procedure pilot is excluded. Every collected attempt remains in the [per-attempt table and complete archives](control-followup/README.md#native-attempts-and-comparison), including slow candidates and later slow unchanged parents. Source statistics and archive members are independently verified. The summary requires matching frozen workload, profile, storage, host/runner and fixed inputs before emitting comparisons. Native game improvement remains unqualified: power/frequency, fully warm shaders, PCM and default JIT game controls are missing. Windows additionally lacks a driver-cache/private-state qualification contract. PR #278 remains draft/HOLD."
s = s[:start] + paragraph + s[end:]
path.write_text(s)
