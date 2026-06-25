#include "radar/file_sink.hpp"

#include "radar/exceptions.hpp"

#include <ios>

namespace radar {

FileSink::FileSink(const std::string& path) : stream_(path, std::ios::out | std::ios::trunc) {
    if (!stream_.is_open()) {
        throw RadarException("FileSink: failed to open log file '" + path + "'");
    }
}

void FileSink::write(std::string_view line) {
    stream_ << line;
}

void FileSink::flush() {
    stream_.flush();
}

}  // namespace radar
