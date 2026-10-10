#pragma once

#include <string_view>

namespace radar {

// Output sink for already-formatted log lines (ARCHITECTURE.md: ILogSink exists
// for output substitution). The Logger formats records into text and writes the
// text to each sink; sinks perform only I/O, never formatting. A sink is used from
// the single Logger thread, so implementations need no internal synchronization.
//
// Polymorphic base: non-copyable and non-movable.
class ILogSink {
public:
    virtual ~ILogSink() = default;

    virtual void write(std::string_view line) = 0;
    virtual void flush() = 0;

    ILogSink(const ILogSink&) = delete;
    ILogSink& operator=(const ILogSink&) = delete;
    ILogSink(ILogSink&&) = delete;
    ILogSink& operator=(ILogSink&&) = delete;

protected:
    ILogSink() = default;
};

}  // namespace radar
