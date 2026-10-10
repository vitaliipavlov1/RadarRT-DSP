#include "radar/console_sink.hpp"

#include <iostream>

namespace radar {

void ConsoleSink::write(std::string_view line) {
    std::cout << line;
}

void ConsoleSink::flush() {
    std::cout.flush();
}

}  // namespace radar
