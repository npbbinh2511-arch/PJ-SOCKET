#include "hftp/network/socket.h"

#include <system_error>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace hftp::network {

SocketRuntime::SocketRuntime() {
#ifdef _WIN32
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        throw std::system_error(result, std::system_category(), "WSAStartup failed");
    }
#endif
}

SocketRuntime::~SocketRuntime() noexcept {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::~Socket() noexcept {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : handle_(other.release()) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.release();
    }
    return *this;
}

NativeSocket Socket::release() noexcept {
    return std::exchange(handle_, invalid_socket);
}

void Socket::close() noexcept {
    if (!valid()) {
        return;
    }

#ifdef _WIN32
    closesocket(handle_);
#else
    ::close(handle_);
#endif
    handle_ = invalid_socket;
}

} // namespace hftp::network
