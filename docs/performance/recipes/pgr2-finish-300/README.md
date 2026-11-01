# PGR2 finish diagnostic: maintained 300-second recipe

This is the exact authored recipe used by draft PR #302 on Steam Deck
`10.0.0.123`. It records existing schema-8 Vulkan finish telemetry on frozen
main `76c23c7d444a6f12c9778bb2c35fab513f6c8056`, settles for 60 seconds,
then observes 300 seconds with start/midpoint/end captures. It adds no emulator
probe. The header-only diagnostic runs after observation without pausing.

## Known outcome and limits

The first attempt, `i250-finish-native-20261003-001-t001`, completed its plan
but failed the frame-sample evidence floor (160 positive intervals). Do not
silently relax that gate or repeat it as a replacement. Its three directly
reviewed captures reach the race, with the player's car parked at 0 MPH;
they do not qualify driving or streaming pressure. Visibility checks only
check image availability/content, not scene equivalence or emulation parity.
The cold private Mesa disk cache was qualified without a waiver. Full source
and outcome bindings are in the owning PR's native evidence report.

Saved runner definition: `i250-pgr2-finish-300-v1` at
`6a75b0ba5ffef2b35ffbbc10e9202e0f6d7740ed02fe4f120f4a2c56382f0135`.
`job.json` is the authored definition; the saved server adds permission
metadata. `CONTRACT.json` states the diagnostic-only decision boundary.
`xemu.toml` is byte-exact and includes the fixed input binding.

## Required package and assets

Use the maintained Xemu-Test-Runner HTTP client. Supply the frozen executable
and matching libraries/EEPROM at the six `requiredFiles` paths in `job.json`;
extract the archived reader identified in
[`vk-perf-reader-v1.json`](../vk-perf-reader-v1.json) to the package root:

```sh
git show 3e69f0b8f110430d34c796706bde35d3c05d3e16:scripts/performance/vk-perf-summary.py > /absolute/package/path/vk-perf-summary.py
cd /absolute/package/path
printf '%s  %s\n' 0f2a1b13d567ce183c3a341989c0071f9e218c8f85a8ee9bd2e613a6a20b84da vk-perf-summary.py | sha256sum --check
```

Fetch that immutable revision if it is absent locally. The archived reader
matches the historical package pin; the current maintained reader is for
subsequent offline analysis. Updating the reader packaged for execution
requires a new recipe revision, never changing this historical job. The job
pins their actual SHA-256 values and rejects changed inputs. Do not bundle
firmware, retail discs or saved guest disks into this repository. The four
private asset IDs and expected hashes are recorded in the runtime-state
contract; they must already exist on the target runner.

A different executable, state, renderer, input binding, navigation or analysis
floor requires an explicitly named new recipe/revision. Keep this original
negative outcome and source unchanged. For an optimization, build matching
parent/candidate packages and declare fresh A/A, ABBA and BAAB controls; this
unpaired diagnostic cannot measure a speedup.

## Upload and select without implicit execution

Run from the maintained runner client checkout. Replace package path and new
application/request IDs with deliberate unique identities; IDs below show the
API shape and are not a retry instruction.

```sh
python3 scripts/runner_tests.py --url http://10.0.0.123:9368 \
  upload /absolute/package/path --exe xemu --id NEW_APPLICATION_ID
python3 scripts/runner_tests.py --url http://10.0.0.123:9368 \
  config-upload i250-pgr2-finish-300-v1 /absolute/recipe/job.json \
  --assets NEW_APPLICATION_ID --description 'Frozen PGR2 finish diagnostic' \
  --build-file xemu
python3 scripts/runner_tests.py --url http://10.0.0.123:9368 \
  select NEW_APPLICATION_ID --id NEW_REQUEST_PREFIX \
  --tests i250-pgr2-finish-300-v1@FULL_RETURNED_REVISION
```

Read back the saved definition and verify authored keys, package inputs and
all 33 steps before explicitly starting the returned request. Follow that
same request to a terminal outcome; a wait timeout is not a rerun trigger.
Collect every eligible artifact after the runner is idle and retain the
original assessment, even when it fails:

```sh
python3 scripts/runner_tests.py --url http://10.0.0.123:9368 start REQUEST_ID
python3 scripts/runner_tests.py --url http://10.0.0.123:9368 wait REQUEST_ID --updates
python3 scripts/runner_api.py --url http://10.0.0.123:9368 \
  collect REQUEST_ID /absolute/evidence/path --all
python3 scripts/performance/vk-perf-summary.py /absolute/stopped/vk-perf.jsonl \
  --finish --tail-seconds 300 --out /absolute/new/finish-summary.json
```

The final 300-second telemetry tail is not exactly the declared segment:
records end on control-frame boundaries, capture steps add time, and a
crossing bucket is separate. Preserve both boundaries rather than treating
all-log totals or the tail as identical guest work. Report host elapsed
sampled waits, complete coverage, actual scene, all failures and identities.
Never sum nested CPU regions or turn counts into FPS/improvement estimates.
