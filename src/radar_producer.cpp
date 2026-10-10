#include "radar/radar_producer.hpp"

#include "radar/diagnostics.hpp"

#include <chrono>
#include <utility>

namespace radar {

RadarProducer::RadarProducer(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
                             ThreadSafeQueue<RadarSignal>& inputQueue, Heartbeat& heartbeat,
                             const RealtimeManager& realtimeManager,
                             RandomGenerator randomGenerator, Metrics& metrics)
    : config_(config), clock_(clock), shutdownToken_(shutdownToken), inputQueue_(inputQueue),
      heartbeat_(heartbeat), realtimeManager_(realtimeManager),
      randomGenerator_(std::move(randomGenerator)), metrics_(metrics) {}

void RadarProducer::start() {
    thread_ = ManagedThread(&RadarProducer::run, this);
}

void RadarProducer::join() {
    thread_.join();
}

void RadarProducer::run() {
    try {
        produceLoop();
    } catch (...) {
        // Top-level exception boundary (D7): never let an exception escape the
        // thread. Log it (best-effort via stderr — the producer has no access to the
        // log queue), record it in Metrics, then request graceful shutdown so the rest
        // of the system winds down.
        reportUnhandledException("producer");
        metrics_.recordThreadException();
        shutdownToken_.requestShutdown();
    }
}

void RadarProducer::produceLoop() {
    // Per-thread realtime stage (D2), applied from inside this thread. Graceful
    // degradation: on failure (e.g. missing CAP_SYS_NICE) a warning is emitted and the
    // thread continues at default scheduling rather than aborting.
    const RealtimeResult realtimeStatus =
        realtimeManager_.configureCurrentThread(config_.realtime().producerPriority);
    reportRealtimeDegradation(realtimeStatus, "producer");

    const Clock::Duration period = config_.producer().samplePeriod;
    Clock::TimePoint nextDeadline = clock_.now();

    while (!shutdownToken_.shutdownRequested()) {
        const Clock::TimePoint sampleTime = clock_.now();
        heartbeat_.markRunning(sampleTime);

        if (!inputQueue_.push(generateSignal(sampleTime))) {
            break;  // input queue closed during shutdown: exit promptly
        }
        metrics_.recordSignalProduced();
        metrics_.setInputQueueDepth(inputQueue_.size());
        metrics_.setInputDrops(inputQueue_.droppedCount());

        nextDeadline += period;
        const Clock::TimePoint now = clock_.now();
        const Clock::Duration remaining = nextDeadline - now;
        if (remaining <= Clock::Duration::zero()) {
            // Deadline missed: realign to now and produce the next sample
            // immediately.
            metrics_.recordDeadlineMiss();
            nextDeadline = now;
            continue;
        }

        // Legitimate periodic wait (not a stall), woken immediately on shutdown.
        heartbeat_.markWaiting();
        if (shutdownToken_.waitFor(remaining)) {
            break;  // shutdown requested during the inter-sample wait
        }
    }
}

RadarSignal RadarProducer::generateSignal(Clock::TimePoint timestamp) {
    RadarSignal::SampleBuffer samples;
    for (float& sample : samples) {
        sample = static_cast<float>(randomGenerator_.uniformReal(-1.0, 1.0));
    }
    return RadarSignal(timestamp, samples);
}

}  // namespace radar
