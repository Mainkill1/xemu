"""Generate canonical body payloads from the complete qualified native summary."""
from pathlib import Path
import json
import subprocess

ROOT = Path('/home/codex/src/steamdeck-xemu')
s = json.loads((ROOT / 'artifacts/issue229-dsp-multiply/native-balanced-v3/summary.json').read_text())
assert s['complete']
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT / 'worktrees/issue229-dsp-multiply', text=True).strip()
url = f'https://github.com/Mainkill1/xemu/blob/{head}/docs/evidence/issue229-dsp-multiply-20261001/control-followup/README.md#native-attempts-and-comparison'
lines = ['Native PGR2 full C DSP on Steam Deck/Vulkan: the frozen **ABBA then BAAB** sequence is complete. Seven attempts pass declared evidence gates; the final candidate exits 0 but fails the 160-positive-interval requirement (150 in the final 300 s). Mesa private disk writes qualify on all eight without waivers. No run is replaced and no gate is relaxed.', '',
         '| Order / position | Build | Cadence flips/s | Interval p95 ms | Comparison gate |',
         '|---|---|---:|---:|---|']
for a in s['attempts']:
    v = a['values']
    p95 = f"{v['frameControlP95']:.3f}" if 'frameControlP95' in v else 'Unavailable: frame gate failed'
    lines.append(f"| {a['order'].upper()} {a['position']} | {a['variant']} | {v['flipControlFps']:.3f} | {p95} | {a['assessment']['Comparison']} |")
lines += ['', 'All eight use the same parent library bundle, fixed settings/seed inputs, VP workers 0, vsync off, a private cold Mesa launch, 60 s scene warmup and 300 s observation. The parent-only pilot is excluded. **No eligible aggregate native improvement percentage is produced:** one required evidence gate fails. Unchanged-parent cadence spans 0.586–0.820 flips/s; candidate cadence spans 0.467–0.804. Slow results remain visible; neither a slowdown cause nor candidate neutrality is established. Flip-control MMIO is not necessarily rendered FPS. Fixed power/frequency, fully warm shaders, PCM parity and ordinary/default JIT game controls remain unqualified. ' + f'[All attempts, hashes and complete archives]({url}).', '']
pr = json.loads((ROOT / 'scratch/issue229-pr-current.json').read_text())['body']
start = pr.index('### Risks and remaining questions')
pr = pr[:start] + '\n'.join(lines) + '\n' + pr[start:]
start = pr.index('No qualified game gain or PCM parity.')
end = pr.index('\n\nOne [parent-only Deck profile]',start)
pr = pr[:start] + 'No qualified game gain or PCM parity. Three initial pilots fail frame gates; the longer parent pilot also fails record-count and quit gates. Those failures remain intact. The corrected parent procedure passes coarse gates; the completed native campaign retains a frame-evidence failure and establishes no qualified game gain. Evidence recipes now reject mismatched measurement identities and retain early failures/partial publication; all 12 retained negative/control fixtures pass.' + pr[end:]
old='79a6001c5b3595b383b05bb1855574b8a1948c4c'
pr=pr.replace(old,head)
issue = json.loads((ROOT / 'scratch/issue229-issue-current.json').read_text())['body']
start=issue.index('All three initial full-DSP Deck game pilots remain ineligible.')
end=issue.index('\n\nFour unchanged-parent', start)
issue=issue[:start] + '\n'.join(lines) + issue[end:]
issue=issue.replace(old,head)
for name,body in [('pr',pr),('issue',issue)]:
 (ROOT / ('scratch/issue229-' + name + '-complete-native.json')).write_text(json.dumps({'body':body})+'\n')
print('Generated body payloads for head',head)
