"""Attribute waits in the existing guest-frame analysis window; no extrapolation."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent / 'native'
results = []
for variant in ['a1', 'b1']:
    paths = list((root / variant / 'deck-evidence').glob('*/performance.json'))
    if not paths:
        continue
    folder = paths[0].parent
    perf = json.loads(paths[0].read_text())
    analysis = perf['analysis']
    window = analysis['frames']
    lines = [json.loads(line) for line in (folder / 'vk-perf.ndjson').open()]
    schema = next(row for row in lines if row['type'] == 'schema')
    rows = [row for row in lines if row['type'] == 'frame' and
            window['windowStartUs'] <= row['timestamp_us'] <= window['windowEndUs']]
    assert rows, 'Telemetry clock does not overlap the guest-frame window'
    summary = dict(variant=variant, runId=perf['runId'],
                   app=json.loads((root / variant / 'deck-app.json').read_text()),
                   test=json.loads((root / variant / 'deck-saved.json').read_text()),
                   windowStartUs=window['windowStartUs'], windowEndUs=window['windowEndUs'],
                   frames=len(rows), guestCadenceFps=analysis['flips']['cadenceFps'],
                   cpuMean=analysis['monitoring']['cpu']['mean'],
                   frameIntervalsMs=window['intervalsMs'])
    groups = [('finish', 'finish_reasons', 'finish_submit_count_per_guest_frame',
               'finish_timed_submit_count_per_guest_frame', 'finish_sampled_wait_us_per_guest_frame'),
              ('aux', 'single_time_callers', 'single_time_submit_count_per_guest_frame',
               'single_time_timed_submit_count_per_guest_frame', 'single_time_sampled_wait_us_per_guest_frame')]
    for name, names, count, timed, wait in groups:
        summary[name] = {}
        for i, label in enumerate(schema[names]):
            submits = sum(row[count][i] for row in rows)
            samples = sum(row[timed][i] for row in rows)
            wait_us = sum(row[wait][i] for row in rows)
            summary[name][label] = dict(submits=submits, timedSubmits=samples,
                allSubmitsTimed=submits == samples, sampledWaitUs=wait_us,
                sampledWaitUsPerFrame=wait_us / len(rows))
    summary['totals'] = {key: sum(row[key] for row in rows) for key in rows[0]
                         if key.startswith('depth_alias_') or key == 'vk_queue_submit_calls_per_guest_frame'}
    summary['limits'] = ['One instrumented development A/B pair, uncontrolled driver cache.',
                         'Wait durations remain sampled when timedSubmits < submits; no extrapolation.',
                         'SURFACE_DOWN includes callers other than alias retirement.',
                         'Window follows the unchanged runner guest-frame analysis; CPU covers its stationary segment.']
    results.append(summary)
(root / 'wait-comparison.json').write_text(json.dumps(results, indent=2))
for row in results:
    print(row['variant'], 'frames', row['frames'], 'fps', row['guestCadenceFps'], 'cpu', row['cpuMean'])
    for name in ['finish', 'aux']:
        print(name, {k: round(v['sampledWaitUsPerFrame'], 2) for k,v in row[name].items() if v['submits']})
    print('retired', row['totals']['depth_alias_views_retired_per_guest_frame'],
          'submits', row['totals']['vk_queue_submit_calls_per_guest_frame'])
