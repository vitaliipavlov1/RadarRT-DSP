#pragma once

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat.hpp"
#include "radar/managed_thread.hpp"
#include "radar/metrics.hpp"
#include "radar/radar_signal.hpp"
#include "radar/random_generator.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/signal_source.hpp"
#include "radar/thread_safe_queue.hpp"

namespace radar {

// Simulated radar device. On its own thread it periodically generates timestamped
// RadarSignals and pushes them into the input queue. The cadence is scheduled
// against absolute deadlines (no cumulative drift); the inter-sample wait is done
// on the ShutdownToken so it is woken immediately on shutdown (D4). Liveness is
// reported through a Heartbeat. Implements ISignalSource so a real-hardware source
// or a test double can replace it. Reports signals produced and deadline
// misses to the injected Metrics, and refreshes the approximate input queue
// depth gauge after each successful push.
//
// Owns its producing thread; non-copyable and non-movable (via ISignalSource).
class RadarProducer final : public ISignalSource {
public:
    RadarProducer(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
                  ThreadSafeQueue<RadarSignal>& inputQueue, Heartbeat& heartbeat,
                  const RealtimeManager& realtimeManager, RandomGenerator randomGenerator,
                  Metrics& metrics);

    void start() override;
    void join() override;

private:
    void run();
    void produceLoop();
    [[nodiscard]] RadarSignal generateSignal(Clock::TimePoint timestamp);

    const Config& config_;
    const Clock& clock_;
    ShutdownToken& shutdownToken_;
    ThreadSafeQueue<RadarSignal>& inputQueue_;
    Heartbeat& heartbeat_;
    const RealtimeManager& realtimeManager_;
    RandomGenerator randomGenerator_;
    Metrics& metrics_;
    ManagedThread thread_;
};

}  // namespace radar
