#include "radar/signal_handler.hpp"

#include "radar/exceptions.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <poll.h>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace radar {
namespace {

// The set of signals this handler turns into a graceful-shutdown request.
sigset_t shutdownSignalSet() noexcept {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    return set;
}

}  // namespace

SignalHandler::SignalHandler(const Config& config, ShutdownToken& shutdownToken)
    : config_(config), shutdownToken_(shutdownToken), signalFd_(-1) {
    sigset_t mask = shutdownSignalSet();

    // Block the signals on this (main) thread so they are never handled by their
    // default disposition and are delivered to the signalfd instead. Threads created
    // afterwards inherit this mask, which is why construction must precede thread
    // start. pthread_sigmask returns the error number directly rather than via errno.
    const int blockResult = pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    if (blockResult != 0) {
        throw SystemCallException("pthread_sigmask", blockResult);
    }

    // Arm the signalfd for the blocked signals. SFD_CLOEXEC prevents the descriptor
    // from leaking across an exec. On failure the signals remain blocked, which is
    // harmless: this is a hard startup error and the caller aborts startup.
    signalFd_ = signalfd(-1, &mask, SFD_CLOEXEC);
    if (signalFd_ < 0) {
        throw SystemCallException("signalfd", errno);
    }
}

SignalHandler::~SignalHandler() {
    if (signalFd_ >= 0) {
        // Best-effort close in a destructor: the fd is released by the kernel even if
        // close() reports EINTR, and there is no caller to surface an error to.
        (void)::close(signalFd_);
    }
}

void SignalHandler::waitForShutdown() {
    const std::chrono::milliseconds interval = config_.signal().pollInterval;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(interval);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(interval - seconds);

    struct timespec timeout;
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(seconds.count());
    timeout.tv_nsec = static_cast<decltype(timeout.tv_nsec)>(nanoseconds.count());

    // Re-check the token first so an already-requested shutdown returns without ever
    // blocking, then block in the kernel on ppoll between checks (no busy waiting).
    while (!shutdownToken_.shutdownRequested()) {
        struct pollfd pollFd;
        pollFd.fd = signalFd_;
        pollFd.events = POLLIN;
        pollFd.revents = 0;

        const int ready = ppoll(&pollFd, 1, &timeout, nullptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by an unrelated signal; retry the wait
            }
            throw SystemCallException("ppoll", errno);
        }
        if (ready == 0) {
            continue;  // timeout: re-check the token, which may have been set elsewhere
        }
        if ((pollFd.revents & POLLIN) != 0) {
            consumePendingSignal();
            shutdownToken_.requestShutdown();
            return;
        }
        // signalfd reports no events other than POLLIN under normal operation; any
        // spurious wake simply loops and re-polls.
    }
}

void SignalHandler::consumePendingSignal() const {
    // Drain one queued notification to clear the descriptor's readiness. The payload
    // is intentionally unused: per the spec no work is derived from a signal beyond
    // translating it into a shutdown request. ppoll already reported the fd readable,
    // so this read does not block.
    struct signalfd_siginfo info;
    const ssize_t bytes = read(signalFd_, &info, sizeof(info));
    if (bytes < 0) {
        throw SystemCallException("read", errno);
    }
    if (bytes != static_cast<ssize_t>(sizeof(info))) {
        // signalfd always returns whole signalfd_siginfo records; a short read is not
        // expected and is treated as an I/O error rather than silently ignored.
        throw SystemCallException("read", EIO);
    }
}

}  // namespace radar
