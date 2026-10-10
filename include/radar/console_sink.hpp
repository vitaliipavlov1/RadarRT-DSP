#pragma once

#include "radar/log_sink.hpp"

#include <string_view>

namespace radar {

// Writes formatted log lines to standard output.
class ConsoleSink final : public ILogSink {
public:
    void write(std::string_view line) override;
    void flush() override;
};

}  // namespace radar
