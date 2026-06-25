#pragma once

#include "radar/log_sink.hpp"

#include <fstream>
#include <string>
#include <string_view>

namespace radar {

// Writes formatted log lines to a file. The file is opened (truncated) on
// construction and closed — which flushes it — on destruction.
class FileSink final : public ILogSink {
public:
    explicit FileSink(const std::string& path);

    void write(std::string_view line) override;
    void flush() override;

private:
    std::ofstream stream_;
};

}  // namespace radar
