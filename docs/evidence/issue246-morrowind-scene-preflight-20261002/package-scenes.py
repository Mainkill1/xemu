"""Freeze the two collected scene attempts without rewriting their outcomes.

Usage: package-scenes.py NATIVE_PREFLIGHT NEW_DESTINATION
The destination receives one archive/inventory per attempt. Raw cache payloads
remain local/server-side with hashes; local symfs symlinks are not portable.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
source = args.source.resolve()
destination = args.destination.resolve()
if destination.exists() or destination.is_relative_to(source):
    raise SystemExit('Destination must be new and outside the source')
destination.mkdir(parents=True)
verification = []
for name in ['morrowind-v3', 'morrowind-v4']:
    root = source / name
    receipt = json.loads((root / 'collection.json').read_text())
    assert receipt['complete'] and receipt['scope'] == 'allEligibleArtifacts'
    assert receipt['excluded'] == 0
    included, omitted = [], []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if 'symfs' in relative.parts:
            continue
        assert not path.is_symlink()
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        row = {'path': str(relative), 'bytes': path.stat().st_size,
               'sha256': digest}
        if 'state' in relative.parts:
            omitted.append(row)
        else:
            included.append((path, row))
    manifest = {
        'attempt': name,
        'included': [row for _, row in included],
        'omittedCachePayloads': omitted,
        'omission': 'Raw state/cache payloads remain local/server-side. '
                    'Local mapped/symfs symlinks are omitted. Original '
                    'diagnostics.zip, perf recordings, telemetry, captures, '
                    'assessment, recipes and collection receipts retained.',
    }
    (destination / (name + '-manifest.json.gz')).write_bytes(gzip.compress(
        (json.dumps(manifest, indent=2) + '\n').encode(), mtime=0))
    archive = destination / (name + '-records.tar.gz')
    with archive.open('xb') as output:
        with gzip.GzipFile(fileobj=output, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as bundle:
                for path, row in included:
                    entry = bundle.gettarinfo(str(path), arcname=row['path'])
                    entry.mtime = entry.uid = entry.gid = 0
                    entry.uname = entry.gname = ''
                    with path.open('rb') as stream:
                        bundle.addfile(entry, stream)
    with tarfile.open(archive, 'r:gz') as bundle:
        assert len(bundle.getmembers()) == len(included)
        for _, row in included:
            stream = bundle.extractfile(row['path'])
            assert hashlib.file_digest(stream, 'sha256').hexdigest() == row['sha256']
            assert bundle.getmember(row['path']).size == row['bytes']
    assert archive.stat().st_size < 100 * 1024 * 1024
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    audit = json.loads((root / 'scene-audit.json').read_text())
    row = {
        'attempt': name, 'archive': archive.name,
        'archiveBytes': archive.stat().st_size, 'archiveSha256': digest,
        'payloadFilesRehashed': len(included), 'omittedCacheFiles': len(omitted),
        'omittedCacheBytes': sum(x['bytes'] for x in omitted),
        'profilerRecordings': sum(p.suffix == '.data' for p, _ in included),
        'reviewedScreenshots': audit['reviewedScreenshots'],
        'candidateImplemented': False, 'speedupMeasured': False,
    }
    verification.append(row)
    print(json.dumps(row), flush=True)
(destination / 'verification.json').write_text(
    json.dumps(verification, indent=2) + '\n')
