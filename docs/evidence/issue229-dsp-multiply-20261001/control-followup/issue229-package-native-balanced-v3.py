"""Retain finished native campaign artifacts and verify each archived member.

The immutable plan bounds the allowed jobs. Already-published archives are
checked rather than overwritten; live or uncollected jobs are not inspected.
"""
from pathlib import Path
import gzip
import hashlib
import json
import shutil
import tarfile

ROOT = Path('/home/codex/src/steamdeck-xemu')
SOURCE = ROOT / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
DESTINATION = ROOT / 'worktrees/issue229-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001/control-followup/native-balanced-v3'
plan = json.loads((SOURCE / 'plan.json').read_text())
allowed = {attempt['id'] for attempt in plan['attempts']}
for result_path in sorted((SOURCE / 'collected').glob('*/result.json')):
    source = result_path.parent
    result = json.loads(result_path.read_text())
    assert result['job'] in allowed
    cleanup = json.loads((source / 'runtime-cleanup.json').read_text())
    assert cleanup['targetStopped'] and cleanup['evidenceFinalized']
    destination = DESTINATION / 'raw' / result['job']
    if not destination.exists():
        destination.mkdir(parents=True)
        rows = []
        for path in sorted(source.rglob('*')):
            if path.is_file():
                assert not path.is_symlink()
                rows.append(dict(path=path.relative_to(source).as_posix(), bytes=path.stat().st_size,
                                 sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        with (destination / 'artifacts.tar.gz').open('wb') as raw:
            with gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as zipped:
                with tarfile.open(fileobj=zipped, mode='w|') as archive:
                    for row in rows:
                        path = source / row['path']
                        info = archive.gettarinfo(str(path), arcname=row['path'])
                        info.uid = info.gid = info.mtime = 0
                        info.uname = info.gname = ''
                        with path.open('rb') as blob:
                            archive.addfile(info, blob)
        (destination / 'inventory.json').write_text(json.dumps({'files': rows}, indent=2) + '\n')
        for name in ['assessment.json', 'performance.json', 'result.json', 'runtime-cleanup.json',
                     'segments.jsonl', 'screenshots/recording-start.png', 'screenshots/recording-end.png']:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, target)
    rows = json.loads((destination / 'inventory.json').read_text())['files']
    with tarfile.open(destination / 'artifacts.tar.gz') as archive:
        assert len(archive.getmembers()) == len(rows)
        for row in rows:
            blob = archive.extractfile(row['path']).read()
            assert len(blob) == row['bytes'] and hashlib.sha256(blob).hexdigest() == row['sha256']
    print('Verified', result['job'], len(rows), 'archived members')
for path in sorted(SOURCE.iterdir()):
    if path.is_file():
        shutil.copyfile(path, DESTINATION / path.name)
