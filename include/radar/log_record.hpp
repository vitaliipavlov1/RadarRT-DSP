#pragma once

#include "radar/clock.hpp"

#include <optional>
#include <string>
#include <utility>

namespace radar {

enum class LogLevel { Info, Warning, Error };

enum class LogSource { Producer, Dsp, Watchdog };

// Structured, move-only logging record. It carries only data: no string
// formatting and no output logic — the Logger renders it later (ARCHITECTURE.md
// D8). A record produced on the realtime DSP path carries a compact result
// payload and leaves the message empty: crucially, no string is built or
// formatted there. An empty std::string performs no heap allocation under every
// mainstream standard library (small-string optimization); the C++ standard does
// not strictly guarantee this, so the "no formatting" property is the guaranteed
// one and the "no allocation" property holds in practice. Diagnostic records from
// non-realtime subsystems may set a message instead.
class LogRecord {
public:
    using Timestamp = Clock::TimePoint;

    // Compact result payload mirrored from a ProcessedSignal. Deliberately
    // independent of ProcessedSignal so the Logger never depends on the DSP
    // domain type.
    struct Result {
        bool targetDetected{false};
        float peakMagnitude{};
    };

    LogRecord(LogLevel level, Timestamp timestamp, LogSource source, Result result) noexcept
        : timestamp_(timestamp), result_(result), level_(level), source_(source) {}

    LogRecord(LogLevel level, Timestamp timestamp, LogSource source, std::string message) noexcept
        : timestamp_(timestamp), message_(std::move(message)), level_(level), source_(source) {}

    LogRecord(const LogRecord&) = delete;
    LogRecord& operator=(const LogRecord&) = delete;
    LogRecord(LogRecord&&) noexcept = default;
    LogRecord& operator=(LogRecord&&) noexcept = default;
    ~LogRecord() = default;

    [[nodiscard]] LogLevel level() const noexcept { return level_; }
    [[nodiscard]] Timestamp timestamp() const noexcept { return timestamp_; }
    [[nodiscard]] LogSource source() const noexcept { return source_; }
    [[nodiscard]] const std::optional<Result>& result() const noexcept { return result_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    Timestamp timestamp_{};
    std::optional<Result> result_{};
    std::string message_{};
    LogLevel level_{LogLevel::Info};
    LogSource source_{LogSource::Producer};
};

}  // namespace radar
