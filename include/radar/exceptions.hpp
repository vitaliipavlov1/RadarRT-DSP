#pragma once

#include <stdexcept>
#include <string>

namespace radar {

// Base type for every exception raised by RadarRT-DSP subsystems, so a caller can
// catch the whole hierarchy with a single handler at a thread or scope boundary.
class RadarException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Invalid or missing configuration.
class ConfigException : public RadarException {
public:
    using RadarException::RadarException;
};

// A POSIX / system call failed. Carries the failed call name and the captured
// errno so that validation of system calls never silently discards the cause.
class SystemCallException : public RadarException {
public:
    SystemCallException(const std::string& call, int errorNumber);

    [[nodiscard]] const std::string& call() const noexcept { return call_; }
    [[nodiscard]] int errorNumber() const noexcept { return errorNumber_; }

private:
    std::string call_;
    int errorNumber_;
};

}  // namespace radar
