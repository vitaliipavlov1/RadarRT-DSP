# ARCHITECTURE.md

# Embedded Real-Time Radar Processing System

## Software Architecture

---

# Purpose

This document describes the software architecture of the repository.

Its purpose is to explain:

* subsystem responsibilities;
* thread interactions;
* ownership model;
* lifetime management;
* synchronization strategy;
* realtime design decisions.

This document is the architectural reference for the entire repository.

When this document conflicts with intuition or with shorter summaries elsewhere, this document wins.

---

# Architectural Decisions

This section records the binding decisions that resolve the design forks in the system.

Implementation must follow these decisions. They exist to prevent ambiguity from leaking into code.

## D1 — ThreadPool is a consumer pool, not a generic executor

The ThreadPool does not own a separate task queue of `std::function`.

The ThreadPool owns the DSP worker threads.

Each worker pulls a `RadarSignal` directly from the Input Queue, runs the `SignalProcessor`, and pushes the result into the Output Queue.

There is exactly one queue on each side of the pool. No second internal queue exists.

Rationale: a generic executor plus the Input Queue would create two queues in series on the hot path, doubling locking and copies for no benefit. A consumer pool is the realistic radar-pipeline design.

## D2 — Realtime configuration has two distinct stages

Process-wide configuration (`mlockall`, memory prefaulting) is applied early in `main`, before any thread is created.

Per-thread configuration (`SCHED_FIFO`, scheduling priority, optional CPU affinity) is applied from inside each thread at start-up, because `pthread_setschedparam` operates on a specific thread that must already exist.

`RealtimeManager` exposes both stages explicitly. It is never a single "configure everything" call.

## D3 — Heartbeats are per-thread, state-aware, and owned by the registry

Each managed worker has one `Heartbeat`.

A `Heartbeat` carries an atomic monotonic timestamp and an atomic liveness state: `Running` or `WaitingForInput`.

A worker blocked on an empty queue is legitimately alive. Before blocking it sets `WaitingForInput`; on wake it sets `Running` and refreshes its timestamp.

The Watchdog only flags a heartbeat whose state is `Running` and whose age exceeds the timeout. A heartbeat in `WaitingForInput` is never flagged.

Ownership: the `HeartbeatRegistry` owns the heartbeats for the whole application lifetime. Workers borrow their heartbeat by reference; they never own it. Registration is static — all heartbeats are registered before any thread starts, and none is removed until after the Watchdog has stopped. This removes any dangling-read hazard and keeps registry reads genuinely lock-free.

Rationale: a naive "last update time" heartbeat produces false stalls whenever the system is idle and workers block on empty queues; registry-owned heartbeats decouple heartbeat lifetime from worker lifetime.

## D4 — Two cooperating shutdown mechanisms with separate roles

`ThreadSafeQueue::close()` wakes threads blocked inside the queue, on both `waitAndPop` and `push`. It is the only shutdown signal for queue waits.

`ShutdownToken` wakes threads blocked outside any queue: the producer's periodic sleep, the watchdog's periodic sleep, the signal-handler wait.

A thread that can block in both places observes both. Neither mechanism replaces the other.

## D5 — Per-queue overflow policy chosen to protect the data path

Every queue is bounded and configured with an overflow policy: `Block`, `DropNewest`, or `DropOldest`.

* The Input Queue uses `Block` (backpressure) so no sampled signal is silently lost.
* The Output / Log Queue uses `DropOldest`, so a slow sink never blocks a DSP worker.

Rationale: a `Block` policy on the logging path would let slow disk I/O propagate backpressure into the SCHED_FIFO DSP workers and stall the pipeline, violating "logging must never block DSP". Making the logging path lossy (best-effort) keeps the real-time data path free. Dropped items are always counted in `Metrics`, never discarded silently.

## D6 — Time is injected through a Clock abstraction

The whole system is time-driven (producer period, heartbeat age, watchdog timeout). A minimal `Clock` interface is injected into every time-dependent subsystem.

`SteadyClock` (monotonic, `CLOCK_MONOTONIC`) is the production implementation. A `ManualClock` lets tests advance time deterministically, so time-dependent logic is tested without real sleeps.

This is the only clock abstraction in the system; no further time indirection is introduced.

## D7 — Every thread has a top-level exception boundary

Each thread's run function wraps its loop in a top-level `try/catch`.

An escaping exception is logged, recorded in `Metrics`, and requests graceful shutdown. It never reaches `std::terminate`.

Rationale: a single uncaught exception on a worker would otherwise terminate the whole process.

## D8 — Logging is best-effort and formatting never runs on the RT path

The record crossing the Output / Log Queue is a move-only `LogRecord` of structured fields. DSP workers populate it without building any formatted string, so no heap allocation happens on the SCHED_FIFO path.

Text formatting happens on the Logger thread, inside the sink path. Low-rate diagnostic logging from non-realtime subsystems may carry a message string, because it is off the hot path.

Combined with D5, this guarantees the DSP workers neither block nor allocate for the sake of logging.

---

# High-Level Architecture

```
                Radar Device (simulated)
                        |
                        v
        RadarProducer  (implements ISignalSource)
                        |
                        v
        Input ThreadSafeQueue<RadarSignal>      (policy: Block)
                        |
                        v
        ThreadPool  (owns N DSP worker threads)
                        |
                        v
        SignalProcessor  (composes pipeline stages)
                        |
                        v
        Output ThreadSafeQueue<LogRecord>       (policy: DropOldest)
                        |
                        v
        Logger thread   (formats records)
                        |
                        v
        ILogSink list  ->  ConsoleSink / FileSink
```

Cross-cutting subsystems (not in the data path):

```
        Watchdog        -> reads HeartbeatRegistry, applies a timeout policy
        RealtimeManager -> process-wide + per-thread realtime configuration
        SignalHandler   -> turns SIGINT / SIGTERM into a ShutdownToken request
        Metrics         -> throughput, drops, queue depth, latency, deadline misses
        Clock           -> injected monotonic time source
```

---

# Component Overview

The repository consists of the following major subsystems.

Infrastructure

* Config
* Clock (interface) — SteadyClock, ManualClock (test)
* RandomGenerator
* ShutdownToken
* Exceptions

Threading

* ManagedThread

Realtime

* RealtimeManager
* PiMutex

Core

* ThreadSafeQueue

Data

* RadarSignal
* ProcessedSignal
* LogRecord

Sources

* ISignalSource
* RadarProducer

Processing

* SignalProcessor
* pipeline stages (NoiseFilter, FFT, TargetDetection) — plain classes composed inside SignalProcessor, not a class hierarchy

Execution

* ThreadPool

Logging

* Logger
* ILogSink
* ConsoleSink
* FileSink

Monitoring

* Heartbeat
* HeartbeatRegistry
* Watchdog
* Metrics

Lifecycle

* SignalHandler

Application

* Application
* main()

---

# Thread Model

```
            Main Thread
                |
                |  creates, owns, starts
                v
   +------------+-----------------------------+
   |            |              |              |
   v            v              v              v
Producer    DSP Worker     Logger        Watchdog
Thread      Threads (N)    Thread        Thread
```

Every thread is wrapped in a `ManagedThread` and is joined on destruction.

Every thread's run function has a top-level exception boundary per D7.

Detached threads are prohibited.

The signal-handler runs on the main thread (it blocks the relevant signals and waits via `signalfd`).

---

# Threading Primitives

`ManagedThread` is an RAII wrapper around `std::thread`.

It joins in its destructor if still joinable. It carries no stop logic and no stop callback.

Stopping a thread before it is joined is the responsibility of `Application`, not of `ManagedThread`. `Application` guarantees that the `ShutdownToken` is set and the relevant queues are closed before any `ManagedThread` is destroyed — on the normal shutdown path and on the startup-failure rollback path alike (see Lifetime). The destructor join is therefore a safety net, not the primary ordering mechanism.

C++17 has no `std::jthread`; `ManagedThread` deliberately stays minimal rather than re-implementing `std::stop_token`.

---

# Queue Model

`ThreadSafeQueue<T>` is a template, used as `ThreadSafeQueue<RadarSignal>` and `ThreadSafeQueue<LogRecord>`.

```
Input Queue                         Output Queue

RadarProducer                       DSP Workers
     |                                   |
     v                                   v
ThreadSafeQueue<RadarSignal>        ThreadSafeQueue<LogRecord>
   (Block)                            (DropOldest)
     |                                   |
     v                                   v
DSP Workers                         Logger
```

Operations:

* push()
* tryPush()
* waitAndPop()  — returns std::optional<T>
* tryPop()      — returns std::optional<T>
* close()
* waitUntilEmpty()

Close semantics (D3/D4):

* close() is idempotent and wakes all waiters (both `waitAndPop` and `push`).
* after close(), push()/tryPush() enqueue nothing and return false; a producer blocked in push() is woken and observes this.
* after close(), waitAndPop()/tryPop() keep returning buffered items until the queue is empty, then return an empty optional (end-of-stream). Closing a non-empty queue never discards buffered items.

Properties:

* bounded capacity, configurable;
* overflow policy per D5;
* two condition variables (not-empty and not-full) to avoid waking the wrong waiters;
* no busy waiting;
* values move in and out; no unnecessary copies.

Synchronization is centralized inside the queue.

Workers never synchronize directly with each other.

The queue's internal mutex is a `PiMutex` (priority-inheritance) because producer, DSP workers and logger may run at different realtime priorities while sharing it (see Realtime Strategy). Because the lock type is custom, the queue uses `std::condition_variable_any`.

---

# Ownership Model

Ownership is always explicit.

`Application` owns:

* Config;
* Clock;
* Metrics;
* both queues;
* HeartbeatRegistry;
* RealtimeManager;
* RadarProducer;
* ThreadPool;
* Logger;
* Watchdog;
* SignalHandler.

Subsystems receive their dependencies by reference or by `unique_ptr` through constructors (dependency injection).

The `HeartbeatRegistry` owns all heartbeats for the application lifetime; workers borrow them by reference (D3).

Temporary data uses move semantics.

Shared ownership (`shared_ptr`) is used only where a resource genuinely has multiple owners; it is not the default.

`ManagedThread` instances are owned by the subsystem whose loop they run.

---

# Lifetime

## Startup

```
Application constructed
        |
        v
Config loaded; Clock selected (SteadyClock)
        |
        v
RealtimeManager: process-wide stage (mlockall + prefault)
        |
        v
Metrics, Queues, HeartbeatRegistry created; heartbeats registered
        |
        v
Subsystems constructed (Producer, ThreadPool, Logger, Watchdog)
        |
        v
SignalHandler installed (SIGINT / SIGTERM blocked, signalfd armed)
        |
        v
Threads started; each applies its per-thread realtime stage on entry
        |
        v
Running
```

Startup-failure rollback: if any step throws after one or more threads have started, `Application` runs the same teardown as the ordered shutdown below (set the `ShutdownToken`, close the queues, then let RAII join). No thread is ever joined while its loop is still running unsignalled.

## Shutdown (ordered drain)

Shutdown order matters: in-flight signals and pending log records must not be lost, and no subsystem may outlive a resource it depends on.

```
Shutdown requested (signal, run duration elapsed, watchdog escalation, or thread exception)
        |
        v
ShutdownToken set
   -> Watchdog wakes from its token wait and stops scanning first
      (the intentional drain must not be mistaken for a stall)
   -> Producer stops sampling
        |
        v
Input Queue closed
   (wakes a producer blocked in push(); DSP workers keep draining
    buffered signals until empty, then observe end-of-stream)
        |
        v
Producer joined
        |
        v
DSP workers finish in-flight work, exit, and are joined
   (guarantees no further pushes into the Output / Log Queue)
        |
        v
Output / Log Queue closed
   (Logger drains remaining records, flushes sinks, exits)
        |
        v
Logger joined, then Watchdog joined
        |
        v
Application destroyed
```

The Logger is shut down among the last subsystems because every other subsystem may log diagnostics. The Watchdog is quiesced first (it only reads state) and joined at the end.

---

# Synchronization Strategy

Synchronization is centralized inside `ThreadSafeQueue`.

Workers communicate only through queues. Direct shared mutable state is avoided.

Two shutdown mechanisms cooperate per D4: `close()` for queue waits, `ShutdownToken` for non-queue waits.

Atomic state (heartbeats, metrics counters, the shutdown flag) uses explicit memory ordering: release on write, acquire on read. Sequential consistency is not assumed by default.

Critical sections remain minimal. No expensive work or I/O is performed while holding a lock.

No busy waiting. Blocking is provided by condition variables.

---

# Realtime Strategy

`RealtimeManager` is split into two stages per D2.

Process-wide stage (early in `main`):

* `mlockall(MCL_CURRENT | MCL_FUTURE)`;
* stack and heap prefaulting to avoid first-touch page faults under load.

Per-thread stage (on each thread's entry):

* `SCHED_FIFO` via `pthread_setschedparam`;
* per-role priority from Config;
* optional CPU affinity via `pthread_setaffinity_np`.

Priority inversion is addressed with `PiMutex`, a thin RAII wrapper over a `pthread_mutex_t` configured with `PTHREAD_PRIO_INHERIT`. The shared queue mutex is a `PiMutex` because it is contended by threads of differing realtime priority. If a deployment runs all pipeline threads at a single priority, priority inversion cannot occur among them and a plain mutex would suffice; the PI mutex is the safe default for the multi-priority configuration.

Graceful degradation: realtime privileges (`CAP_SYS_NICE`, `RLIMIT_RTPRIO`) may be unavailable. Every realtime system call is validated; on permission failure the system logs a warning and continues at default scheduling rather than aborting.

Deterministic periodic timing: the producer schedules each sample against an absolute deadline on the monotonic `Clock` — each deadline is the previous one plus the period, never `now + period` — so period error does not accumulate. The inter-sample wait is performed on the `ShutdownToken` (`waitFor`) rather than a bare `clock_nanosleep`, so it is woken the instant shutdown is requested (D4); a plain `clock_nanosleep` is deliberately not used because it is not wakeable by the token and would leave the producer's periodic sleep unstoppable, violating "sleeping threads must always be wakeable". The wait is computed from the absolute deadline each cycle, so it remains drift-free; `std::condition_variable::wait_for` itself measures the timeout against `std::chrono::steady_clock`, keeping the whole path on a monotonic base. A sample whose deadline is missed is counted in `Metrics`.

---

# Signal Handling Strategy

`SignalHandler` provides deterministic, async-signal-safe shutdown.

SIGINT and SIGTERM are blocked in all threads and consumed through `signalfd` on the main thread.

On receipt it sets the `ShutdownToken`, which triggers the ordered drain.

No work is performed inside a signal context; the handler only translates a signal into a token request.

---

# Logging Strategy

Logging is asynchronous and best-effort. DSP workers never perform file I/O, never block on logging, and never allocate or format strings for logging (D5, D8).

`LogRecord` is a move-only value of structured fields: level, monotonic timestamp, source, and a compact result payload. A short message string is allowed only for low-rate diagnostic logging from non-realtime subsystems.

The Output / Log Queue is `ThreadSafeQueue<LogRecord>` with `DropOldest` policy — the single asynchronous logging channel for the whole application.

DSP workers fill a `LogRecord`'s structured fields (no string formatting) and push it; the push never blocks. Other subsystems (e.g. the Watchdog) enqueue diagnostic records on the same channel.

The `Logger` owns the draining thread and a list of `ILogSink`. It performs all text formatting and all I/O. Concrete sinks are `ConsoleSink` and `FileSink`. Dropped records are counted in `Metrics`.

Rationale: this keeps `Logger` generic, keeps the DSP path free of I/O, blocking and allocation, and decouples `Logger` from the DSP domain type. `ProcessedSignal` remains the DSP computation result internally and is the source of the record's structured payload.

---

# Health Monitoring Strategy

Each managed worker has a `Heartbeat` owned by the `HeartbeatRegistry` per D3.

`HeartbeatRegistry` is the single point the Watchdog reads, with lock-free reads over statically registered heartbeats.

The Watchdog runs on its own thread, waits on the `ShutdownToken`, and periodically scans the registry. Because it waits on the token, a shutdown request ends its scanning immediately, before the drain begins, so the intentional drain is never mistaken for a stall.

A heartbeat is flagged only when its state is `Running` and its age exceeds the configured timeout. `WaitingForInput` is never flagged. Age is measured with the injected `Clock`.

Watchdog policy is configurable: report-only (default), or request graceful shutdown via the `ShutdownToken`. In both cases the event is reported through the Logger and recorded in `Metrics`.

A thread that is genuinely hung in `Running` state cannot be force-recovered; this is an accepted limitation. The Watchdog reports it and, if so configured, requests graceful shutdown. No abort or forced-termination machinery is introduced.

---

# Metrics Strategy

`Metrics` is a thread-safe set of atomic counters and gauges:

* signals produced / processed;
* input drops and log-record drops;
* deadline misses (producer period);
* per-queue depth (approximate);
* processing latency (sampled).

Updates use explicit memory ordering (relaxed-to-release on write); readers (Watchdog, shutdown summary) read with acquire.

Metrics make backpressure, drops, deadline misses and stalls observable rather than invisible.

---

# Thread Pool Strategy

Signal processing is parallelized across the DSP worker threads owned by `ThreadPool` per D1.

Each worker loops: set `Running` and refresh its heartbeat, `waitAndPop` from the Input Queue (state `WaitingForInput` while blocked), run `SignalProcessor`, populate a structured `LogRecord` (no formatting, no allocation), push it to the Output / Log Queue (never blocks, `DropOldest`).

`SignalProcessor` is one class that composes its pipeline stages — `NoiseFilter`, `FFT`, `TargetDetection` — by value. The stages are plain classes held by composition, not an inheritance hierarchy and not behind an interface; a new stage is added by extending the composition. This keeps the pipeline open to extension without abstract indirection.

Workers never communicate directly. The pool exits when the Input Queue is closed and drained.

---

# Error Handling

Every subsystem is exception safe.

Resources are released automatically using RAII.

No resource leaks. No ignored failures.

Every POSIX call is validated; failures either degrade gracefully (realtime) or raise a typed exception from the `Exceptions` hierarchy.

Every thread loop has a top-level exception boundary per D7: an escaping exception is logged, recorded in `Metrics`, and requests graceful shutdown rather than terminating the process.

---

# Design Principles

The repository follows:

* Single Responsibility Principle
* RAII
* Composition over inheritance
* Explicit ownership
* Dependency Injection
* Modern C++17 (std::optional, enum class, [[nodiscard]], move semantics)
* Exception Safety
* Thread Safety

Abstractions are introduced only where they earn their keep (Clock for testability, ISignalSource for source substitution, ILogSink for output substitution). No interface is added purely for symmetry.

---

# Architectural Goals

The architecture prioritizes:

* maintainability;
* readability;
* modularity;
* deterministic behaviour;
* production-quality design;
* realistic Embedded Linux and realtime practices.

Every subsystem is independently understandable, testable and reusable.
