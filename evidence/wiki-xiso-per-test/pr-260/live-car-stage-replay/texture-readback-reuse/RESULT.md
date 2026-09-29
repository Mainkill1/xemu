# Shared exact-generation Vulkan texture readbacks

Native run3 at3a3765ef rendered the confirmed37-part PGR2 car with owned DAC
palette and a fixed above-body camera. Properly resumed/focused steering changed
a front wheel pose. The run remains failed/incomplete: no smooth fresh-pose,
matched overhead, broad OpenGL, duplicate-car/LOD or exact-appearance gate passed.
Push36515358970 and PR36515362583 for63f7e934 both passed20/20 jobs; the actual
unit job109236522489 reported189 passed,0failed,17skipped.

The private saved native frame retained61events,1050 texture images,55 unique
immutable image blocks and128363064 repeated decoded bytes. Local O2 catalog
median10.054ms and placement9.060ms identify additional CPU cost; these are not
Windows timings or a measured gain from this change.

The production texture admission/retirement fixture first failed at runtime:
generationsA,A,B issued3 copies instead of the required2. The implementation
matches allocation incarnation, content generation, capture batch, format,
extent, mip, face and component mapping. All three actual texture write paths
increment the generation; saturation and unknown generation/owner/batch disable
reuse. Targeted single-draw requests keep independent copies. An indexed map
and reference-counted sources survive cancellation and owner-first retirement;
consumer sampler/slot/state and final service-owned payloads remain separate.
The recorder disarmed path adds no copy/hash/query/file I/O.

Final strict14cases pass. ASan/UBSan14cases pass; LeakSanitizer cannot execute
under this environment's ptrace, so detect_leaks=0 is explicit. Every fixture
checks allocation/destruction balance and zero retained staging, but this does
not claim a whole-application leak qualification. Four changed product units
(draw, texture, command and Asset Browser UI) compile using actual QEMU flags
and-Werror. Formatting and whitespace checks pass.

The native fixture directly includes the production input header. Its Vulkan/
VMA callbacks bound and freeze data at copy-command recording; unused draw
helpers assert if reached. It does not model a real Vulkan queue/fence or
measure native performance. An earlier fixture including all draw.c failed
ASan link because global pipeline callback tables retained unrelated sections;
that test setup failure is distinct from an application defect.

A new exact-head Windows artifact and immutable native procedure must measure
readback copies/shares, viewer FPS and fresh-pose rate. The current serial
next-frame/rearm scheduler still limits cadence; no30 fresh poses/s claim,
merge readiness or Steam Deck improvement is established by these tests.

## Native discovery regression and correction

Both b19c9415 CI triggers36518156191/36518159100 passed20/20; actual unit
job109245149203 reported190passed,0failed,17skipped, including the new14-case
texture reuse target. Nevertheless native Windows attempt4 at exact executable
287296f0d74d5fa74911424575573095502f36ab3714d07089422a1ce942d294
stopped initial discovery at237events before the car. The retained manifest
reports capture.readback.status4 (budget),256MiB recorder budget and pending
consumer shares. Most geometry was unavailable after the failed readback batch.
The run is archived failed/incomplete; process exited, no rule or marker enabled.

Root cause: reuse lowers physical staging but every pending consumer still
reserves its future CPU payload conservatively. The existing safe-boundary
drain watched only physical staging, so logical reservations exhausted first.
The fix tracks pending consumer bytes independently, retires at either physical
or pending128MiB pressure, and clears the counter on all retirement paths.
Budgets stay unchanged and no forensic evidence is skipped or falsely ready.

Two runtimeREDs pin this:4MiB physical/128MiB pending must drain; six consumers
of one2048x2048R16 snapshot must account24MiB apiece despite just one allocation.
The pressure test and15 native adapter cases now pass, as does ASan/UBSan with
leakchecking disabled under tracing. Four actual-flags product units pass.
A further immutable native attempt of the corrected head is required.
