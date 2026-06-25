#include "radar/exceptions.hpp"

#include <string>
#include <system_error>

namespace radar {

namespace {

std::string buildSystemCallMessage(const std::string& call, int errorNumber) {
    // std::generic_category().message() is the portable, thread-safe way to turn
    // an errno into a description, avoiding the strerror_r feature-macro pitfalls.
    return "system call '" + call + "' failed: " + std::generic_category().message(errorNumber) +
           " (errno " + std::to_string(errorNumber) + ")";
}

}  // namespace

SystemCallException::SystemCallException(const std::string& call, int errorNumber)
    : RadarException(buildSystemCallMessage(call, errorNumber)), call_(call),
      errorNumber_(errorNumber) {}

}  // namespace radar
