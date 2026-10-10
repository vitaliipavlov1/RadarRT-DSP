#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

namespace radar {

// Immutable, injected configuration object. Construct it once (defaults, or with
// selected overrides via Params) and pass it to subsystems as `const Config&`.
// Components read parameters from here instead of reaching for global state.
//
// Values are grouped by subsystem. A configuration loader (file / CLI) can build
// a Params and construct a Config from it; the loader itself is a separate concern
// at the application entry point. After construction a Config exposes no
// mutators, so it is immutable through its public interface.
class Config {
public:
    struct Producer {
        std::chrono::microseconds samplePeriod{1000};  // 1 kHz default
    };

    struct Queues {
        std::size_t inputCapacity{256};
        std::size_t outputCapacity{1024};
    };

    struct Pool {
        std::size_t workerCount{4};
    };

    struct Watchdog {
        std::chrono::milliseconds scanInterval{100};
        std::chrono::milliseconds livenessTimeout{500};
        bool escalateToShutdown{false};
    };

    struct Realtime {
        bool enabled{true};
        bool lockMemory{true};
        int producerPriority{80};
        int dspPriority{70};
        int watchdogPriority{60};
        int loggerPriority{40};
    };

    struct Logging {
        std::string filePath{"radar.log"};
        bool console{true};
    };

    struct Signal {
        // Bound on how long the main-thread signal wait blocks in the kernel before
        // re-checking the ShutdownToken. The token may be set by subsystems that do
        // not write to the signalfd (watchdog escalation, a worker's exception
        // boundary), so this interval bounds the non-signal shutdown latency. A
        // received signal still wakes the wait immediately; this only caps the delay
        // for token-driven shutdown. Not busy waiting: the wait sleeps in the kernel.
        std::chrono::milliseconds pollInterval{100};
    };

    struct Params {
        Producer producer{};
        Queues queues{};
        Pool pool{};
        Watchdog watchdog{};
        Realtime realtime{};
        Logging logging{};
        Signal signal{};
    };

    Config() = default;
    explicit Config(Params params) : params_(std::move(params)) {}

    [[nodiscard]] const Producer& producer() const noexcept { return params_.producer; }
    [[nodiscard]] const Queues& queues() const noexcept { return params_.queues; }
    [[nodiscard]] const Pool& pool() const noexcept { return params_.pool; }
    [[nodiscard]] const Watchdog& watchdog() const noexcept { return params_.watchdog; }
    [[nodiscard]] const Realtime& realtime() const noexcept { return params_.realtime; }
    [[nodiscard]] const Logging& logging() const noexcept { return params_.logging; }
    [[nodiscard]] const Signal& signal() const noexcept { return params_.signal; }

private:
    Params params_{};
};

}  // namespace radar
