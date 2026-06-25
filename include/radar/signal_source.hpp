#pragma once

namespace radar {

// Abstraction of a radar signal source so the simulated RadarProducer can be
// substituted by a real-hardware source or a test double (ARCHITECTURE.md). A
// source owns its producing thread; the Application starts it and, during the
// ordered shutdown, joins it after closing the input queue. Stopping is
// cooperative through the shared ShutdownToken, so there is no separate stop().
//
// Polymorphic base: non-copyable and non-movable.
class ISignalSource {
public:
    virtual ~ISignalSource() = default;

    // Begin producing (spawn the producing thread).
    virtual void start() = 0;
    // Wait for the producing thread to finish.
    virtual void join() = 0;

    ISignalSource(const ISignalSource&) = delete;
    ISignalSource& operator=(const ISignalSource&) = delete;
    ISignalSource(ISignalSource&&) = delete;
    ISignalSource& operator=(ISignalSource&&) = delete;

protected:
    ISignalSource() = default;
};

}  // namespace radar
