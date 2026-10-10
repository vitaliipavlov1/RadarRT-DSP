#pragma once

#include "radar/config.hpp"
#include "radar/shutdown_token.hpp"

namespace radar {

// Deterministic, async-signal-safe shutdown trigger (ARCHITECTURE.md "Signal
// Handling Strategy"). It turns SIGINT /
// SIGTERM into a single ShutdownToken request and performs no work inside a
// signal context — the only thing that happens "because of" a signal is a byte
// becoming readable on a signalfd, which is consumed from ordinary thread context.
//
// Installation (done in the constructor, matching the startup sequence step
// "SignalHandler installed (SIGINT / SIGTERM blocked, signalfd armed)"):
//   1. SIGINT / SIGTERM are blocked on the calling (main) thread with
//      pthread_sigmask. Every thread created afterwards inherits this mask, so the
//      signals end up blocked in all threads.
//      The handler must therefore be constructed before any worker thread starts.
//   2. A signalfd is armed for those signals, so their delivery becomes a readable
//      file descriptor instead of an asynchronous interrupt.
// Both system calls are validated; a failure throws SystemCallException, because a
// signalfd that could not be armed is a hard startup error, not a degradable
// best-effort feature like realtime scheduling.
//
// waitForShutdown() is the main thread's wait: it blocks until either a signal
// arrives (then it sets the ShutdownToken) or the token has already been set by
// another subsystem. Per D4 the ShutdownToken must also be able to end this wait,
// yet the token is set by threads (watchdog escalation, a worker's exception
// boundary) that know nothing about this SignalHandler, and it signals through a
// condition variable rather than a file descriptor. Rather than add a wakeup fd to
// the completed, portable ShutdownToken, the wait blocks in the kernel on ppoll
// with a bounded interval and re-checks the token on each wake. This is not busy
// waiting (ppoll sleeps in the kernel between checks, like the producer's and
// watchdog's periodic waits) and bounds the non-signal shutdown latency to one
// poll interval — negligible for a graceful drain.
//
// Owns a file descriptor and borrows its dependencies by reference: non-copyable
// and non-movable.
class SignalHandler {
public:
    SignalHandler(const Config& config, ShutdownToken& shutdownToken);

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;
    SignalHandler(SignalHandler&&) = delete;
    SignalHandler& operator=(SignalHandler&&) = delete;

    ~SignalHandler();

    // Block the calling (main) thread until SIGINT / SIGTERM is received or the
    // ShutdownToken is set by another subsystem. On a received signal it requests
    // shutdown via the token before returning. Runs on the main thread; an
    // escaping SystemCallException signals an unrecoverable wait error to the
    // caller, which treats it as a shutdown trigger.
    void waitForShutdown();

private:
    void consumePendingSignal() const;

    const Config& config_;
    ShutdownToken& shutdownToken_;
    int signalFd_;
};

}  // namespace radar
