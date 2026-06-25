#include "radar/application.hpp"
#include "radar/config.hpp"
#include "radar/exceptions.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

// Entry point. Kept deliberately minimal (see ARCHITECTURE.md): it constructs the
// Application with the
// default configuration and runs it. All wiring, lifecycle and shutdown ordering
// live in Application. A configuration loader (file / CLI) is a separate concern;
// until it exists, the default Config is used.
int main() {
    try {
        radar::Application application{radar::Config{}};
        return application.run();
    } catch (const std::exception& ex) {
        // Construction or an unrecoverable startup failure (e.g. signalfd could not
        // be armed) reaches here. Application has already rolled back any started
        // threads; report and exit non-zero.
        std::cerr << "[radar] fatal: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
