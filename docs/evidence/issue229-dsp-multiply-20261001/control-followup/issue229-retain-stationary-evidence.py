"""Archive a collected stationary pilot; verify every stored byte on readback.

Usage: python issue229-retain-stationary-evidence.py v2 COLLECTED_RUN_DIRECTORY
No runner access, scheduling, assessment changes or original-result rewrites.
"""
from pathlib import Path
import gzip
import hashlib
import json
import re
import shutil
import sys
import tarfile

ROOT = Path('/home/codex/src/steamdeck-xemu')
version, source_name = sys.argv[1:]
source = Path(source_name)
destination = ROOT / 'worktrees/issue229-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001/control-followup' / ('stationary-' + version)
destination.mkdir(exist_ok=False)
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
with tarfile.open(destination / 'artifacts.tar.gz') as archive:
    assert len(archive.getmembers()) == len(rows)
    for row in rows:
        blob = archive.extractfile(row['path']).read()
        assert len(blob) == row['bytes']
        assert hashlib.sha256(blob).hexdigest() == row['sha256']
(destination / 'inventory.json').write_text(json.dumps({'files': rows}, indent=2) + '\n')
for name in ['result.json', 'assessment.json', 'performance.json', 'segments.jsonl',
             'stderr.log', 'diagnostics/run-state/report.json',
             'screenshots/recording-start.png', 'screenshots/recording-end.png']:
    target = destination / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / name, target)
package = ROOT / 'artifacts/issue229-dsp-multiply/native-packages' / ('issue229-deck-pgr2-stationary-full-c-' + version)
for name in ['manifest.json', 'xemu.toml', 'created.json', 'baked.json', 'parent-started.json', 'rejected-label-shape.json']:
    if (package / name).exists():
        shutil.copyfile(package / name, destination / name)
result = json.loads((source / 'result.json').read_text())
performance = json.loads((source / 'performance.json').read_text())
storage = json.loads((source / 'diagnostics/run-state/report.json').read_text())
frames = [tuple(map(int, re.findall(r'\d+', line))) for line in (source / 'guest-frames.log').read_text().splitlines()]
assert all(len(row) == 3 for row in frames)
tail_seconds = performance['Profile']['FrameTailSeconds']
tail = [row for row in frames if row[0] >= frames[-1][0] - tail_seconds * 1000000 and row[2] > 0]
summary = dict(runId=result['runId'], executableSha256=result['executableSha256'],
               executionStatus=result['status'], exitCode=result['exitCode'], detail=result['detail'],
               assessment=result['assessment'], positiveFrameTailIntervals=len(tail),
               flipSummaryRecords=len((source / 'guest-flips.log').read_text().splitlines()),
               driverQualification=storage['DriverQualification'], driverIssues=storage['Issues'],
               driverAfterFiles=len(storage['DriverAfter']['Files']), driverAfterBytes=storage['DriverAfter']['Bytes'],
               targetStopped=storage['TargetStopped'], cacheWaiver=storage['AllowUncontrolledDriverCache'],
               archiveVerifiedFiles=len(rows), archiveVerifiedBytes=sum(row['bytes'] for row in rows))
(destination / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({key: value for key, value in summary.items() if key not in ('assessment', 'detail')}))
