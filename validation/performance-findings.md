# PS5 gameplay timing findings

The diagnostic build boots and records both CPU-thread and graphics-stage
timings. The user confirmed reaching gameplay for this capture. The original
game's frame-producing HLE call remains intact; its diagnostic wrapper was
verified in the linked PPC function table. Executable readback passed after
deployment, and the runtime produced all four diagnostic row types.

See `before-range-optimization.log` for the original diagnostic measurements
and frame ranges. The latest build's capture is in `gameplay-diagnostics.log`.
The opening menu is included before the transition into gameplay. Log offsets
are relative to the first diagnostic row; console dates are omitted.

Gameplay samples varied from roughly 9.5 to 16.5 guest swaps per second at
1280×720 as scene workload changed. These are game frame updates, rather than
a measurement of distinct images displayed by the television.

Two representative windows illustrate the workload increase:

| Measured value | Frames 547–572 | Frames 1011–1030 |
| --- | ---: | ---: |
| Guest swaps/sec | 12.92 | 9.70 |
| Mean frame interval | 77.40 ms | 103.11 ms |
| Median interval | 76.79 ms | 98.74 ms |
| 95th percentile interval | 87.45 ms | 121.12 ms |
| Maximum interval | 100.19 ms | 124.61 ms |
| Draw calls/frame | 4,265 | 12,288 |
| Host draw processing, inclusive | 42.16 ms | 87.35 ms |
| Primitive/index processing phase | 12.24 ms | 22.68 ms |
| Buffer residency phase | 10.05 ms | 23.34 ms |
| State and bindings phase | 6.90 ms | 17.06 ms |
| Shader preparation phase | 0.55 ms | 1.24 ms |
| Pipeline lookup/configuration phase | 0.45 ms | 1.07 ms |
| Graphics thread CPU time | 37.41 ms | 69.36 ms |

The log identifies significant host cost in primitive/index processing, buffer
residency, and state/binding preparation. Shader and pipeline preparation are
much smaller in these warmed gameplay windows. It does not support treating
shader compilation as the primary explanation for the sustained slowdown.

The frame-producing thread's CPU time also rose in the heavier section,
reaching about 86.6 ms per frame in its nearby window. The two threads can run
concurrently; their CPU times must not be added as a frame-time estimate.

The next useful source-level target is shared-memory range preparation:
index processing and vertex residency both call that path. Its current
single-range entry forwards to range-list allocation, merging, locking and
page-validity scanning. This is a hypothesis for further profiling, rather
than a measured attribution of those internal operations. State and descriptor
updates are another substantial measured cost.

Limitations:

- Stage durations are host wall times and may include scheduling or internal
  waits. GPU execution timestamps have not been collected.
- Nested Vulkan playback, blocking fence waits and queue submission overlap
  their enclosing stages; they must not be added to the draw totals.
- The measured blocking-fence waits round to 0.00 ms in these windows. This
  does not exclude waits elsewhere or GPU-side serialization.
- CPU-thread windows are independent and may not align at exactly the same
  frame boundary as graphics windows.
- Additional instrumentation has overhead; it has not been isolated in an
  otherwise identical comparison. These scene changes also prevent using this
  capture as a controlled before/after optimization benchmark.
- This establishes logging and performance targets. It does not establish
  acceptable gameplay speed or full-game stability.
