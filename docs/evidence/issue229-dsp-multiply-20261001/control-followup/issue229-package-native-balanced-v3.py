"""Retain available terminal artifacts, including early failures.

Publish only a verified archive via directory rename. Preserve interrupted
publications beside their repaired destination. Missing cleanup is recorded,
not interpreted as proof that the target stopped or qualification passed.
"""
from pathlib import Path
import gzip
import hashlib
import json
import shutil
import tarfile
import tempfile
import uuid

ROOT = Path('/home/codex/src/steamdeck-xemu')
SOURCE = ROOT / 'artifacts/issue229-dsp-multiply/native-balanced-v3'
DESTINATION = ROOT / 'worktrees/issue229-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001/control-followup/native-balanced-v3'
plan = json.loads((SOURCE / 'plan.json').read_text())
allowed = {attempt['id'] for attempt in plan['attempts']}
readable = ['assessment.json', 'performance.json', 'result.json', 'runtime-cleanup.json',
            'segments.jsonl', 'screenshots/recording-start.png', 'screenshots/recording-end.png']


def verify(destination, rows):
    with tarfile.open(destination / 'artifacts.tar.gz') as archive:
        assert sorted(member.name for member in archive.getmembers()) == sorted(row['path'] for row in rows)
        for row in rows:
            blob = archive.extractfile(row['path']).read()
            assert len(blob) == row['bytes'] and hashlib.sha256(blob).hexdigest() == row['sha256']


for result_path in sorted((SOURCE / 'collected').glob('*/result.json')):
    source = result_path.parent
    result = json.loads(result_path.read_text())
    assert result['job'] in allowed
    assert result['status'] not in ['running', 'pending', 'testing'], 'Do not package live attempts'
    cleanup_path = source / 'runtime-cleanup.json'
    cleanup_error = None
    try:
        cleanup = json.loads(cleanup_path.read_text()) if cleanup_path.exists() else {}
    except (OSError, ValueError) as error:
        cleanup = {}
        cleanup_error = str(error)
    rows = []
    for path in sorted(source.rglob('*')):
        if path.is_file():
            assert not path.is_symlink()
            rows.append(dict(path=path.relative_to(source).as_posix(), bytes=path.stat().st_size,
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    destination = DESTINATION / 'raw' / result['job']
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and (destination / 'inventory.json').exists() and (destination / 'artifacts.tar.gz').exists():
        previous = json.loads((destination / 'inventory.json').read_text())['files']
        verify(destination, previous)
        assert previous == rows, 'Collected evidence changed after publication'
    else:
        stage = Path(tempfile.mkdtemp(prefix=destination.name + '.staging-', dir=destination.parent))
        with (stage / 'artifacts.tar.gz').open('wb') as raw:
            with gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as zipped:
                with tarfile.open(fileobj=zipped, mode='w|') as archive:
                    for row in rows:
                        path = source / row['path']
                        info = archive.gettarinfo(str(path), arcname=row['path'])
                        info.uid = info.gid = info.mtime = 0
                        info.uname = info.gname = ''
                        with path.open('rb') as blob:
                            archive.addfile(info, blob)
        (stage / 'inventory.json').write_text(json.dumps({'files': rows}, indent=2) + '\n')
        verify(stage, rows)
        if destination.exists():
            destination.rename(destination.with_name(destination.name + '.partial-' + uuid.uuid4().hex))
        stage.rename(destination)
    # Repair missing readable copies without changing the complete raw archive.
    for name in readable:
        if (source / name).exists():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, target)
    retention = dict(execution=result['status'], cleanupError=cleanup_error, targetStopped=cleanup.get('targetStopped'),
                     evidenceFinalized=cleanup.get('evidenceFinalized'),
                     missingReadableFiles=[name for name in readable if not (source / name).exists()],
                     qualification='See canonical result assessment; retention alone grants no eligibility.')
    (destination / 'retention.json').write_text(json.dumps(retention, indent=2) + '\n')
    print('Verified', result['job'], len(rows), 'archived members')
for path in sorted(SOURCE.iterdir()):
    if path.is_file():
        shutil.copyfile(path, DESTINATION / path.name)
