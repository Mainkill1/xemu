# Apply selected stage filtering before the first follow frame

Native5 exact8b316278 (executablecdc0871d777df5b1d23d34e86a7941ffa43002609068f20d014b60788903d44e)
retained1714 parts and the player bodyE1543/frame3553/draw5923976/emission2953603.
The manually confirmed37-part car rendered original captured stages, textures,
samplers and post-blend DAC palette at the fixed above-body angle. Native save
and reopen restored that37-part assembly and its original anchor/appearance;
the private recording retains10132blocks73570704bytes. No game bytes/source or
screenshots are added to public Git.

Live follow failed: its first request still acquired the whole scene, reached
a partial frame5644/1723parts, retained the old coherent car and stopped. The
stage filter was populated only on a later rearm, which budget failure prevented.
The game was properly focused and drove/right-steered in the recorded interval;
this is not native wheel-update or fresh-cadence success. No acceptance marker
or replacement rule was enabled, and the process exited.

The correction copies the pinned assembly's supported stage sets synchronously
before the initial recorder claim. It retains no controller pointer. Normal
unpinned discovery stays unfiltered; later rearm still refreshes the filter.
A runtime regression failed with0 stage sets instead of1 in the first request.
It now verifies unrelated draws are rejected and matching draws admitted, with
a separate unpinned test proving ordinary discovery still admits other shaders.
All18 controller/lifecycle cases pass strict and ASan/UBSan (leakchecking0 under
tracing); both changed production units compile with actual QEMU flags/-Werror.

First GREEN build attempts exhausted the local16GiB/tmp filesystem, rather
than finding a source defect. Compiler temporary files were moved to owned
workspace scratch without removing unrelated data; the complete builds then
passed. The8b push CI passed20/20, while its PR trigger failed on macOS arm64
Python DNS before compilation and cancelled siblings. That infrastructure failure
is retained; this new head needs its own CI and further immutable native attempt.
No30 fresh poses/s, matched gameplay overhead or merge readiness is claimed.
