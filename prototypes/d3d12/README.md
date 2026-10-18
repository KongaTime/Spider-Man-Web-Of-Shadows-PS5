# Native PS5 D3D12 experiment

This separate PPSA99302 title tests **vkd3d-proton → Vulkan/RADV**. It does not
change the PPSA99301 game, import game files, or implement a faster game build.

Console probe 004 created a D3D12 device, reported ROV support and resource
binding tier 3, copied 1,024 words with no mismatch, and rendered ten overlapping
ROV instances into a 4×4 float image. All sixteen pixels matched 1023. It then
presented 240 D3D12-written frames through native `VK_KHR_display`/VideoOut and
read back the expected BGRA pixel from the final swapchain image. See
[`../../validation/d3d12-prototype-result.json`](../../validation/d3d12-prototype-result.json).

The presentation test drains the queue and waits after every frame. It is a
correctness test, **not a performance benchmark or a claim of 60 FPS gameplay**.
The display mode used by this run was 3840×2160 at 59.94 Hz; this does not
establish whether the screen supports 120 Hz. Visual confirmation is separate
from the successful API calls and pixel readback.

## Reproduce

Use WSL/Linux with Python 3, Git, Meson, Ninja, Clang, glslang, and `widl`
(the `mingw-w64-tools` package supplies `x86_64-w64-mingw32-widl`). Obtain the
existing PS5 payload SDK, PS5_Vulkan native tool/runtime, and matching RADV
archive described by this project's normal dependency setup. This prototype
does not download Sony SDK material.

The default paths below match this development workspace. For another layout,
pass explicit paths to `prepare-vkd3d.py` and set `PS5_VULKAN_ROOT`,
`PS5_PAYLOAD_SDK`, `VKD3D_ROOT`, `VKD3D_BUILD`, and `PS5_D3D12_BUILD_DIR` for
the title builder. `VKD3D_BUILD` is the generated Meson directory ending in
`/build`; `PS5_D3D12_BUILD_DIR` is an independent scratch/output folder.

```sh
git clone https://github.com/HansKristian-Work/vkd3d-proton work/deps/vkd3d-proton-probe
git -C work/deps/vkd3d-proton-probe checkout 0bb64c56c8f0dcc11ecc0f6c56e00e4f7bd8d506
git -C work/deps/vkd3d-proton-probe -c core.longpaths=true submodule update --init --recursive
python3 outputs/PS5_WebOfShadowsTitle/prototypes/d3d12/prepare-vkd3d.py \
  --source work/deps/vkd3d-proton-probe --build work/wos/vkd3d-ps5 \
  --sdk work/deps/PS5_EdgeOfTimeTitle/.deps/native/ps5-payload-sdk \
  --vulkan work/deps/PS5_Vulkan --widl /usr/bin/x86_64-w64-mingw32-widl
D3D12_PROBE_SOURCE=device-probe.c bash outputs/PS5_WebOfShadowsTitle/prototypes/d3d12/build-prerequisites.sh
```

Without `D3D12_PROBE_SOURCE`, the builder produces only the Vulkan prerequisite
inventory. Deploy either variant only as PPSA99302 after checking that ID is
unused. The title builder performs no console deployment. Preserve the working
game and its settings; never rename this prototype over the game executable.

The native synchronization implementation can be checked on the host:

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  outputs/PS5_WebOfShadowsTitle/prototypes/d3d12/test-sync.c -o /tmp/wos-sync-test
/tmp/wos-sync-test
```

## Scope and limitations

The patch adds a native static build configuration, PS5 monotonic clock and
thread-name handling, 64-bit file positions, an atomic no-replace cache-file
link, and in-process events/semaphores instead of Linux eventfd. The caller
supplies RADV's `vk_icdGetInstanceProcAddr` directly to `vkd3d_create_device`.
No Wine runtime or DXVK D3D11 layer is used.

This is not a complete Windows D3D12/DXGI replacement. Windows DLL loading,
DXGI waitable-handle duplication, cross-process sharing and arbitrary external
HANDLE values remain unsupported. The events are private objects belonging to
this implementation. PS5's generic upstream thread-ID diagnostic still reports
zero; thread naming is native. Tests cover only the listed operations and do
not establish broad D3D12 or shader-model conformance.

The WoS D3D12 backend still needs native provider/presenter integration,
Windows platform dependencies removed or adapted, game shaders exercised, and
a comparison of frame time against the working Vulkan renderer. More than
60 game updates per second also needs correct guest timing. No game-speed
multiplier is included here.

## Sources and licenses

- [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton/tree/0bb64c56c8f0dcc11ecc0f6c56e00e4f7bd8d506),
  LGPL-2.1-or-later. `vkd3d-proton-ps5.patch` and `vkd3d_ps5_sync.h` belong
  with the matching source. `extract-rov-shaders.py` extracts two LGPL test
  shaders from that pin; the shader arrays are not game assets.
- [PS5_Vulkan](https://github.com/KongaTime/PS5_Vulkan/tree/5b5e4fc2d80fdb67a4f61ba9e6a8a424026bb8a5), pinned by the main
  project. The title link recipe and native display patterns retain their
  GPL-3.0-or-later notices in the relevant sources.
- Prototype files carry individual SPDX identifiers. The enclosing project's
  license does not replace dependency licenses. Do not redistribute a linked
  binary without the matching notices and source obligations for vkd3d,
  RADV/Mesa, the SDK/runtime, and shader translators.
