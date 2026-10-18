# Runtime 027 and related repository review

Reviewed on 2026-10-18. The supplied log is runtime 027, 14,017,128 bytes and
86,061 lines over approximately 46.8 seconds. This report includes no game data
or console identity information.

The capture contains 84,388 trace rows, 1,224 debug rows, 438 info rows and
11 warning rows, with no error or critical rows. Menus reach about 30 guest
swaps/second; gameplay windows fall to about 11–16. Representative later
windows report 11.60, 11.23, 12.48 and 12.29 swaps/second (roughly 80–89 ms
between guest swaps). The game renders at 1280x720 using fragment shader
interlock and asynchronous shader compilation.

Only guest-frame timing rows are present. This capture does not contain the
detailed draw-phase or graphics-thread timings from earlier diagnostic builds.
Repeated texture loads, resolves and coherency operations are visible, but
message counts do not establish actual transfer bandwidth or GPU execution
time. Trace logging itself adds work; the size of that overhead is unmeasured.
An otherwise identical comparison should use info-level logging, preserving
the guest timing rows. The timing-disabled source changes already present in
the workspace have been retained.

## Relevant repositories

| Repository | Finding | Applicability |
| --- | --- | --- |
| [TheSimpsonsGame-PS5-Builder shared memory](https://github.com/KongaTime/TheSimpsonsGame-PS5-Builder/blob/main/tools/rexglue-sdk/src/graphics/shared_memory.cpp) | Atomic page-validity reads avoid the global memory lock for resident requests. | Adapted as a small PS5-only patch. The streamed-page system and GPU mirror changes have additional dependencies and were not copied. |
| [TheSimpsonsGame-PS5-Builder Vulkan processor](https://github.com/KongaTime/TheSimpsonsGame-PS5-Builder/blob/main/tools/rexglue-sdk/src/graphics/vulkan/command_processor.cpp) | Descriptor reuse, submission instrumentation and batching probes. | Further candidates if residency locking proves insignificant. Requires matching state/invalidation checks before reuse. |
| [PS5_EdgeOfTimeTitle](https://github.com/KongaTime/PS5_EdgeOfTimeTitle) and [PS5_ReXGlueTemplate](https://github.com/KongaTime/PS5_ReXGlueTemplate) | Edge of Time uses a game-specific renderer with Plume and XenosRecomp shaders. | Useful platform reference; replacing the generic Web of Shadows Xenos renderer requires game-specific work. |
| [PS5_Vulkan](https://github.com/KongaTime/PS5_Vulkan) and [rexglue-sdk](https://github.com/KongaTime/rexglue-sdk) | Their current main heads match the already pinned local commits. | No newer upstream head to obtain as a simple driver/runtime update. |
| [PS5_MihawkGen](https://github.com/KongaTime/PS5_MihawkGen) | Frame interpolation implementation. | Not validated for this title. Interpolation does not accelerate simulation or lower the underlying 80–90 ms game-update interval. |

## Adaptation and validation

`patches/rexglue-valid-page-fast-path.patch` follows the all-valid bitmap
approach in Simpsons shared-memory blob
`2b8b74ca7e9a6b2d55736e725f7e669be3789f7e`. It applies after
`rexglue-single-range.patch` to SDK commit
`f47ccdde34b3a992f7872a27619fc588040c229f`.

Backing allocation and input bounds checks remain in place. Every published
validity-word access is atomic on PS5, including writers serialized by the
existing global critical region. Bitmap storage remains fixed during command
processor operation. Invalid requests still enter the existing locked scan and
upload routine. Guest writes still require synchronization with GPU use, as
with the original path. Other platforms retain their locked request scan.

The host harness extracts the actual request methods and atomic helpers from
the SDK source. Address/undefined-behavior sanitizer checks passed:

- 12,000 differential request cases against the original SDK method, including
  boundaries, mixed page validity, overlapping ranges and allocation/upload failures.
- 10,000 resident requests: 10,000 original global-lock acquisitions versus
  zero in the PS5 fast path; 10,000 original temporary allocations versus zero.
- 1,000 invalidation/revalidation cycles: invalid pages request the expected
  upload and acquire the lock; revalidated pages return without the lock.
- Concurrent atomic publication stress check. This is not a ThreadSanitizer
  result or proof of all possible runtime interleavings.

Patch provenance is verified by replaying each patch chain from the pinned
source and comparing the resulting files with the runtime checkout. This
handles overlapping timing and residency patches without skipping validation.

PS5 runtime compilation passed. The title build result and artifact hash are
recorded separately in `valid-page-fast-path.json`. The deployed executable
passed readback verification and the original settings were preserved. The user
confirmed correct graphics in opening gameplay but could not tell whether it
was slightly faster. The last 12 guest timing windows report 15.86–17.55
swaps/second, averaging 16.73 across reported windows, with no error or
critical rows in the capture. Runtime 029 uses info logging, unlike the supplied
trace-level runtime 027. Scene/camera and logging differences make this an
uncontrolled comparison; no patch-specific gain or speedup percentage is
established. See `valid-page-console-result.json` for the sanitized samples.

The next useful comparison is the same stationary street-facing camera view,
with the same settings and info-level logging, before and after this patch.
Capture at least 20 seconds after loading and shader warmup. If improvement is
small, split residency cost into page scanning, write-fault/protection changes
and uploads before considering the larger streamed-page changes.
