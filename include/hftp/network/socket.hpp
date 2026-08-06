#pragma once

#include <cstdint>

#ifdef _WIN32
#include <WinSock2.h>
#else
using SOCKET = std::uintptr_t;
inline constexpr SOCKET INVALID_SOCKET = static_cast<SOCKET>(-1);
#endif

namespace hftp::network {

class WinSockRuntime {
public:
    WinSockRuntime();
    ~WinSockRuntime();
    WinSockRuntime(const WinSockRuntime&) = delete;
    WinSockRuntime& operator=(const WinSockRuntime&) = delete;

    // TODO(B):
    // - Call WSAStartup for a supported WinSock version and retain the outcome.
    // - Surface startup failure without leaking partially initialized process state.
    // - Pair every successful startup with exactly one WSACleanup in the destructor.
    // - Tests: startup success, injected startup failure, single cleanup.
};

class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(SOCKET handle) noexcept : handle_(handle) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] SOCKET native_handle() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != INVALID_SOCKET; }
    SOCKET release() noexcept;
    void close() noexcept;

    // TODO(B):
    // - Implement exclusive ownership for a WinSock socket handle.
    // - Make move operations leave the source invalid and close at most once.
    // - Treat shutdown separately from close so protocol owners control sequencing.
    // - Tests: default state, move construction/assignment, release, idempotent close.

private:
    SOCKET handle_{INVALID_SOCKET};
};

} // namespace hftp::network

