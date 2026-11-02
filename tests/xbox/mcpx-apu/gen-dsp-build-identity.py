#!/usr/bin/env python3
"""Record the fallback static DSP archive used by this build."""

import hashlib
from pathlib import Path
import re
import sys

if len(sys.argv) not in (2, 4):
    raise SystemExit("expected output, optionally followed by archive and version")
output = Path(sys.argv[1])
if len(sys.argv) == 4:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", sys.argv[3]):
        raise SystemExit("invalid DSP version")
    digest = hashlib.sha256()
    with open(sys.argv[2], "rb") as archive:
        for block in iter(lambda: archive.read(1024 * 1024), b""):
            digest.update(block)
    status, version, sha256 = "BUILD_RECORDED", sys.argv[3], digest.hexdigest()
else:
    status, version, sha256 = "UNVERIFIED", "", ""
output.write_text(
    f'#define DSP_REPLAY_LIBRARY_STATUS "{status}"\n'
    f'#define DSP_REPLAY_LIBRARY_VERSION "{version}"\n'
    f'#define DSP_REPLAY_LIBRARY_SHA256 "{sha256}"\n', encoding="ascii")
