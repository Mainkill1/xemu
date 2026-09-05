"""Recalculate the published fixed-work test-family comparison."""
import json
from pathlib import Path
from statistics import mean

root = Path(__file__).resolve().parent
for backend in ['opengl', 'vulkan']:
    docs = [json.loads((root / 'inputs' / f'{role}-{backend}-r{i}.json').read_text())
            for role in ['baseline', 'candidate'] for i in [1, 2]]
    maps = [{r['id']: r for r in d['records'] if r['kind'] == 'leaf'} for d in docs]
    assert all(set(m) == set(maps[0]) for m in maps)
    included = []
    for name in sorted(maps[0]):
        records = [m[name] for m in maps]
        for key in ['revision', 'iterations', 'sample_count', 'gpu_completion_mode', 'outcome']:
            assert len({str(r.get(key)) for r in records}) == 1
        if len({r.get('framebuffer_fnv1a64') for r in records}) == 1:
            included.append(name)
    assert len(included) == 142
    print(backend, 'family, baseline mean ms, candidate mean ms, time delta percent')
    for group in sorted({n.split('.')[0] for n in included}) + ['ALL_INCLUDED']:
        names = [n for n in included if group == 'ALL_INCLUDED' or n.split('.')[0] == group]
        values = [sum(m[n]['guest_total_us'] for n in names) / 1000 for m in maps]
        baseline, candidate = mean(values[:2]), mean(values[2:])
        print(f'{group}, {baseline:.3f}, {candidate:.3f}, {100 * (candidate / baseline - 1):+.2f}')
