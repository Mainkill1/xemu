# Advanced Shortcut Planned Testing

> **Status:** Planned testing. Nothing on this page is accepted merely because it has a menu design or an implementation issue.  
> **Audited baseline:** [`e1a41d1ee67f03874e62f160fec1ed2784600f77`](https://github.com/Mainkill1/xemu/commit/e1a41d1ee67f03874e62f160fec1ed2784600f77)  
> **Coordination issue:** [#100 — Advanced shortcut controls and planned qualification matrix](https://github.com/Mainkill1/xemu/issues/100)

This page tracks host-side shortcuts that should be testable from one xemu executable. The purpose is regression isolation: a developer can select the optimized route or its maintained reference route without reverting commits, rebuilding unrelated branches, or weakening Xbox-visible correctness rules.

The underlying audit deliberately searches beyond obvious bit shifts. In an emulator, the expensive shortcuts are often dirty-state checks, generation counters, cache lookups, hashing, invalidation, polling, GPU synchronization, format conversion, and broad flush operations. A candidate is valuable only when its likely frequency, cost, realistic reduction, and affected workload justify testing.

## Safety boundary

A setting may select a host implementation policy:

```text
Xbox-visible input/state
    -> mandatory validation, coherency, lifetime, and capability checks
    -> host policy gate
         optimized route
         or
         maintained reference route
    -> equivalent Xbox-visible result
```

A setting must never decide whether required emulation work is correct.

The following are not optional performance controls:

- DMA A/B remap invalidation.
- Texture, palette, and vertex source-address validation.
- Surface overlap, ownership, lifetime, and freshness checks.
- GPU-authored surface readback before CPU vertex consumption.
- Complete strided vertex bounds and dirty-page publication.
- Disabled texture-stage dirtiness retention and failed-bind retry state.
- Scanout lifetime, draw time, guest frame time, address, line offset, dimensions, scale, interlace, PVIDEO, pending-upload, and recreation checks.
- Vulkan/OpenGL device identity, UUID, extension, and import/export capability checks.
- Original wait-handle readiness, timeout semantics, and timer-failure fallback.
- Exact divisibility proof before integer QPC conversion.
- Guest-visible report/query ordering and synchronization.

`Enabled` means **permit the shortcut after every existing guard passes**. It never means **force the shortcut past a guard**.

## Work index

| Order | Work item | Requested modes | Apply model | Status |
|---:|---|---|---|---|
| 1 | [#93 — Advanced settings v2](https://github.com/Mainkill1/xemu/issues/93) | Typed policy infrastructure | Mixed live/restart | **Planned** |
| 2 | [#94 — Shortcut evidence and ABBA qualification](https://github.com/Mainkill1/xemu/issues/94) | CLI overrides and evidence export | Session | **Planned** |
| 3 | [#95 — Clean Vulkan texture-stage skipping](https://github.com/Mainkill1/xemu/issues/95) | `Auto / Disabled` | Live | **Planned** |
| 4 | [#96 — Unchanged Vulkan scanout reuse](https://github.com/Mainkill1/xemu/issues/96) | `Auto / Disabled` | Live plus reuse-key invalidation | **Planned** |
| 5 | [#97 — Shader cache mode](https://github.com/Mainkill1/xemu/issues/97) | `Off / Process only / Persistent exact SPIR-V` | Partial live; persistent upgrade may require restart | **Planned** |
| 6 | [#98 — Host wait policy](https://github.com/Mainkill1/xemu/issues/98) | `Auto / Low latency / Lower CPU use` | Live | **Planned** |
| 7 | [#99 — Host clock conversion diagnostic](https://github.com/Mainkill1/xemu/issues/99) | `Auto / Generic reference` | Restart; developer-only | **Planned** |
| Epic | [#100 — Coordination and qualification matrix](https://github.com/Mainkill1/xemu/issues/100) | All of the above | N/A | **Open** |

Existing or held work is deliberately not duplicated:

| Existing owner / held branch | Disposition |
|---|---|
| [#72 — Vulkan adapter/presentation transport](https://github.com/Mainkill1/xemu/issues/72) | Keep beside renderer selection; do not duplicate as a generic performance toggle. |
| [PR #71 — Hybrid ubershader](https://github.com/Mainkill1/xemu/pull/71) | Experimental only, default Off, until cold-tail and correctness qualification is complete. |
| [PR #83 — Descriptor capacity](https://github.com/Mainkill1/xemu/pull/83) | Developer experiment only while wait relocation remains unresolved. |
| [PR #85 — Surface/vertex freshness](https://github.com/Mainkill1/xemu/pull/85) | Correctness work, not a user-selectable shortcut. |
| [PR #87 — Fresh-page vertex direct writes](https://github.com/Mainkill1/xemu/pull/87) | Hold until stacked lifetime and freshness proof is complete. |
| [PR #89 — Bounded vertex versions](https://github.com/Mainkill1/xemu/pull/89) | Hold until rollover, lifetime, and representative-title proof is complete. |

## Planned menu organization

```text
Machine
└── Settings
    └── Advanced
        ├── Host
        │   └── Host wait policy                         #98
        ├── Command processing
        │   └── Existing bulk packet control
        ├── Vulkan
        │   ├── Existing fence/color/vertex controls
        │   ├── Skip clean texture stages               #95
        │   ├── Reuse unchanged scanout                  #96
        │   └── Existing transient-buffer control
        ├── Shader
        │   └── Shader cache mode                        #97
        ├── OpenGL
        │   └── Existing native S3TC control
        └── Developer diagnostics
            └── Host clock conversion                    #99
```

## Shared settings contract — issue #93

The current boolean model cannot truthfully represent backend availability, `Auto`, restart-latched changes, or a requested mode that differs from the active mode.

The planned resolution vector is:

```text
saved configuration + non-persistent CLI override
    -> normalize requested policies
    -> resolve platform/backend/capability support
    -> preserve startup-latched state when required
    -> publish one coherent effective bit/mode snapshot
    -> hot paths consume a cheap lock-free value
    -> UI and evidence report requested/effective/reason/restart state
```

Representative types:

```c
typedef enum XemuTweakPolicy {
    XEMU_TWEAK_POLICY_AUTO = 0,
    XEMU_TWEAK_POLICY_DISABLED,
    XEMU_TWEAK_POLICY_ENABLED,
} XemuTweakPolicy;

typedef struct XemuTweakRuntimeState {
    XemuTweakPolicy requested;
    bool effective;
    bool restart_pending;
    XemuTweakAvailability availability;
    const char *reason;
} XemuTweakRuntimeState;
```

The worker-facing fast path remains compact:

```c
typedef uint64_t XemuTweakBits;
extern _Atomic XemuTweakBits xemu_tweaks_effective_bits;

static inline bool xemu_tweak_enabled(XemuTweak tweak)
{
    XemuTweakBits bits = atomic_load_explicit(
        &xemu_tweaks_effective_bits, memory_order_relaxed);
    return (bits & (UINT64_C(1) << tweak)) != 0;
}
```

No draw, texture-stage, fence, clock-read, or wait path should parse configuration strings, allocate status objects, acquire a UI lock, or construct JSON.

## Shared evidence contract — issue #94

A benchmark is invalid unless it proves that the requested route became effective and the workload reached the target path.

```text
saved settings
    -> apply in-memory CLI overrides
    -> resolve effective profile
    -> run fixed guest work
    -> subsystem-owned counters record route attempts and outcomes
    -> low-frequency snapshot/export
    -> validate identity, order, reachability, and correctness
    -> only then calculate a performance result
```

Suggested CLI:

```text
xemu.exe \
  -xemu-tweak vk_skip_clean_texture_stages=disabled \
  -xemu-tweak vk_reuse_unchanged_scanout=auto \
  -xemu-shortcut-evidence C:\evidence\run-a.json
```

CLI overrides must not rewrite the user's saved configuration. Unknown keys, unknown values, or malformed assignments must fail loudly.

Minimum evidence identity:

```json
{
  "schema": "xemu-shortcut-evidence/v1",
  "commit": "<40-character SHA>",
  "executable_sha256": "<SHA-256>",
  "build_type": "release",
  "platform": "windows-x86_64",
  "renderer": "vulkan",
  "workload": "pgr2-snapshot-city",
  "order_group": "ABBA-01",
  "position": 1
}
```

Preferred run order:

```text
A -> B -> B -> A
```

and, where practical:

```text
B -> A -> A -> B
```

Reject the comparison before interpreting percentages when:

- commits or executable hashes differ unexpectedly;
- renderer, GPU/driver, workload, snapshot, or fixed progress differs;
- requested mode did not become effective;
- restart is pending;
- the target attempt/eligibility counter is zero;
- the reference run did not enter the maintained route;
- a correctness oracle differs;
- required validation output is missing.

## Planned shortcut — clean Vulkan texture stages (#95)

**Classification:** Host performance optimization.  
**Source seam:** `hw/xbox/nv2a/pgraph/vk/texture.c`, `pgraph_vk_bind_textures()`.  
**Default:** `Auto`.  
**Apply:** Live.

Current optimized vector:

```text
slow texture bind entered because at least one stage may need work
    -> inspect one stage
    -> require clean guest state
    -> require a real non-dummy existing binding
    -> reject possibly-dirty VRAM or palette data
    -> reject surface-backed source and texture/palette overlap
    -> revalidate texture and palette DMA-derived physical addresses
    -> all guards pass
    -> skip rebuilding this stage
```

Planned reference vector:

```text
same slow bind and same guards
    -> stage is eligible for reuse
    -> policy is Disabled
    -> record forced-reference case
    -> fall through to the maintained create/bind route
```

Policy gate sketch:

```c
if (clean_stage_reuse_allowed) {
    stats->clean_stage_eligible++;

    if (xemu_tweak_enabled(
            XEMU_TWEAK_VK_SKIP_CLEAN_TEXTURE_STAGES)) {
        stats->clean_stage_skips++;
        continue;
    }

    stats->clean_stage_forced_reference++;
}
```

The real implementation must preserve the exact current order and side effects. In particular, do not call dirty-test-and-clear helpers twice merely to collect a counter.

Planned reachability counters:

```text
vk.texture_bind.slow_bind_calls
vk.texture_bind.stage_checks
vk.texture_bind.clean_stage_eligible
vk.texture_bind.clean_stage_skips
vk.texture_bind.clean_stage_forced_reference
vk.texture_bind.bind_failures
```

Required gates:

- Existing disabled-stage and failed-bind retry tests.
- New four-combination policy helper test.
- DMA A/B remap, palette remap, and surface-to-texture source oracles.
- PGR2 snapshot and fresh start.
- Morrowind snapshot.
- Complete Vulkan XISO suite.
- No output/hash difference and no new validation message.

A run intended to test this shortcut is invalid when `clean_stage_eligible == 0`.

Related history: [#79](https://github.com/Mainkill1/xemu/issues/79) and [PR #80](https://github.com/Mainkill1/xemu/pull/80).

## Planned shortcut — unchanged Vulkan scanout (#96)

**Classification:** Host performance optimization.  
**Source seam:** `hw/xbox/nv2a/pgraph/vk/display.c`.  
**Default:** `Auto`.  
**Apply:** Live; a policy change invalidates the retained reuse key on the renderer thread.

Optimized vector:

```text
display update requested
    -> execution mode permits reuse
    -> PVIDEO is inactive
    -> no surface upload is pending
    -> display resources were not recreated
    -> compare retained output key:
         surface lifetime identity
         surface draw time
         guest frame time
         scanout address
         VGA line offset
         width and height
         surface scale
         interlace mode
    -> all fields match
    -> return the completed retained output
```

Reference vector:

```text
same guards and same key comparison
    -> reuse is eligible
    -> policy is Disabled
    -> record forced-reference case
    -> execute fresh conversion/submission
    -> publish a newly completed output and key
```

Final gate sketch:

```c
if (reuse_eligible) {
    stats->reuse_eligible++;

    if (xemu_tweak_enabled(
            XEMU_TWEAK_VK_REUSE_UNCHANGED_SCANOUT)) {
        stats->reuse_hits++;
        return;
    }

    stats->reuse_forced_reference++;
}

render_display(pg, surface, line_offset);
```

The setting does not weaken key construction or invalidation. The first update after either live policy transition must be freshly produced.

Required gates:

- Pure policy helper and every reuse-key field.
- Static dashboard/menu repeated frames.
- PVIDEO/video playback.
- Progressive/interlaced transitions.
- Resolution, line-offset, scale, and geometry changes.
- Same-address surface destruction and recreation.
- Pending upload, display-resource recreation, title exit, and renderer teardown.
- PGR2, Morrowind, and complete Vulkan XISO.

A reuse benchmark is invalid when `reuse_eligible == 0`.

Related investigation: [#90](https://github.com/Mainkill1/xemu/issues/90).

## Planned policy — shader cache modes (#97)

**Classification:** Host performance optimization.  
**Source seams:** `config_spec.yml`, `ui/xui/main-menu.cc`, `hw/xbox/nv2a/pgraph/vk/shaders.c`, and `spirv-prewarm.[ch]`.  
**Default:** `Persistent exact SPIR-V`, preserving the current default behavior.

Mode vectors:

```text
Off
    -> no persistent artifact lookup or publication
    -> no optional process-cache admission
    -> normal compiler fallback remains available
    -> renderer-required live objects keep normal lifetimes

Process only
    -> use in-memory exact-source/module/pipeline reuse
    -> no persistent disk read or write
    -> compile on process miss

Persistent exact SPIR-V
    -> use in-process reuse
    -> try validated exact-source persistent artifact
    -> compile normally on miss or rejection
    -> publish only an eligible validated artifact
```

Migration:

```text
cache_shaders: true  -> persistent
cache_shaders: false -> off
new key present      -> new key wins
both missing         -> persistent
```

Named predicates prevent scattered integer checks:

```c
static inline bool shader_cache_process_enabled(
    XemuShaderCacheMode mode)
{
    return mode >= XEMU_SHADER_CACHE_PROCESS;
}

static inline bool shader_cache_persistent_enabled(
    XemuShaderCacheMode mode)
{
    return mode == XEMU_SHADER_CACHE_PERSISTENT;
}
```

A persistent hit is only an optimization. Missing, corrupt, ABI-incompatible, target-incompatible, or compiler-policy-incompatible records must reach normal compilation.

Required gates:

- Old/new configuration migration and round-trip tests.
- Off/Process/Persistent decision tests.
- Invalid checksum, structure, ABI, target, and compiler-policy records.
- Empty and read-only cache directories.
- Live downgrade and restart-required persistent upgrade.
- Cold and warm PGR2/Morrowind campaigns.
- Cold and warm full Vulkan XISO campaign.
- Renderer teardown, writeback, partial-file, and publication behavior.

Related implementation: [PR #70](https://github.com/Mainkill1/xemu/pull/70).

## Planned policy — host wait strategy (#98)

**Classification:** Host wait/power policy.  
**Source seams:** `util/qemu-timer.c`, `include/qemu/timer.h`, `ui/xemu-tweaks.c`, and Windows poll tests.  
**Modes:** `Auto / Low latency / Lower CPU use`.  
**Apply:** Live at the next wait.

Lower-CPU vector:

```text
original GPollFD handles + nanosecond timeout
    -> reject alertable/re-entrant use
    -> check unsupported/backoff state
    -> create or reuse one thread-local high-resolution waitable timer
    -> copy original handles and append the timer handle
    -> recompute remaining deadline
    -> arm a rounded-up relative timeout
    -> wait on timer and original handles together
    -> cancel/drain timer lifecycle state
    -> expose only original-handle readiness or timeout
```

Failure vector:

```text
create / arm / cancel / re-entry failure
    -> record the exact reason
    -> preserve unsupported or retry-backoff state
    -> close any timer whose reuse safety is unknown
    -> return an internal fallback indication
    -> execute compatibility wait with the original handles
```

Proposed API:

```c
typedef enum QemuPollWaitPolicy {
    QEMU_POLL_WAIT_AUTO = 0,
    QEMU_POLL_WAIT_LOW_LATENCY,
    QEMU_POLL_WAIT_LOWER_CPU,
} QemuPollWaitPolicy;

void qemu_poll_set_wait_policy(QemuPollWaitPolicy policy);
QemuPollWaitPolicy qemu_poll_get_wait_policy(void);
```

Required gates:

- Zero, finite sub-millisecond, long, and infinite waits.
- One and multiple handles, including a non-first signaled handle.
- Immediate readiness, timeout-only, auto-reset, and multiple-ready behavior.
- Live policy transitions and concurrent thread lifetimes.
- Injected unsupported, transient create, arm, cancel, and re-entry failures.
- Windows handle-count boundary.
- OpenGL and Vulkan XISO, PGR2, and Morrowind.
- Host CPU and p50/p95/p99/max latency reported together.

A timer failure may reduce optimization coverage. It may not lose an event, become a timer-only sleep, return early, or hang.

Related work: [#19](https://github.com/Mainkill1/xemu/issues/19), [#20](https://github.com/Mainkill1/xemu/issues/20), and [PR #25](https://github.com/Mainkill1/xemu/pull/25).

## Planned diagnostic — host clock conversion (#99)

**Classification:** Host performance optimization retained with a developer reference route.  
**Source seams:** `util/qemu-timer-common.c` and inline `get_clock()` in `include/qemu/timer.h`.  
**Modes:** `Auto / Generic reference`.  
**Apply:** Restart required.

Auto vector:

```text
QueryPerformanceFrequency
    -> frequency is positive and divides one second exactly
         store exact nanoseconds-per-tick scale
         QPC ticks -> integer multiply
    -> otherwise
         QPC ticks -> muldiv64 generic reference
```

Generic-reference vector:

```text
QPC frequency discovered
    -> ignore an available exact scale for route selection
    -> use muldiv64
```

There is deliberately no `Force exact` mode. Integer division of one second by a non-divisible frequency truncates and changes time.

Capability and active route should be separate:

```c
extern int64_t clock_freq;
extern uint64_t clock_exact_ns_per_tick; /* immutable capability */
extern uint64_t clock_ns_per_tick;       /* startup-latched active route */
```

`get_clock()` should retain its one existing route branch. Do not add a live settings lookup, mutex, or extra atomic load to every clock read.

Required gates:

- Synthetic 10 MHz, 1 MHz, 1 GHz, non-divisible, zero, and boundary frequencies.
- Exact-versus-generic integer equality for exact-capable frequencies.
- Fixed, boundary, pseudorandom, and monotonic tick sequences.
- Profile proof that Auto removes `muldiv64`/128-bit division from an exact-capable clock route.
- Profile proof that Generic reference reaches the maintained route.
- PGR2 lag snapshot/fresh start, Morrowind, and fixed-work GL/Vulkan XISO.
- No guest timer, timeout, ordering, or progression difference.

Related investigation: [#19](https://github.com/Mainkill1/xemu/issues/19).

## Shared qualification matrix

| Gate | Texture #95 | Scanout #96 | Shader #97 | Wait #98 | Clock #99 |
|---|---:|---:|---:|---:|---:|
| Pure/focused unit test | Required | Required | Required | Required | Required |
| Failure/rejection path | Retry/remap | Invalidation/recreation | Invalid artifact/fallback | Timer API fallback | Non-divisible frequency |
| Same executable | Required | Required | Required | Required | Required; separate starts |
| ABBA/BAAB ordering | Required | Required | Required | Required | Required |
| Requested/effective export | Required | Required | Required | Required | Required |
| Reachability proof | Counters | Counters | Layer counters | Route counters | Route/profile proof |
| PGR2 snapshot/start | Required | Required | Cold/warm | Required | Required |
| Morrowind snapshot | Required | Required | Cold/warm | Required | Required |
| Full XISO | Vulkan | Vulkan | Vulkan cold/warm | OpenGL + Vulkan | OpenGL + Vulkan |
| Output/validation oracle | Required | Required | Required | Required | Required |
| CPU/resource report | Required | Required | Required | Required | Required |
| p95/p99/max tails | Required | Required | Required | Required | Required |

## Intern implementation order

Use this order inside each feature issue:

```text
1. Read the exact source seam and existing focused tests.
2. Add a pure helper or decision test before changing behavior.
3. Add configuration migration and requested/effective policy resolution.
4. Gate only the final optimized decision; preserve every correctness guard.
5. Add subsystem-owned integer counters without hot-path formatting or I/O.
6. Add the UI row and truthful availability/restart status.
7. Run focused unit and failure-path tests.
8. Run same-executable ABBA/BAAB qualification.
9. Store raw evidence under the repository evidence hierarchy.
10. Update this page from Planned to the measured disposition.
```

For every implementation PR, document:

```text
Location:
Classification:
Requested mode:
Effective mode:
Optimized vector:
Reference vector:
Reachability counters:
Correctness oracles:
Performance metrics:
Identity:
Decision:
```

## Completion definition

This planned-testing program is complete only when:

- [ ] #93 and #94 are implemented and documented.
- [ ] Each normal shortcut has its own independently reviewable PR.
- [ ] Old saved booleans migrate without inversion.
- [ ] Requested, available, effective, reason, and restart state are visible.
- [ ] Every accepted shortcut retains a maintained reference route.
- [ ] Every accepted shortcut has nonzero reachability evidence.
- [ ] Focused oracles, representative titles, and required XISO gates pass.
- [ ] Raw evidence is stored under the repository evidence hierarchy.
- [ ] This page is updated from **Planned** to **Accepted**, **Developer-only**, **Hold**, or **Removed** for each item.

Do not add one master `Fast mode` switch. Do not expose correctness guards as performance options. Do not promote held experiments merely because this page exists.
