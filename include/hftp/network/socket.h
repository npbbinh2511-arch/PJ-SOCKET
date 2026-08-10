#ifndef HFTP_NETWORK_SOCKET_H
#define HFTP_NETWORK_SOCKET_H

#ifdef _WIN32
#include <WinSock2.h>
#endif

namespace hftp::network {

#ifdef _WIN32
using NativeSocket = SOCKET;
inline constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
inline constexpr NativeSocket invalid_socket = -1;
#endif

class SocketRuntime {
public:
    SocketRuntime();
    ~SocketRuntime() noexcept;
    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;

};

class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(NativeSocket handle) noexcept : handle_(handle) {}
    ~Socket() noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] NativeSocket native_handle() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != invalid_socket; }
    NativeSocket release() noexcept;
    void close() noexcept;

private:
    NativeSocket handle_{invalid_socket};
};

} // namespace hftp::network

#endif // HFTP_NETWORK_SOCKET_H
