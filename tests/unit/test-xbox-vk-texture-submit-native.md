<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Vulkan texture submission driver regression

Build `tests/unit/test-xbox-vk-texture-submit-native` with the normal xemu
Vulkan dependencies. The two GLib cases are skipped unless explicitly enabled:

```sh
XEMU_VK_NATIVE_TEST=1 build/tests/unit/test-xbox-vk-texture-submit-native --verbose
```

The fixture selects the first device exposed by the Vulkan loader and prints its
name, type, driver/API versions and validation availability. Select the intended
ICD through the loader environment when multiple devices are available. To
require the installed Khronos validation layer, also set
`XEMU_VK_NATIVE_REQUIRE_VALIDATION=1`; missing validation then fails the test.
Validation errors fail the test. A software ICD run is useful local coverage,
but must be identified separately from a physical-device run.

Each case prepares four real sampled images, forces either descriptor-set or
uniform-buffer capacity during the production descriptor update, then records a
real graphics draw after that submission boundary. The fragment shader packs the
four source red channels into one pixel. All stage pointers are replaced before
submission; the production cache pre-eviction callback and LRU must refuse to
evict the consumed images. The production finish submits and waits on its real
fence, the pixel must equal `{17, 37, 71, 131}`, and actual cache eviction must
then destroy all four images, views and samplers.

The fixture calls production descriptor allocation/update, buffer append, bind,
command begin/finish, queue submission, fence wait, and cache pre/post-eviction.
The tiny wrapper exposes only the file-local descriptor bind. UI debug markers
and profile notifications are no-ops; the guest-report service asserts there are
no reports/queries. The memory-budget service must remain unreachable (the test
finishes only three submissions). No GPU completion or resource-destruction
function is mocked. The replacement image is only a stage-pointer placeholder;
it is never published or sampled.

This is a deterministic lifetime regression, not a game benchmark or timing
contract. Forced capacities and its four-node cache do not estimate game cache
pressure. Existing game/XISO measurements and adverse results remain applicable
independently; this test does not resolve their performance questions.
