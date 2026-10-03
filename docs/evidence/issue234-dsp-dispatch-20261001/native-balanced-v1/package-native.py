"""Package a collected campaign for its owning xemu PR, retaining failures.

Usage: package-native.py CAMPAIGN NEW_DESTINATION
Cache payloads stay local/server-side; their sizes and hashes remain in the
manifest. Every other downloaded byte, including diagnostic ZIPs, is retained.
This command never contacts the tester, launches jobs, or changes outcomes.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('campaign', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
root = args.campaign.resolve()
destination = args.destination.resolve()
if destination.exists():
    raise SystemExit('Refusing an existing output directory')
assert not destination.is_relative_to(root), 'Destination must be outside the campaign'
summary = json.loads((root / 'raw-audit-summary.json').read_text())
details = json.loads((root / 'raw-detail-audit.json').read_text())
assert summary['complete'] and summary['archivedAttempts'] == 12
assert details['cachedReportsMatch'] and details['rawFramesMatchRunnerCoverage']
scene = json.loads((root / 'scene-audit.json').read_text())
assert scene['reviewedCaptures'] == 24
included, omitted = [], []
for path in sorted((root / 'collected').rglob('*')):
    if not path.is_file():
        continue
    assert not path.is_symlink()
    relative = path.relative_to(root)
    row = dict(path=str(relative), bytes=path.stat().st_size,
               sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    if 'state' in relative.parts:
        omitted.append(row)
    else:
        included.append((path, row))
for path in sorted(root.iterdir()):
    if not path.is_file():
        continue
    relative = path.relative_to(root)
    row = dict(path=str(relative), bytes=path.stat().st_size,
               sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    included.append((path, row))
manifest = dict(included=[row for _, row in included], omittedCachePayloads=omitted,
                omission='Only collected/*/state runtime driver-cache payloads omitted; '
                         'preserved locally and server-side with hashes here. '
                         'All expanded diagnostics and original diagnostic ZIPs retained. '
                         'No executable, firmware, game image or private HDD redistribution.')
destination.mkdir(parents=True)
(destination / 'manifest.json.gz').write_bytes(gzip.compress(
    (json.dumps(manifest, indent=2) + '\n').encode(), mtime=0))
archive = destination / 'records.tar.gz'
with archive.open('wb') as output:
    with gzip.GzipFile(fileobj=output, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as bundle:
            for path, row in included:
                entry = bundle.gettarinfo(str(path), arcname=row['path'])
                entry.mtime = entry.uid = entry.gid = 0
                entry.uname = entry.gname = ''
                with path.open('rb') as payload:
                    bundle.addfile(entry, payload)
with tarfile.open(archive, 'r:gz') as bundle:
    assert len(bundle.getmembers()) == len(included)
    for _, row in included:
        payload = bundle.extractfile(row['path']).read()
        assert len(payload) == row['bytes']
        assert hashlib.sha256(payload).hexdigest() == row['sha256']
verification = dict(archiveBytes=archive.stat().st_size,
                    archiveSha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                    verifiedPayloadFiles=len(included), omittedCacheFiles=len(omitted),
                    omittedCacheBytes=sum(row['bytes'] for row in omitted),
                    archivedAttempts=summary['archivedAttempts'],
                    eligibleAttempts=sum(row['assessment']['Comparison'] == 'eligible'
                                         for row in summary['attempts']),
                    rawReportsMatchCached=True, rawFramesMatchRunnerCoverage=True,
                    allArchivePayloadsRehashed=True)
(destination / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
print(json.dumps(verification))
