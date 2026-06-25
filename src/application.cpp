#include "radar/application.hpp"

#include "radar/console_sink.hpp"
#include "radar/exceptions.hpp"
#include "radar/file_sink.hpp"
#include "radar/random_generator.hpp"
#include "radar/steady_clock.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace radar {

Application::Application(Config config)
    : config_(std::move(config)), clock_(std::make_unique<SteadyClock>()),
      realtimeManager_(config_), inputQueue_(config_.queues().inputCapacity, OverflowPolicy::Block),
      outputQueue_(config_.queues().outputCapacity, OverflowPolicy::DropOldest),
      // Heartbeats are registered into the registry here, before any thread starts
      // (static registration, D3). The producer and logger own a single
      // heartbeat each; the ThreadPool registers one per DSP worker from inside its
      // own constructor. Registration must happen before the borrowing subsystem is
      // constructed, which the member declaration order guarantees.
      producer_(config_, *clock_, shutdownToken_, inputQueue_,
                heartbeatRegistry_.registerHeartbeat("producer"), realtimeManager_,
                RandomGenerator{}, metrics_),
      pool_(config_, *clock_, shutdownToken_, inputQueue_, outputQueue_, realtimeManager_,
            heartbeatRegistry_, metrics_),
      logger_(config_, *clock_, shutdownToken_, outputQueue_, realtimeManager_,
              heartbeatRegistry_.registerHeartbeat("logger"), buildSinks(config_), metrics_),
      watchdog_(config_, *clock_, shutdownToken_, heartbeatRegistry_, outputQueue_,
                realtimeManager_, metrics_),
      // Installed last among the members but still during construction, before any
      // thread is started in run(): it blocks SIGINT / SIGTERM on the main thread so
      // every worker thread inherits the mask.
      signalHandler_(config_, shutdownToken_) {}

int Application::run() {
    // Process-wide realtime stage (D2): mlockall + prefault, applied once before any
    // thread is created. Best-effort — missing privileges degrade gracefully with a
    // warning instead of aborting. The Logger thread is not running yet, so this
    // single pre-logging diagnostic goes straight to stderr.
    const RealtimeResult memoryLock = realtimeManager_.lockProcessMemory();
    if (!memoryLock.ok()) {
        std::cerr << "[radar] realtime: " << memoryLock.failedCall << " failed (errno "
                  << memoryLock.errorNumber << "); continuing without locked memory\n";
    }

    try {
        startThreads();

        // The main thread blocks here until shutdown is requested. A SIGINT /
        // SIGTERM wakes the signalfd and sets the token; a token set by another
        // subsystem (watchdog escalation or a worker's exception boundary, D7) ends
        // the bounded wait on its next re-check.
        try {
            signalHandler_.waitForShutdown();
        } catch (const RadarException& ex) {
            // An expected, unrecoverable wait error is itself treated as a shutdown
            // trigger rather than an abort: request shutdown and fall through to the
            // ordered drain below, returning success.
            std::cerr << "[radar] signal wait failed: " << ex.what() << "; shutting down\n";
            shutdownToken_.requestShutdown();
        }
    } catch (...) {
        // Reached by a startup failure, or by any unexpected error after threads
        // started. Run the very same ordered teardown so no thread that did start is
        // joined while still running unsignalled, then propagate so main maps it
        // to a non-zero exit code.
        shutdown();
        throw;
    }

    shutdown();
    reportSummary();
    return EXIT_SUCCESS;
}

void Application::startThreads() {
    // Start the consumers before the producer so the pipeline is already draining
    // before the first signal is generated. Each thread applies its per-thread
    // realtime stage on entry (D2). If any start() throws, the caller runs the
    // ordered teardown, which safely joins whatever did start.
    logger_.start();
    watchdog_.start();
    pool_.start();
    producer_.start();
}

void Application::shutdown() noexcept {
    // Idempotent: run() calls this once on the normal path and once on the rollback
    // path; the guard makes a second call a no-op.
    if (stopped_) {
        return;
    }
    stopped_ = true;

    // Ordered drain (ARCHITECTURE.md "Shutdown (ordered drain)"). Each consumer
    // is joined before the queue it feeds is closed, so no buffered item is lost and
    // no subsystem outlives a resource it depends on. Joining a thread that never
    // started is a no-op (ManagedThread::join), which keeps the rollback path safe.
    shutdownToken_.requestShutdown();  // watchdog stops scanning first; producer stops sampling
    inputQueue_.close();  // wake a producer blocked in push(); DSP drains buffered signals
    producer_.join();
    pool_.join();  // DSP workers finish in-flight work and exit — no further output pushes
    outputQueue_.close();  // Logger drains remaining records, flushes sinks, exits
    logger_.join();
    watchdog_.join();
}

void Application::reportSummary() const {
    // Final shutdown summary. The Logger thread has already exited, so this goes
    // straight to stdout; it is off the realtime path. Reads use Metrics' acquire
    // loads.
    std::cout << "[radar] shutdown summary:\n"
              << "  signals produced  : " << metrics_.signalsProduced() << '\n'
              << "  signals processed : " << metrics_.signalsProcessed() << '\n'
              << "  input drops       : " << metrics_.inputDrops() << '\n'
              << "  log-record drops  : " << metrics_.logDrops() << '\n'
              << "  deadline misses   : " << metrics_.deadlineMisses() << '\n'
              << "  watchdog stalls   : " << metrics_.watchdogStalls() << '\n'
              << "  thread exceptions : " << metrics_.threadExceptions() << '\n';
}

std::vector<std::unique_ptr<ILogSink>> Application::buildSinks(const Config& config) {
    std::vector<std::unique_ptr<ILogSink>> sinks;
    if (config.logging().console) {
        sinks.push_back(std::make_unique<ConsoleSink>());
    }
    if (!config.logging().filePath.empty()) {
        sinks.push_back(std::make_unique<FileSink>(config.logging().filePath));
    }
    return sinks;
}

}  // namespace radar
