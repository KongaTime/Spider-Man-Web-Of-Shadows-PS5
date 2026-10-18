# Web of Shadows PS5 development project

This is work in progress, based on KongaTime's native PS5 ReXGlue platform.
It is not yet a verified playable PS5 port. The source selects the statically
linked Xenos Vulkan backend instead of loading the PC renderer DLL.

The game executable, generated game code, ISO and extracted game data stay in
the private workspace; none are included here. The currently supported input
being developed against is the USA Xbox 360 executable, title ID 41560815,
media ID 638BF87A, SHA-256
3183f0beab0df589137da4430de4ac6716d281fd732b7c6b71d153140add479c.

`tools/build.sh` runs under Linux/WSL with the checked-out dependencies and
generated code prepared in this chat's workspace. It accepts `wos_guest` or
`wos_app` to compile one part, and defaults to linking the `title` target.
Dependency locations can be supplied through the environment variables in
that script. Building requires the PS5 ReXGlue runtime, its Xenos GPU archive,
RADV and the native title converter.

The provisional homebrew title ID is PPSA99301. Game data is expected at
`/app0/gamedata`, saves at `/app0/userdata`, and caches at `/app0/cache`.
Startup diagnostics are written to `/app0/wos-startup.log` and klog.
The first-run importer scans the app folder and its `import/` folder for one ISO,
checks the supported executable and file layout, and extracts into a staging
directory. A completed staging directory becomes `gamedata`; an interrupted
import can resume on the next launch. Transfer the complete ISO while the app
is closed (or upload using a temporary non-ISO extension, then rename it).
The importer also accepts a complete extracted `gamedata` folder. The startup
screen waits for a game file and displays import progress. The development
app has booted on a PS5 and its setup screen was confirmed visible. RADV
created a 3840x2160 swapchain and the runtime detected the controller.
On-console import has completed: all 750 files passed the layout and executable
checks. Missing startup methods and a computed virtual-dispatch bank have been
added to ahead-of-time code generation using verified original boundaries. With
Vulkan fragment shader interlock and synchronous shader compilation, game picture
and audio are confirmed on PS5, and pressing X no longer exits the app. The main
menu, opening cutscene and initial gameplay have been reached. Gameplay is still
consistently slow; performance optimization and save/load validation are pending.
The working renderer is FSI; the framebuffer path produced a black picture.
Guest vsync stays enabled because disabling it accelerated menus without a useful
gameplay improvement. Native timing uses a kernel-anchored invariant CPU counter;
its elapsed-time conversion has been checked against the console clock.

The build uses O3 and ThinLTO for generated native game code. Runtime
logs in `/app0/logs/wos_recomp_*.log` include two-second windows of guest swaps
per second, frame-time percentiles, graphics-thread and frame-producing thread
CPU time, and (when diagnostics are enabled) draw preparation phases. These are host measurements; GPU
execution time is not measured directly. Nested command playback and fence wait
times overlap their calling phases and must not be added to them.

Before rebuilding this diagnostic runtime from the pinned ReXGlue checkout,
apply `patches/rexglue-frame-diagnostics.patch`, followed by
`patches/rexglue-disable-phase-timing.patch` for the current timing-disabled
comparison build. Apply `patches/rexglue-single-range.patch`, then
`patches/rexglue-valid-page-fast-path.patch`, and rebuild
the runtime's `rexgpu-xenos` target. The title notice staging step verifies that
the patch is applied and includes it with the dependency provenance. The patch
for diagnostics adds timing only and leaves the original command stream and waits intact.
The single-range patch removes temporary list allocation and merging from
individual buffer residency requests, retaining the original shared page
checks and upload path. Its result and requested uploads matched the original
routine in 12,000 differential host cases under address/undefined-behavior
sanitizers. The user reported no noticeable gameplay improvement from allocation removal alone.
The host harness is `tools/test-single-range.py`; run it under Linux/WSL with
the patched SDK path and `--work-dir` pointing to a scratch directory. It
compares the actual modified methods with their versions from the SDK's HEAD.

The PS5 valid-page patch adapts the atomic bitmap approach from
[TheSimpsonsGame-PS5-Builder](https://github.com/KongaTime/TheSimpsonsGame-PS5-Builder/blob/main/tools/rexglue-sdk/src/graphics/shared_memory.cpp).
Already-resident buffer requests can return without acquiring the global memory
lock. All runtime validity-word reads and writes use compiler atomics on PS5;
invalid pages retain the existing locked upload path. This passed 12,000
differential cases, 1,000 invalidation/revalidation cycles and an atomic
publication stress check under host address/undefined-behavior sanitizers.
In the harness, 10,000 resident requests took zero global locks. The user
confirmed correct graphics in opening gameplay. The captured tail runs at
15.86–17.55 guest swaps/second; the user could not tell whether it was faster.
Camera and logging differences prevent attributing a speed gain to this patch. See
`validation/repository-performance-review.md` for the log review and limitations.

PS5 settings open and close with **L1 + R1 + Options**. The touchpad is mapped
to the original game's Back button. Apply
`patches/rexglue-ps5-settings-navigation.patch` when rebuilding the runtime UI:
it feeds physical controller navigation into ImGui and blocks guest controller
input while the settings overlay is open. Navigate with the D-pad and select
with Cross; controller navigation has been confirmed on PS5. The original Windows launcher's FSR and frame-generation controls
are not implemented in this PS5 Vulkan integration.

The single-range change passed correctness checks but the user reported no
noticeable speed improvement. Camera tests show roughly 19 frame updates/sec
looking at the floor versus 11–12 looking toward the world, with substantially
more draw calls. A requested 854×480 video mode did not change the game's
1280×720 render dimensions; the console setting has been restored to 1280×720.

The target is now more than 60 game frames/second, which has not been achieved.
Runtime 030 measured about 60–62 ms/frame and 37–38 ms/frame of graphics-thread
CPU work in the street-facing opening scene. Host draw preparation took roughly
23–24 ms/frame, including 8.5–8.9 ms in bindings; these nested values overlap.
There were no completed host occlusion queries in this scene, ruling out their
synchronous readback as the cause of this capture's slowdown.

For the current experiment, apply `patches/rexglue-coarse-frame-costs.patch`
after the timing-disable patch, then `patches/rexglue-texture-descriptor-reuse.patch`.
Then apply `patches/rexglue-command-wait-breakdown.patch`,
`patches/rexglue-command-ring-wait.patch`, and
`patches/rexglue-sampled-draw-costs.patch` in that order for the current probes.
The command-ring patch makes the spin budget
tunable and wakes the worker for queued host callbacks. To reproduce the
short-spin comparison, add `WOS_COMMAND_RING_SPIN_LIMIT=16` to the build
environment; the default is the original 500. Its extracted worker passed
host tests at limits 0, 16 and 500, including pointer updates, callbacks,
signals arriving before waits and shutdown. The PS5 comparison reduced idle
CPU use to roughly 3–4 ms/frame, but the user reported the same speed and the
capture remained around 16 swaps/second. This is not an established FPS gain.
Rebuild the runtime's `rexgpu-xenos` target, then build the title with
`WOS_FRAME_COST_DIAGNOSTICS=ON WOS_REUSE_TEXTURE_BINDINGS=ON bash tools/build.sh title`.
Both switches default to OFF. The new renderer cvars are `wos_frame_costs`
and `wos_reuse_texture_bindings`. Descriptor reuse compares the actual Vulkan
image views, samplers, layouts and counts, keeps the existing validity checks,
and rebinds the current pipeline's sets. It avoids hash-only matching of guest
fetch constants. The extracted decisions passed 20,000 host cases with
streaming changes, cache/frame resets, owner changes, failures and toggling.
These tests use mock Vulkan writes; console picture and timing checks are
required. See `validation/high-frame-rate-work.md` for measured costs and limits.
In runtime 031, the user confirmed correct graphics but similar speed. Roughly
5,200–5,300 stage bindings were reused per heavy frame, while frame intervals
remained around 62–64 ms. No material gameplay speedup was established.

With diagnostics enabled, the title also measures imported kernel waits,
thread delays and critical-section entries on the thread that calls VdSwap.
The wrappers forward the original context and guest memory to the original
implementation exactly once. Their host tests cover forwarding and report
reset with diagnostics enabled and disabled. `tools/check-guest-wrappers.py`
checks the unstripped linked ELF against the private generated import table
to verify that game calls actually reach all ten wrappers. No generated
game source is included in this repository.

The latest diagnostic build samples NtSingle waits at 1/64 calls, records
delay arguments, and observes imported KeSetEvent, NtSetEvent and
KeReleaseSemaphore calls for watched objects. Logs include guest handles,
object types/addresses, requested timeouts, return categories, guest caller
addresses and thread entry points. These are observations, not object
ownership or a complete synchronization trace. Tables are bounded and report
overflow. Rendering detail samples approximately 1/64 draws across eleven
stages; it reports mean host microseconds per sampled draw, not GPU time.

The zero-duration priority change in `patches/rexglue-zero-delay-priority.patch`
is a **rejected experiment**. It passed 720 host branch checks but removing
the normal-priority 100 µs backoff increased object polling dramatically and
reduced opening gameplay to roughly 7–8 swaps/second. The previous backoff is
restored; this patch is excluded from the active dependency list and build.
The patch and test remain for the experiment record, not as a recommended
optimization. See `validation/zero-delay-priority-result.json`.

Deploy the complete title folder, including `sce_sys/icon0.png`, before
publishing `sce_sys/param.json` to the loader. An initial missing icon can
exhaust ShadowMountPlus's registration retries and require a fresh scan with
retry counters reset after the files are corrected.

Platform and Vulkan-loader sources retain their original license notices in
`vendor/`. The link command derives from the MIT-licensed PS5 title foundation;
`tools/wos-pie.ld` adapts the GPL-3.0-or-later native PIE layout and groups
Clang's large-code-model sections into the corresponding load segments.
ReXGlue, RADV, the payload SDK and their dependencies retain their own licenses;
a distributable build must carry their matching notices and required sources.
