# Deck black-screen diagnostic — 2026-10-05

The reported run used the parent/main-equivalent build `9c1342550097`, not the audio candidate. QMP was running, not paused; chunk 13 completed in 35.476 seconds with all 12 intrinsic guest outcomes PASS. Its pinned framebuffer oracle comparison failed one existing composite leaf; this is not an overall correctness pass.

Three exact-procedure short diagnostic replays completed without a persistent hang. Replay 1 produced no images because the installed `/preview` endpoint had no cached image and the desktop tool initially lacked the X11 authentication path. Replay 2 used the supported `/screenshot` endpoint; a sampled framebuffer was uniformly RGB (16,24,32), matching `PrepareDraw(0xFF101820)`. Later samples displayed composite results. Replay 3 captured the physical KDE desktop alongside guest images: the display advanced from Xbox startup through initializing/running text to results for multiple cases. The dark physical images retained running/result text. This supports a transient dark test interval, not a reproduced frozen presentation. It does not prove that every earlier reported black screen has this cause.

`GameLoadCompositeTests::RunTest` clears before the measured Profile. CPU-only phases do not draw visible geometry; final correctness rendering and result overlay occur after Profile and FinishDraw. The pinned tests/inputs/measurement boundaries were not changed. Replays with screenshots are diagnostic and excluded from performance comparisons. Original failed/ineligible attempts remain retained.

The earlier OpenGL/Morrowind startup timeout had a main-thread stack inside Mesa libgallium and never reached input execution. That is a separate unresolved failure. Windows full XISO chunk 17 also reports VK_ERROR_DEVICE_LOST at command.c:117 on BOTH parent and candidate, so it is not an observed newly introduced audio-stack failure.

The Deck is idle, with no queued launches, after diagnostics. No black-screen fix is claimed.
