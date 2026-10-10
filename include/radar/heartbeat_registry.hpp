#pragma once

#include "radar/heartbeat.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace radar {

// Owns every Heartbeat for the lifetime of the application and is the single point
// the Watchdog reads (ARCHITECTURE.md D3). Workers borrow their heartbeat by
// reference.
//
// Registration is static: perform all registrations before worker threads start.
// The registry is not modified afterwards, which is exactly what makes the
// Watchdog's reads lock-free. std::deque is used so the references handed out by
// registerHeartbeat() remain valid for the registry's whole lifetime even as more
// heartbeats are registered.
//
// Non-copyable and non-movable: references to the owned heartbeats escape to
// workers, so the registry must keep a fixed identity.
class HeartbeatRegistry {
public:
    HeartbeatRegistry() = default;
    ~HeartbeatRegistry() = default;

    HeartbeatRegistry(const HeartbeatRegistry&) = delete;
    HeartbeatRegistry& operator=(const HeartbeatRegistry&) = delete;
    HeartbeatRegistry(HeartbeatRegistry&&) = delete;
    HeartbeatRegistry& operator=(HeartbeatRegistry&&) = delete;

    // Registers a named heartbeat and returns a stable reference for the worker.
    // Not thread-safe: call before worker threads start (static registration).
    [[nodiscard]] Heartbeat& registerHeartbeat(std::string name);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Lock-free, read-only visit of each (name, heartbeat). Safe to call
    // concurrently with worker heartbeat updates (atomic), provided no registration
    // is in progress.
    template <class Visitor>
    void forEach(Visitor&& visitor) const {
        for (const Entry& entry : entries_) {
            visitor(entry.name, entry.heartbeat);
        }
    }

private:
    struct Entry {
        explicit Entry(std::string entryName) : name(std::move(entryName)) {}
        std::string name;
        Heartbeat heartbeat;
    };

    std::deque<Entry> entries_;
};

}  // namespace radar
