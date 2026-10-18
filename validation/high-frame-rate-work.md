# Work toward more than 60 FPS

The target is actual game frame updates above 60/second. It has not been
achieved. At about 16.7 updates/second the frame interval is roughly 60 ms;
60 FPS requires 16.67 ms, or about 3.6 times the current throughput. Rates
above 60 require still less time. Displaying more than 60 distinct pictures
also requires a suitable display and output mode; the user is unsure whether
their screen supports 120 Hz. Display settings remain unchanged.

## Current bottleneck measurements

The user confirmed runtime 030 was in opening gameplay looking toward the
street/buildings. The tail of the capture records:

| Measurement | Representative tail range |
| --- | --- |
| Guest/graphics swaps | 16.1–16.7 per second |
| Frame interval | 59.9–62.1 ms |
| Graphics-thread CPU | 36.8–38.1 ms/frame |
| Draw calls | 7,213–7,583/frame |
| Host draw preparation | 23.4–24.5 ms/frame |
| Primitive processing within draw preparation | 0.65–0.69 ms/frame |
| Bindings within draw preparation | 8.52–8.93 ms/frame |
| Residency within draw preparation | 0.84–1.04 ms/frame |
| Completed host occlusion queries | 0/frame |
| Measured blocking fence waits | 0.00 ms/frame at displayed precision |
| Nested command playback | 2.03–2.10 ms/frame |
| Queue submission call | 0.18–0.58 ms/frame |

These are host wall intervals and graphics-thread CPU measurements. They are
not GPU hardware timestamps. Draw, bindings, residency and playback timings
nest; adding them produces an invalid frame-cost estimate. Guest and graphics
windows are independently reported and may have different boundaries.
CPU times from concurrent threads must not be added as elapsed frame time.
Coarse instrumentation overhead is not yet isolated in a controlled comparison.

The source performs synchronous completion for host occlusion queries, but
this scene issues no completed queries, so that hypothesis is ruled out for
this capture. Binding preparation is a measured CPU target. Even eliminating
all of its measured cost would leave substantial graphics-thread work beyond
the 60 FPS budget; this optimization alone cannot establish the target.

## Exact texture descriptor reuse experiment

The existing runtime unconditionally invalidates texture descriptor values on
every draw. The experiment retains the actual descriptor contents from the
last successful write for each shader stage, and compares every image-view
handle, sampler handle, image layout, descriptor-set layout and descriptor count.
It only reuses a set whose existing runtime validity bit was already set.
It still rebinds the set under the current pipeline layout, so it does not rely
on a previous pipeline remaining compatible.

Snapshots are bounded to two shader stages per GPU command thread and reset
when their owning command processor changes. Turning the experiment off
invalidates the snapshot owner, so later re-enabling cannot reuse a snapshot
that stopped following actual descriptor writes. Cache/frame invalidation and
allocation failures leave missing descriptors invalid and force fresh writes.
Snapshots are committed only after successful allocation and descriptor writes.

This differs from a removed hash-based attempt documented in the Simpsons
repository: its source warns that missed inputs caused streamed characters to
use incorrect textures. No guest-address-only or shader-identity-only key is
used here. The actual host descriptor inputs are compared instead.

`tools/test-texture-descriptor-reuse.py` extracts the actual comparison,
decision and publication code from the modified SDK. Under ASan/UBSan, 20,000
mock-driver cases passed with changing views, samplers and layouts, empty and
changed binding counts, frame resets, multiple command processors, allocation
failures, and off/on transitions. The harness observed 29,353 stage reuses and
13,156 stage writes. This establishes those modeled decisions, not PS5 driver
correctness or a gameplay speedup.

The opt-in coarse timing helper also passed tests for disabled/no-clock behavior,
nested scopes, explicit stop, early returns, report intervals and reset. A
native runtime compile passed. Title and console results are recorded in
`descriptor-reuse-result.json` once available. Runtime 030's sanitized rows are
in `coarse-before-descriptor-reuse.json`.

Runtime 031 confirmed correct graphics, including the user-requested texture
check, but the user reported the same apparent speed. In its later heavy
windows, roughly 5,200–5,300 stage bindings/frame were reused and 1,260–1,410
were written. Bindings took 7.5–7.9 ms/frame and guest frame intervals stayed
around 62–64 ms. Scene/workload differences prevent treating the phase change
as a controlled speedup. This remains an opt-in experiment, with no material
gameplay FPS improvement established. Full-game/streaming validation is pending.

## Command wait breakdown

The next diagnostic patch separates command-ring-empty wall time and CPU time
from WAIT_REG_MEM packet time, its nested polling sleeps, and nested coherency
work. This avoids treating an empty command ring as a GPU fence stall. It also
records swap/copy work and submission-fence checking/reclamation in the Vulkan
backend. Ring CPU time helps distinguish useful graphics work from CPU spent
spinning while waiting for the game to supply more commands.

The existing waits, command ordering, vblank frequency, synchronization and
game timing are unchanged. These counters are enabled only for the profiling
build. Their extracted host tests passed disabled/no-clock behavior, nested
interval accounting, ring CPU time, report intervals and state reset under
ASan/UBSan. Measurements must be interpreted as nested host intervals; they
do not establish GPU execution time.

Runtime 032 measured roughly 24–27 ms/frame in an empty command ring, of which
about 6.1–6.5 ms was CPU time. WAIT_REG_MEM packets cost about 0.02 ms/frame,
with no polling sleeps in the representative tail. Nested coherency cost about
0.01 ms/frame; swap work about 1.2 ms and copy work about 3.1–3.7 ms.
This identifies idle spinning as CPU work, not a GPU-fence stall. Legitimate
producer work can still account for the ring remaining empty. These results
are in `wait-breakdown-result.json` and `coarse-before-ring-wait.json`.

## Short-spin command-ring experiment

The existing worker yields 501 times before blocking on the command event.
`rexglue-command-ring-wait.patch` exposes the existing threshold as a bounded
cvar (0–500), preserving 500 by default. The current comparison uses 16,
then the same event wait with the same 5 ms timeout. `UpdateWritePointer`
already signals this event. Queued host callbacks now also signal it, so an
earlier-blocking worker does not leave callbacks waiting for the timeout.
Guest vblank, register waits, fences and command ordering are unchanged.

`tools/test-command-ring-wait.py` extracts the actual worker, pointer update
and callback enqueue methods. With mock host events/queues/packet execution,
ASan/UBSan checks passed 6,003 pointer updates and 600 callbacks across spin
limits 0, 16 and 500, including pre-signaled events and shutdown. These are
modeled event/wakeup tests; they are not PS5 timing measurements or a proof
of all possible interleavings. Console results belong in
`command-ring-wait-result.json`. Reducing idle CPU time alone does not imply
a gameplay frame-rate gain.

Runtime 033 remained around 15.7–15.8 swaps/second in the representative
tail. Ring-empty CPU time fell to about 3.6–4.2 ms/frame, with 29–34 ms/frame
of ring-empty wall time. The user confirmed correct behavior and the same
apparent speed. This saves idle CPU work without an established gameplay FPS
gain. Scene differences also prevent interpreting these as a controlled
before/after benchmark. Sanitized rows are in `coarse-short-spin.json`.

## Game-thread wait diagnostics

The diagnostic title now wraps imported single/multiple kernel-object waits,
thread delay and critical-section entry. Each forwards the unchanged PPC
context and guest memory to its original implementation exactly once. With
`WOS_FRAME_COST_DIAGNOSTICS=ON`, it accumulates wall intervals and call counts
in thread-local storage; the thread calling VdSwap reports its own counters.
The first swap discards startup waits. Other threads' waits are not included.
These measurements can distinguish engine waiting from useful CPU work but
cannot identify the owner or purpose of a waited object by themselves.

`tools/test-guest-wait-wrappers.py` tests the actual source with mocked HLE
implementations under ASan/UBSan, with diagnostics enabled and disabled. It
checks forwarding, context changes, units, report reset and startup discard.
`tools/check-guest-wrappers.py` verifies all seven linked import-table entries
point at the corresponding wrappers. Neither test substitutes for console
timing measurements. No waits or game timing behavior are removed.

Runtime 034's street-facing capture measured 16.58–16.65 guest swaps/second
in its final windows. The swapping thread spent about 36.7–36.9 ms/frame in
KeDelayExecutionThread (188–189 calls/frame), and 11.1–11.2 ms/frame in
NtWaitForSingleObjectEx (about 8,430–8,445 calls/frame). Critical-section entry
was 0.00 ms at displayed precision. These are host wall intervals, including
overhead and any scheduling time; they do not establish the purpose of each
wait. Calling-thread CPU remained about 14.4–14.7 ms/frame.

## Zero-duration delay priority correction

Inspection found that `XThread::priority_` stores a guest priority increment,
while `Delay` compared it directly with the host `ThreadPriority::kBelowNormal`.
The POSIX host constant is +8, so a normal guest increment of 0 incorrectly
took the 100 µs sleep path for non-alertable delays that convert to zero ms.
`SetPriority` classifies increments below -0x11 as below normal; the correction
uses that same range. This retains the existing low-priority backoff, delays
that convert to at least 1 ms, and alertable delays. It does not bypass game synchronization or alter the
guest clock/vblank rate.

`tools/test-zero-delay-priority.py` extracts both actual SDK methods. Under
ASan/UBSan, 720 cases covered priority boundaries, zero/sub-ms/nonzero delays,
duration scaling, alertable completion/APCs and termination checks, using
recorded mock sleep/yield calls. These checks establish the branch behavior;
the runtime 034 totals alone do not prove all delay calls used the faulty
branch.

Runtime 035 **regressed** to roughly 7.2–7.9 guest swaps/second in the captured
tail, matching the user's report of much slower gameplay. NtSingle polling
rose to about 111,600–123,500 calls/frame, taking 66–72 ms/frame. Delay calls
rose to roughly 3,900–4,300/frame, taking 18.5–20.8 ms/frame. Calling-thread CPU
rose to about 86–87 ms/frame. This shows that eliminating backoff can increase
polling and CPU load enough to hurt throughput. The original backoff has been
restored in the runtime source and the experiment removed from the active
dependency list. Its host tests establish branch behavior, not a useful PS5
scheduling policy. Sanitized rows are in `coarse-zero-delay-regression.json`.

## High-frame-rate timing constraints

Disabling the existing `vsync` option already made menus run too fast without
fixing gameplay performance. The runtime's guest vblank worker changes from
the configured refresh interval to a 1 kHz interval with that option disabled.
This is not a safe game-specific high-frame-rate implementation.

The upstream Windows frame-pacer thread schedules an intermediate presenter
refresh. Its code does not advance another game simulation frame for that
refresh. Porting that mechanism alone would not establish more than 60 actual
game updates per second. The game-specific frame limiter and timing-dependent
logic still need to be identified and tested after renderer throughput allows
higher rates. No game-speed or refresh-clock multiplier has been applied here.
