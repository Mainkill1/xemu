# Shader Draw and Object Capture Design

## Approved intent and status

Implement the user's shader-seeded capture concept as a draft code foundation
following the live shader workbench, not a generic named-asset extractor. Keep
one object's geometry separate from all objects a shader is used on or can
influence. The executable foundation has no live capture or preview adapter yet.
The developer README is the detailed integration contract:
`docs/devel/shader-draw-object-capture.md`.

## Three independent relationships

1. Exact shader use -> draw event -> primitive selection.
2. Inferred object relationships and explicit confirmed segment membership.
3. Versioned resource producers/consumers, upstream and downstream.

A draw can contain multiple objects; one object can have multiple draws. Shared
shader, texture, buffer, transform, skinning, or content hashes are not ownership.
A fullscreen operation may influence many objects without containing their meshes.

## Records and rules

`DrawEventKey` carries session/renderer epoch/frame/draw identity, accompanied by
`ShaderScope` title and executable identity. Object consolidation cannot cross
any of those scopes or frame boundaries. Cross-frame resource use is permitted
only with an explicitly retained backing/version in the same context.

`DrawSegmentSummary` retains split provenance and exact primitive selection.
Noncontiguous selections use primitive IDs, not a covering interval. Index
connectivity is a segmentation hint. Unknown, screen-space, and unresolved
suspected-batch segments are returned separately from object candidates.

Only matching nonzero `confirmed_object_id` values establish membership.
They are supplied by user confirmation or verified engine knowledge. Equal
geometry/transform hashes produce suggestions only. This rule supersedes the
initial draft's automatic same-pass hash grouping, which could merge unrelated
instances and even different sessions. Different view transforms do not override
explicitly confirmed membership. Bounds require a matching coordinate-space ID.

`ResourceIdentity` separates guest/debug addresses, canonical backing ID/range,
descriptor digest, and content digest. `ResourceTouch` separates input/output
versions. Version provenance must come from the capture owner, not address/hash
inference. Different typed views can alias the same backing. Partial writes need
per-origin fragments. Unknown/missing/ambiguous/partially covered reads stay
explicit. Read/read and bind-only relationships are not producer edges.

Dependency traversal never consolidates object candidates. Resource reachability
is possible influence or an input dependency, not per-pixel causal proof. Missing
CPU uploads, clears, resolves, blits, earlier frames, or feedback cannot be
invented from the draw-only records. Future adapters must provide them.

## Implementation boundary

This PR provides standalone C++17 identity, admission, segmentation, relationship,
grouping, and directed resource analysis with production-source unit tests.
It does not mutate PGRAPH, issue GPU work, retain live handles, or copy guest RAM.
The analysis windows are limited to 256 draws, 512 segments, 32 resources/draw,
262,144 explicit primitive selections, and 32,768 dependency edges. Triangle-list
segmentation accepts 786,432 indices. Admission failure is visible, not truncation
presented as complete. These limits are not final full-frame capture capacities.

## Future capture/replay requirements

Capture exact bytes and state at the renderer ownership boundary on explicit
request. Preserve raw guest and resolved host resources separately; a fresh
conversion must not erase evidence of stale or incorrectly translated inputs.
Assign request/epoch IDs, enforce owned byte budgets, track backing versions and
aliases, cancel stale requests, and keep analysis off the normal draw path.
Capture pauses/readbacks need declared cost; no zero-overhead capture promise.

Replay needs original partner stages, vertex/inline/index data, all texture
subresources/samplers/constants/state, and destination contents where relevant.
Camera or material substitutions are experiments, not exact replay. Named engine
assets, full skeleton recovery, and exact pixel history are additional work.
