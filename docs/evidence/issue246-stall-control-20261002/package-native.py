"""Preserve two explicit Deck procedures with original outcomes and raw traces.

Cache/state payloads remain on the runner and locally; their hashes are included.
Packaging is exclusive and verifies each archived payload before publication.
"""
from pathlib import Path
import gzip
import hashlib
import json
import tarfile

root = Path('/home/codex/src/steamdeck-xemu')
work = root / 'worktrees/issue246-allocation-attribution'
dest = work / 'docs/evidence/issue246-stall-control-20261002'
dest.mkdir(parents=True, exist_ok=False)
sources = {
    'pgr2-throttle-off': 'native-collector/pgr2-throttle-off-v1',
    'pgr2-stall-profile': 'native-collector/pgr2-stall-profile-v1',
}
included, omitted, runs = [], [], []
for label, relative in sources.items():
    base = root / 'artifacts/issue246-texture-allocation' / relative
    receipt = json.loads((base / 'collection.json').read_text())
    assert receipt['complete'] and receipt['excluded'] == 0
    outcome = json.loads((base / 'result-summary.json').read_text())
    runs.append({'label': label, 'collection': receipt, 'outcome': outcome['outcome']})
    for file in sorted(base.rglob('*')):
        rel = file.relative_to(base)
        if 'symfs' in rel.parts:
            continue
        assert not file.is_symlink(), file
        if not file.is_file():
            continue
        row = {'path': str(Path(label) / rel), 'bytes': file.stat().st_size,
               'sha256': hashlib.file_digest(file.open('rb'), 'sha256').hexdigest()}
        if 'state' in rel.parts:
            omitted.append(row)
        else:
            included.append((file, row))
build = root / 'artifacts/issue246-texture-allocation/native-builds/candidate-linux-v2'
for name in ['build-identity.json', 'ci-unit.log', 'ci-linux-release.log']:
    file = build / name
    included.append((file, {'path': 'build/' + name, 'bytes': file.stat().st_size,
                            'sha256': hashlib.file_digest(file.open('rb'), 'sha256').hexdigest()}))
archive = dest / 'records.tar.gz'
with archive.open('xb') as output:
    with gzip.GzipFile(fileobj=output, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as tar:
            for file, row in included:
                info = tar.gettarinfo(str(file), arcname=row['path'])
                info.mtime = info.uid = info.gid = 0
                info.uname = info.gname = ''
                with file.open('rb') as payload:
                    tar.addfile(info, payload)
with tarfile.open(archive) as tar:
    assert len(tar.getmembers()) == len(included)
    for _, row in included:
        assert tar.getmember(row['path']).size == row['bytes']
        assert hashlib.file_digest(tar.extractfile(row['path']), 'sha256').hexdigest() == row['sha256']
inventory = {'included': [row for _, row in included], 'omittedStatePayloads': omitted,
             'omission': 'State/cache payloads retained locally/server-side; original outcomes, captures, traces and profiler data included. No firmware/disc/executable payloads.'}
(dest / 'records-inventory.json.gz').write_bytes(gzip.compress((json.dumps(inventory, indent=2) + '\n').encode(), mtime=0))
verification = {'testedRuntimeCommit': '8079502ae25a602e1395fa9ee87ec1ef8b57c9d6',
                'parentCommit': 'ee5ce48b48784f999af374c1452003f8b2b1230f',
                'archiveBytes': archive.stat().st_size,
                'archiveSha256': hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest(),
                'payloadFilesRehashed': len(included), 'omittedStateFiles': len(omitted),
                'runs': runs, 'performanceComparison': 'Not measured: no A/A, ABBA/BAAB or per-leaf XISO acceptance'}
(dest / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
(dest / 'package-native.py').write_bytes(Path(__file__).read_bytes())
print(json.dumps({key: value for key, value in verification.items() if key != 'runs'}, indent=2))
