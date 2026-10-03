# Issue #167: updated-runner A/A control on Steam Deck

**Decision: HOLD. No performance improvement is established.** All eight runs
completed, passed their existing guest references, archived complete evidence,
and were eligible for comparison. Identical executable bytes nevertheless produced
a 4.25% pooled difference in rewrite mean time. Small rewrite effects cannot be
accepted as gains from this measurement setup.

## What was compared

Both A and B are the same compile-disabled executable, SHA-256
`4a85fa3316e1f29ca4526ccad6488ff5ebf0414998c01328132e76a50fbc8de8`,
built from product `7cb38844fb2dddb1b24cc65501831c9cdcedb43e`. A and B are
schedule labels, not different source revisions. The maintained runner executed
physical **A1, B1, B2, A2, B3, A3, A4, B4**: ABBA followed by BAAB.

Runner `c264004dfc906eef008c8a7235764c37daee330b` includes the current main
controller-presence (#93), benchmark-start-scene (#94), and portable saved-test
identity (#95) fixes, plus draft #89's retained standalone-process support.
The HTTP discovery receipt verifies this deployed revision on Deck `10.0.0.123`.
These fixes do not retroactively qualify older measurements. This CPU suite
uses its pinned guest-result references; it is not a retail scene/controller test.

## Results anyone can read

Each attempt records ten guest samples per leaf. The following A/B values are
the **median of four attempt-level mean work times**, converted from the runner's
microseconds to seconds. Saved time is A minus B. Improvement is
`100 * (A - B) / A`: positive is better, negative is longer execution.
Because A and B are identical, these differences describe variation, not a patch.

| Workload | A time | B time | Time saved | Improvement % | ABBA % | BAAB % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stable code, 50 million operations/sample | 3.334416 s | 3.347894 s | -0.013478 s | -0.40% | +0.06% | -0.48% |
| Code rewrite, 1 million operations/sample | 2.992916 s | 3.120203 s | -0.127287 s | -4.25% | -4.05% | -2.60% |

The stable-code median-sample metric differed by -0.52%; rewrite by -4.33%.
Stable p95/max differed by -0.75%; rewrite by -3.06%. With only ten samples per
attempt, the reported nearest-rank p95 equals that attempt's maximum; it is not
a well resolved tail estimate. All five metrics and individual attempt values
are preserved in the runner's JSON/CSV reports inside the archive.

There were no runner-reported opposite-order disagreements. That does **not**
make the rewrite difference a real regression: both halves tested identical bytes.
This single campaign does not estimate a confidence interval or establish a
universal noise floor. The cause of the rewrite variation is not classified.
No host clock, power policy or guest clock was changed to make it pass.

## State and correctness

All eight runs had empty private Mesa disk-cache namespaces before launch and
observed 275 driver-cache files / 2,886,729 bytes afterward. Each reports
`mesa_private_disk_writes_observed`, `ComparisonReady=true`, zero state issues,
and `AllowUncontrolledDriverCache=false`. No shared cache purge or waiver was
used. OS page cache and driver memory cache remain disclosed limits.

Both leaves passed the original pinned references in every attempt. There were
no failed native attempts, silent reruns, missing children or reference changes.
Suite qualification remains **unverified**; eligible attempts are not an approved
baseline. An initial local staging assertion expected a different leaf order;
that pre-execution error was retained and the same staged identities recovered.

Campaign: `i167-obs-aa-7cb-001`; revision
`b1df6dd9522ef2b7f05a9707e3f892d5a29b0c979511bda61d5e328c662e9782`.
Suite revision:
`8d810171d230a102b01412ff13b20cc05986611636cac7ef71a7ac7a0ad8437d`.
ISO: `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`.
Catalog: `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`.
Both pinned `c02a1a44` CPU leaves run in one chunk, Vulkan/RADV, 128 MiB,
zero warmups, multiplier one, per-iteration completion. Supporting binaries,
configuration, EEPROM and prepared disk seed are unchanged and hashed.

Normal operation used maintained LAN HTTP for launch, wait and collection.
Collection occurred after all eight attempts finished and the Deck was idle.
No monitor commands, occupancy/timing instrumentation, external sampler or
operator intervention was injected into the campaign.

## Evidence and next step

[SUMMARY.json](SUMMARY.json) retains all reported metric distributions and cache
qualification. [INDEX.json](INDEX.json) hashes every collected native file.
[EVIDENCE.zip](EVIDENCE.zip) includes canonical attempts, reports, raw guest
results, state/configuration receipts, host metrics, frozen contract and plans.
**2,480 files were collected, zero exclusions.** The archive contains 280 native
files; 2,200 private runtime/cache payloads remain locally retained and individually
hashed. Firmware, HDDs and private cache payloads are not published.

Archive: 899,981 bytes, SHA-256
`f3afab368098d0b69b5729279bfbe60aa029aed4882fe0a2b3a1ecbc7d1cae63`.

The matched dormant-probe ABBA/BAAB campaign has been started; counters/conflicts
campaigns are staged but unstarted. Their results are not included here. The
existing API freezes one runtime mode for both builds, so those comparisons
measure compile-hook plus collection cost, not same-executable OFF/ON isolation.
Any small rewrite difference remains inconclusive given this A/A result.
No production victim-cache candidate is implemented or qualified by this packet.
PR #299 remains draft; issue #167 remains open. Nothing was merged.
