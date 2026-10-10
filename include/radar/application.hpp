#pragma once

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat_registry.hpp"
#include "radar/log_record.hpp"
#include "radar/log_sink.hpp"
#include "radar/logger.hpp"
#include "radar/metrics.hpp"
#include "radar/radar_producer.hpp"
#include "radar/radar_signal.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/signal_handler.hpp"
#include "radar/thread_pool.hpp"
#include "radar/thread_safe_queue.hpp"
#include "radar/watchdog.hpp"

#include <memory>
#include <vector>

namespace radar {

// Composition root of the whole system (ARCHITECTURE.md "Application"). It owns
// every subsystem and every shared resource, wires them by
// constructor injection, and drives the application lifecycle. It is the only place
// where the concrete dependency graph is assembled, which keeps main() trivial and
// every subsystem free of global state.
//
// Lifecycle:
//   * Construction wires the graph and installs the SignalHandler (which blocks
//     SIGINT / SIGTERM on the main thread so every later thread inherits the mask).
//     No worker thread is started during construction.
//   * run() applies the process-wide realtime stage, starts the threads, blocks the
//     main thread until shutdown is requested (a signal, a watchdog escalation, or a
//     worker's exception boundary), then performs the ordered drain and prints a
//     final summary.
//
// Shutdown is a single ordered drain (ARCHITECTURE.md "Shutdown (ordered drain)"):
// the ShutdownToken is set, queues are closed in data-flow order, and each
// consumer is joined before the queue it feeds is closed. The same teardown is the
// startup-failure rollback path, so no ManagedThread is ever joined while its
// loop is still running unsignalled; the RAII join in ManagedThread is only a safety
// net.
//
// Owns synchronization primitives and threads, and hands out references to itself
// throughout the graph: non-copyable and non-movable.
class Application {
public:
    explicit Application(Config config);

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    ~Application() = default;

    // Runs the application until shutdown is requested, then drains and tears down.
    // Returns a process exit code. On a startup failure after threads have started
    // it runs the same teardown and rethrows, so the caller (main) maps it to a
    // non-zero exit code.
    int run();

private:
    void startThreads();
    void shutdown() noexcept;
    void reportSummary() const;

    [[nodiscard]] static std::vector<std::unique_ptr<ILogSink>> buildSinks(const Config& config);

    // Declaration order is initialization order: every dependency is constructed
    // before the subsystem that borrows it. Destruction (reverse order) tears the
    // graph down dependencies-last, but run()/shutdown() has already joined all
    // threads by then.
    Config config_;
    std::unique_ptr<Clock> clock_;
    ShutdownToken shutdownToken_;
    Metrics metrics_;
    RealtimeManager realtimeManager_;
    ThreadSafeQueue<RadarSignal> inputQueue_;
    ThreadSafeQueue<LogRecord> outputQueue_;
    HeartbeatRegistry heartbeatRegistry_;
    RadarProducer producer_;
    ThreadPool pool_;
    Logger logger_;
    Watchdog watchdog_;
    SignalHandler signalHandler_;

    bool stopped_{false};
};

}  // namespace radar
