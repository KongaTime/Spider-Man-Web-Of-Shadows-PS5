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

The diagnostic build uses O3 and ThinLTO for generated native game code. Runtime
logs in `/app0/logs/wos_recomp_*.log` include two-second windows of guest swaps
per second, frame-time percentiles, graphics-thread and frame-producing thread
CPU time, and draw preparation phases. These are host measurements; GPU
execution time is not measured directly. Nested command playback and fence wait
times overlap their calling phases and must not be added to them.

Before rebuilding this diagnostic runtime from the pinned ReXGlue checkout,
apply `patches/rexglue-frame-diagnostics.patch` and
`patches/rexglue-single-range.patch` using `git apply`, then rebuild
the runtime's `rexgpu-xenos` target. The title notice staging step verifies that
the patch is applied and includes it with the dependency provenance. The patch
for diagnostics adds timing only and leaves the original command stream and waits intact.
The single-range patch removes temporary list allocation and merging from
individual buffer residency requests, retaining the original shared page
checks and upload path. Its result and requested uploads matched the original
routine in 12,000 differential host cases under address/undefined-behavior
sanitizers. Gameplay performance comparison is pending.
The host harness is `tools/test-single-range.py`; run it under Linux/WSL with
the patched SDK path and `--work-dir` pointing to a scratch directory. It
compares the actual modified methods with their versions from the SDK's HEAD.

PS5 settings open and close with **L1 + R1 + Options**. The touchpad is mapped
to the original game's Back button. Apply
`patches/rexglue-ps5-settings-navigation.patch` when rebuilding the runtime UI:
it feeds physical controller navigation into ImGui and blocks guest controller
input while the settings overlay is open. Navigate with the D-pad and select
with Cross. The original Windows launcher's FSR and frame-generation controls
are not implemented in this PS5 Vulkan integration.

The single-range change passed correctness checks but the user reported no
noticeable speed improvement. Camera tests show roughly 19 frame updates/sec
looking at the floor versus 11–12 looking toward the world, with substantially
more draw calls. A requested 854×480 video mode did not change the game's
1280×720 render dimensions; the console setting has been restored to 1280×720.

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
