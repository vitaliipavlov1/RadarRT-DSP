# Interview discussion

This is a discussion-format companion to [ARCHITECTURE.md](../ARCHITECTURE.md),
written the way these design choices tend to come up in an interview: as
"why did you do X instead of Y" questions. ARCHITECTURE.md (D1–D8) remains
the authoritative record of the decisions; this document explains the
reasoning conversationally and lists alternatives that were considered and
rejected.

## "Why does the ThreadPool pull straight from the input queue instead of having its own task queue?" (D1)

A generic executor (`std::function` task queue serviced by worker threads)
plus the existing input queue would put two queues in series on the hot
path: the producer would push a `RadarSignal`, and then something would
have to wrap and push a *task* into a second queue for a worker to pop. That
doubles the locking and copying for a pipeline that only ever has one kind
of work item. Since `ThreadPool` here exists for exactly one purpose — run
`SignalProcessor` over whatever the input queue yields — making it a
specialized consumer pool that owns its worker threads and reads the
existing queue directly is simpler and faster, and it's also the more
realistic shape for a fixed radar pipeline. A generic executor would earn
its complexity back if the pool had to run heterogeneous tasks; it doesn't
here.

## "Why two shutdown mechanisms (ShutdownToken and queue close()) instead of one?" (D4)

Because threads block in two different places that a single mechanism can't
both reach efficiently. A worker waiting on `waitAndPop()` is woken by
`close()`. A thread that's not waiting on any queue — the producer's
periodic sleep between samples, the watchdog's scan interval, the main
thread's signal wait — has nothing to close. `ShutdownToken` is a
condition-variable-backed flag those waits check. Trying to fold both into
one mechanism would mean giving every non-queue wait a queue to block on
just so it could be "closed," which is a worse abstraction than having two
small, single-purpose ones. A thread that can block in both places (e.g. the
producer, which sleeps *and* may block in `push()`) simply observes both.

## "Why is the logging queue lossy (DropOldest) while the input queue blocks?" (D5)

Because the two queues protect different things. The input queue represents
sampled radar data — losing a sample is a real loss, so it backpressures the
(simulated) radar source instead of dropping silently. The log queue carries
diagnostic output; if the logger (or its file sink) is briefly slow, the
correct behavior is to drop old, already-stale log records, not to block a
`SCHED_FIFO` DSP worker on disk I/O. A `Block` policy on the log path would
let slow I/O propagate backpressure straight into the realtime data path,
which defeats the purpose of running the DSP workers under a realtime
scheduling policy at all. Every drop, on either queue, is still counted in
`Metrics` — lossy doesn't mean invisible.

## "Why does the Watchdog need heartbeat *state*, not just a timestamp?" (D3)

A bare "last update" timestamp produces false positives the moment the
system is idle: a DSP worker legitimately blocks on an empty input queue
between signals, and its timestamp goes stale even though nothing is wrong.
Tagging the heartbeat with `Running` vs `WaitingForInput` lets the worker
say "I am intentionally idle" before it blocks, so the Watchdog only flags a
heartbeat that's stale *while claiming to be running*. The alternative —
periodically waking idle workers just to refresh a timestamp — would be
busy-waiting in disguise, which the project explicitly avoids.

## "Why can't the Watchdog actually kill a stuck thread?"

Because there's no safe way to force-terminate a `std::thread` in standard
C++ (no equivalent of `pthread_cancel` that's exception- and RAII-safe), and
even POSIX thread cancellation is notoriously unsafe with held locks and
unwound destructors. The Watchdog's job is to make a stall *observable* —
report it through the Logger and `Metrics` — and, if configured, request a
graceful shutdown of the whole process via `ShutdownToken`. A genuinely
hung thread inside `Running` state is an accepted limitation, not a gap:
production systems usually solve "a thread is truly wedged" at a layer above
the process (a supervisor that restarts the process), not by adding unsafe
cancellation machinery inside it.

## "Why PiMutex instead of a plain std::mutex for the queues?"

Because the queue is shared by threads running at different realtime
priorities (producer, DSP workers, watchdog, logger each have a distinct
priority by default). Without priority inheritance, a low-priority thread
holding the queue's mutex can block a higher-priority thread that's waiting
for it — classic priority inversion, the failure mode the Mars Pathfinder
incident made famous. `PTHREAD_PRIO_INHERIT` temporarily boosts the lock
holder's priority to that of the highest-priority waiter, bounding the delay.
If a deployment ran every pipeline thread at one priority, a plain mutex
would be sufficient — `PiMutex` is the safe default for the multi-priority
configuration this project actually uses.

## "Why does ThreadSafeQueue use condition_variable_any instead of condition_variable?"

`std::condition_variable` only works with `std::unique_lock<std::mutex>` —
it's specified in terms of the standard mutex type. Since the queue's lock
is `PiMutex` (a custom RAII wrapper, not `std::mutex`), it needs the more
general `std::condition_variable_any`, which works with any lockable type.
That generality has a real cost (typically a heavier wait path than
`condition_variable`), which is accepted here because the priority-inversion
guarantee matters more than shaving wait overhead on this queue.

## "Why is time injected through a Clock interface instead of calling clock_gettime directly?" (D6)

Because almost everything time-dependent in this system — the producer's
sampling period, heartbeat age, the watchdog's timeout check — needs to be
testable without real sleeps. A test that has to sleep for hundreds of
milliseconds to exercise a timeout is slow and flaky. Injecting a `Clock`
interface lets tests use `ManualClock` to advance time deterministically and
instantly, while production code uses `SteadyClock` over
`CLOCK_MONOTONIC` (monotonic, so it's immune to wall-clock adjustments).
This is the *only* clock indirection in the system — it isn't generalized
further, because nothing else needs it.

## "Why does every thread loop have its own try/catch instead of one handler at the top?" (D7)

Because `std::thread` has no built-in exception propagation: an exception
that escapes a thread's entry function calls `std::terminate` and takes the
whole process down, regardless of what the main thread is doing. There's no
single place above the thread function to catch it. So each thread's run
loop wraps itself in a top-level boundary that logs the exception, records
it in `Metrics`, and requests shutdown through the same `ShutdownToken` a
clean shutdown would use — one bad signal sample or one logging failure
degrades to a graceful shutdown instead of crashing the process outright.

## "If you had more time, what would you add next?"

The CI quality gates (every preset built and tested, the unit suite run under
ASan/UBSan and TSan, and clang-format/clang-tidy enforcement) are in place. The
most interesting extensions from here would be a
config file/CLI loader (`Config` is already structured to support one — see
`app/main.cpp`), and a second `ISignalSource` implementation backed by real
hardware or a recorded capture file, to demonstrate that the interface
abstraction actually pays for itself.
