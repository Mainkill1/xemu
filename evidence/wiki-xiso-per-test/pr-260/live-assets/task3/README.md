# Assembly viewport fixture

A native SDL3/OpenGL3.3 fixture on a local virtual display checks two independently textured parts in one shared bound, red/blue UV sampling, the separate green part, GL state restoration with hostile scissor/logic/clip/unpack settings, invalid dimensions/camera, texture budget rejection,24-thumbnail eviction and explicit shutdown. Strict and ASan/UBSan runs passed. Leak detection is disabled under tracing.

Before implementation, the declared viewport returned texture0 and the native assertion failed. The initial sandbox run could not connect to the display; the same fixture was then run with approved local-display access and reached that expected runtime failure.

Reported cached512-square fixture throughput measures these simple parts only. It does not establish PGR2 viewport cadence, game overhead, skinning, original-stage replay, or player-car recognition. GPU caches hold weak input references and bounded owned buffers/textures. The diagnostic material uses captured base2D textures; original shader arithmetic/state are not claimed.
