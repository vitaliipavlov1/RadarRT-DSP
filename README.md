# RadarRT-DSP — Embedded Real-Time Radar Processing System

**Embedded Linux C++17** real-time radar processing pipeline: 
a radar source produces signals, a pool of DSP
workers processes them through a staged pipeline, results are logged asynchronously,
and a watchdog supervises liveness — all under Linux realtime scheduling with graceful
shutdown.

> **Status:** All subsystems (producer, DSP thread pool, logger, watchdog, realtime
> support, signal handling, application assembly) are implemented and unit tested;
> documentation is complete. CI builds and tests every preset, runs the unit suite under
> AddressSanitizer/UBSan and ThreadSanitizer, and enforces clang-format and clang-tidy
> (see [Continuous integration](#continuous-integration)).

## Pipeline overview

```
RadarProducer -> Input Queue (Block) -> ThreadPool / SignalProcessor
              -> Output Queue (DropOldest) -> Logger -> Console / File
```

Cross-cutting: `Watchdog` (per-thread heartbeats), `RealtimeManager`
(SCHED_FIFO, mlockall, priority inheritance), `SignalHandler` (signalfd),
`Metrics`, injected `Clock`.

## Documentation

| Document | Purpose |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Authoritative architecture and decisions (D1–D8) |
| [docs/diagrams.md](docs/diagrams.md) | Rendered architecture / thread / queue / sequence diagrams |
| [docs/interview.md](docs/interview.md) | Interview-style discussion of the design decisions |

## Requirements

- Linux
- C++17 compiler (GCC or Clang)
- CMake ≥ 3.21
- POSIX threads

## Build

The project uses CMake presets:

```sh
cmake --preset debug        # configure
cmake --build --preset debug
```

Available configure presets:

| Preset | Description |
| --- | --- |
| `debug` | Debug build |
| `release` | Optimized build |
| `asan-ubsan` | Debug + Address/UB sanitizers |
| `tsan` | Debug + ThreadSanitizer |

Build other presets the same way, e.g. `cmake --preset asan-ubsan && cmake --build
--preset asan-ubsan`.

## Execution

The built binary is `radarrt-dsp` (target `radar_app`), under `app/` inside the preset's
build directory, e.g. `build/debug/app/radarrt-dsp`:

```sh
./build/debug/app/radarrt-dsp
```

There is no CLI/config-file loader yet (`app/main.cpp` runs the default `Config`); see
[docs/interview.md](docs/interview.md) for that as a possible extension. With the
default configuration the application:

* simulates a radar source at 1 kHz, processed by 4 DSP worker threads;
* logs DSP results and diagnostics to the console and to `radar.log` in the current
  directory;
* runs SCHED_FIFO realtime threads (producer/DSP/watchdog/logger priorities 80/70/60/40)
  and locks process memory (`mlockall`), unless privileges are missing — see below.

Stop it with `Ctrl+C` (SIGINT) or `SIGTERM`; the ordered drain (see
[docs/diagrams.md](docs/diagrams.md#shutdown-sequence-ordered-drain)) runs before the
process exits, printing a shutdown summary (signals produced/processed, drops,
deadline misses, watchdog stalls, thread exceptions) on stdout.

> **RLIMIT_MEMLOCK note.** `realtime.lockMemory` (default: on) calls
> `mlockall(MCL_CURRENT | MCL_FUTURE)`, which locks every page mapped afterwards —
> including each subsequently created thread's stack. With the common 64 MB default
> `RLIMIT_MEMLOCK` (check with `ulimit -l`), the seven realtime threads this application
> starts can exceed that budget, and thread creation then fails with `EAGAIN` ("Resource
> temporarily unavailable"), which `main` reports and exits non-zero. Either raise the
> limit (`ulimit -l unlimited`, or a systemd `LimitMEMLOCK=infinity` / `/etc/security/
> limits.conf` entry, as is typical for realtime deployments) or run with
> `Config::Params::realtime.lockMemory = false` (and typically `enabled = false`, since
> `SCHED_FIFO` usually requires the same elevated privileges) if the host does not allow
> raising it. This is inherent to `mlockall(MCL_FUTURE)` plus several realtime thread
> stacks, not a `RealtimeManager` defect — see "Realtime and priority-inversion notes" in
> [docs/diagrams.md](docs/diagrams.md).

Sanitizer builds run the same way as a normal build — they need no special invocation
beyond building the corresponding preset:

```sh
cmake --build --preset asan-ubsan && ./build/asan-ubsan/app/radarrt-dsp
cmake --build --preset tsan       && ./build/tsan/app/radarrt-dsp
```

## Tests

Unit tests cover the reusable infrastructure (`ThreadSafeQueue`, `ShutdownToken`,
`ManagedThread`, `Heartbeat`/`HeartbeatRegistry`, the `Watchdog` flagging policy, and
the queue's ordered-drain synchronization). Time-dependent logic is driven by the
injected `ManualClock`, so the tests are deterministic and use no real sleeps.

They use a **minimal in-repo assertion harness** (`tests/framework`), not an external
library: external test libraries are disallowed by default, and FetchContent would add
a network dependency to every clean build. The harness is a small static test registry
with `RADAR_TEST` / `RADAR_EXPECT` / `RADAR_ASSERT` macros.

Run them via CTest (one entry per suite):

```sh
ctest --preset debug          # also: --preset asan-ubsan, --preset tsan
```

Or run the test binary directly, optionally filtered to one suite:

```sh
./build/debug/tests/radar_tests              # all suites
./build/debug/tests/radar_tests watchdog     # one suite
```

> ThreadSanitizer needs ASLR entropy compatible with its shadow-memory mapping. On
> hosts whose `vm.mmap_rnd_bits` is too high (some newer kernels) the TSan binary
> aborts with "unexpected memory mapping"; run it under `setarch -R` to disable ASLR.

## Continuous integration

[GitHub Actions](.github/workflows/ci.yml) runs the quality gates on every push and
pull request:

| Gate | What it does |
| --- | --- |
| Build & test | Configures, builds and runs CTest for the `debug` and `release` presets |
| Sanitizers | Builds the `asan-ubsan` and `tsan` presets and runs the unit suite under each (any sanitizer report fails the job; the TSan job lowers `vm.mmap_rnd_bits` for the shadow-memory mapping) |
| clang-format | `clang-format --dry-run --Werror` over all headers and sources against [.clang-format](.clang-format) |
| clang-tidy | `clang-tidy` over every translation unit against [.clang-tidy](.clang-tidy) (warnings are errors; `tests/` carries [scoped overrides](tests/.clang-tidy)) |

The gates use the same tool versions (clang-format/clang-tidy 18) and invocations as
the local workflow above, so a clean local run matches CI.

## Repository layout

```
include/radar/   public headers
src/             library sources
app/             application entry point
tests/           unit tests
cmake/           build modules (warnings, sanitizers, toolchains)
docs/            diagrams and design notes
.github/         continuous integration
```

## License

[MIT](LICENSE).
