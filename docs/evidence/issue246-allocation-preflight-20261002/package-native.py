"""Freeze collected issue246 diagnostics with hashes; never modify outcomes.

Usage: package-native.py SOURCE NEW_DESTINATION
Omit cache payloads and local symbol-resolution symlinks, retaining their hashes
and identities. Source contains no executable/firmware/game/HDD payloads.
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
root = args.source.resolve()
destination = args.destination.resolve()
if destination.exists() or destination.is_relative_to(root):
    raise SystemExit('Destination must be new and outside the source')
for name in ['pgr2-v1', 'morrowind-v1', 'morrowind-v2']:
    receipt = json.loads((root / 'native-preflight' / name / 'collection.json').read_text())
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
    row = {'path': str(relative), 'bytes': path.stat().st_size, 'sha256': digest}
    if 'state' in relative.parts:
        omitted.append(row)
    else:
        included.append((path, row))
destination.mkdir(parents=True)
manifest = {'included': [r for _, r in included], 'omittedCachePayloads': omitted,
            'omission': 'Raw collected state/driver-cache payloads remain local '
                        'and server-side. Local mapped/symfs symlinks are omitted. '
                        'All nine original perf recordings, screenshots, full '
                        'telemetry logs, diagnostic ZIPs and failures are retained.'}
(destination / 'manifest.json.gz').write_bytes(gzip.compress(
    (json.dumps(manifest, indent=2) + '\n').encode(), mtime=0))
archive = destination / 'records.tar.gz'
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
with archive.open('rb') as stream:
    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
verification = {'archiveBytes': archive.stat().st_size, 'archiveSha256': digest,
                'payloadFilesRehashed': len(included), 'omittedCacheFiles': len(omitted),
                'omittedCacheBytes': sum(x['bytes'] for x in omitted),
                'archivedAttempts': 3, 'comparisonEligibleAttempts': 1,
                'profilerRecordings': 9, 'reviewedScreenshots': 14,
                'candidateImplemented': False, 'speedupMeasured': False}
(destination / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
print(json.dumps(verification), flush=True)
