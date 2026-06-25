# Diagrams

Rendered (Mermaid) companions to the ASCII diagrams in
[ARCHITECTURE.md](../ARCHITECTURE.md). ARCHITECTURE.md is authoritative on any
conflict; these diagrams are a visual aid, not a second source of decisions.

## Architecture / data-flow diagram

```mermaid
flowchart TD
    RD["Radar Device (simulated)"] --> RP["RadarProducer\n(implements ISignalSource)"]
    RP --> IQ["Input ThreadSafeQueue&lt;RadarSignal&gt;\npolicy: Block"]
    IQ --> TP["ThreadPool\n(owns N DSP worker threads)"]
    TP --> SP["SignalProcessor\nNoiseFilter -> FFT -> TargetDetection"]
    SP --> OQ["Output ThreadSafeQueue&lt;LogRecord&gt;\npolicy: DropOldest"]
    OQ --> LG["Logger thread\n(formats records)"]
    LG --> CS["ConsoleSink"]
    LG --> FS["FileSink"]

    WD["Watchdog"] -. reads .-> HR["HeartbeatRegistry"]
    RP -. registers .-> HR
    TP -. registers .-> HR
    LG -. registers .-> HR
    RTM["RealtimeManager"] -. configures .-> RP
    RTM -. configures .-> TP
    RTM -. configures .-> LG
    RTM -. configures .-> WD
    SH["SignalHandler"] -. requests shutdown .-> ST["ShutdownToken"]
    MET["Metrics"] -. observed by .-> WD
```

## Thread interaction diagram

```mermaid
flowchart TD
    MAIN["Main thread\n(constructs Application, blocks on SignalHandler)"]
    MAIN -->|creates, owns, starts| PROD["Producer thread"]
    MAIN -->|creates, owns, starts| DSP["DSP Worker threads (N)"]
    MAIN -->|creates, owns, starts| LOG["Logger thread"]
    MAIN -->|creates, owns, starts| WD["Watchdog thread"]

    PROD -->|push RadarSignal| IQ[("Input Queue")]
    IQ -->|waitAndPop| DSP
    DSP -->|push LogRecord| OQ[("Output Queue")]
    OQ -->|waitAndPop| LOG
    WD -.->|reads heartbeats, no direct coupling| PROD
    WD -.-> DSP
    WD -.-> LOG
```

Every thread is wrapped in a `ManagedThread` (RAII join on destruction).
Workers never synchronize directly with each other — all communication is
through the two queues, and the Watchdog only *reads* shared heartbeat state.

## Queue flow diagram

```mermaid
flowchart LR
    subgraph InputSide["Input side (Block policy)"]
        direction TB
        P["RadarProducer"] -->|push - blocks producer on full| IQ[("ThreadSafeQueue&lt;RadarSignal&gt;")]
        IQ -->|waitAndPop| W["DSP Workers"]
    end
    subgraph OutputSide["Output side (DropOldest policy)"]
        direction TB
        W2["DSP Workers"] -->|push - never blocks, drops oldest on full| OQ[("ThreadSafeQueue&lt;LogRecord&gt;")]
        OQ -->|waitAndPop| L["Logger"]
    end
```

The Input Queue applies backpressure (D5): a full queue blocks the producer
rather than losing a sampled signal. The Output / Log Queue is lossy by
design: a slow sink drops the oldest buffered record instead of propagating
backpressure into the `SCHED_FIFO` DSP workers.

## Startup sequence

```mermaid
sequenceDiagram
    participant Main
    participant App as Application
    participant RTM as RealtimeManager
    participant Reg as HeartbeatRegistry
    participant Threads as Producer/Pool/Logger/Watchdog

    Main->>App: construct(Config)
    App->>RTM: configured (no syscalls yet)
    App->>Reg: register heartbeats (static, pre-thread-start)
    Main->>App: run()
    App->>RTM: lockProcessMemory() [mlockall + prefault]
    Note over App,RTM: best-effort#59; logs a warning and continues on failure
    App->>Threads: start() (Logger, Watchdog, Pool, Producer)
    Threads->>RTM: configureCurrentThread() [SCHED_FIFO, priority, affinity]
    Note over Threads,RTM: per-thread#59; degrades gracefully without CAP_SYS_NICE
    App->>App: signalHandler.waitForShutdown() (blocks main thread)
```

## Shutdown sequence (ordered drain)

```mermaid
sequenceDiagram
    participant Sig as SIGINT/SIGTERM
    participant Tok as ShutdownToken
    participant WD as Watchdog
    participant Prod as Producer
    participant IQ as Input Queue
    participant Pool as ThreadPool (DSP)
    participant OQ as Output Queue
    participant Log as Logger

    Sig->>Tok: requestShutdown()
    Tok->>WD: wakes token wait, stops scanning first
    Tok->>Prod: stops sampling
    Note over IQ: close() wakes a producer blocked in push()#59;<br/>DSP keeps draining buffered signals
    IQ->>IQ: close()
    Prod->>Prod: joined
    Pool->>Pool: finish in-flight work, exit, joined
    Note over OQ: closed only after DSP workers are joined -<br/>no further pushes are possible
    OQ->>OQ: close()
    Log->>Log: drain remaining records, flush sinks, exit, joined
    WD->>WD: joined
```

No in-flight signal or pending log record is lost: every consumer is joined
before the queue it feeds is closed (see ARCHITECTURE.md, "Lifetime").

## Realtime and priority-inversion notes

* **Two-stage configuration (D2).** `mlockall(MCL_CURRENT | MCL_FUTURE)` and
  prefaulting run once in `main`/`Application::run()`, before any thread is
  created. `SCHED_FIFO` and per-role priority are applied per-thread, because
  `pthread_setschedparam` needs the target thread to already exist.
* **Priority inversion** is addressed by `PiMutex`
  (`PTHREAD_PRIO_INHERIT`), used as the queue's internal lock. The Producer
  (priority 80 by default), DSP workers (70), Watchdog (60) and Logger (40)
  share the two queues at different priorities, so without priority
  inheritance a lower-priority holder of the queue mutex could block a
  higher-priority waiter indefinitely.
* **Graceful degradation.** Every realtime syscall is validated. Missing
  `CAP_SYS_NICE` / a low `RLIMIT_RTPRIO` causes `configureCurrentThread()` to
  return a failure result that the caller logs as a warning; the thread keeps
  running at the default scheduling policy rather than the process aborting.
* **A practical RLIMIT_MEMLOCK caveat.** `mlockall(MCL_FUTURE)` locks every
  page the process maps *afterwards*, including each subsequently created
  thread's stack. On a host with the common 64 MB default `RLIMIT_MEMLOCK`
  (`ulimit -l`), seven `SCHED_FIFO` threads at the default ~8 MB stack size
  can exceed that limit, and `pthread_create` then fails with `EAGAIN`. This
  is not a logic bug in `RealtimeManager` — `mlockall` itself succeeds; the
  budget is exhausted by the threads started afterwards. See "Execution" in
  [README.md](../README.md) for how to raise the limit or run with realtime
  configuration disabled.
