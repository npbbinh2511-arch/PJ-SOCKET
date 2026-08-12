#include "hftp/client/client.h"

#include <array>
#include <limits>
#include <string>
#include <utility>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace hftp::client {
namespace {

int current_socket_error() noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

common::Status socket_failure(std::string message, int error) {
#ifdef _WIN32
    message += " (WinSock error " + std::to_string(error) + ')';
#else
    message += ": ";
    message += std::strerror(error);
#endif
    return {common::Error::socket_error, std::move(message)};
}

int close_direction() noexcept {
#ifdef _WIN32
    return SD_BOTH;
#else
    return SHUT_RDWR;
#endif
}

int send_flags() noexcept {
#ifdef _WIN32
    return 0;
#else
    return MSG_NOSIGNAL;
#endif
}

} // namespace

common::Status Client::connect(std::string_view host, std::uint16_t port) {
    if (host.empty() || port == 0) {
        return {common::Error::invalid_argument, "Host is empty or port is zero"};
    }

    disconnect();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string host_text(host);
    const std::string port_text = std::to_string(port);
    addrinfo* addresses = nullptr;
    const int resolve_result =
        getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &addresses);
    if (resolve_result != 0) {
        return {common::Error::socket_error,
                "Unable to resolve server address (resolver error " +
                    std::to_string(resolve_result) + ')'};
    }

    int last_error = 0;
    for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        network::Socket candidate(
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!candidate.valid()) {
            last_error = current_socket_error();
            continue;
        }

#ifdef _WIN32
        const int address_length = static_cast<int>(address->ai_addrlen);
#else
        const socklen_t address_length = static_cast<socklen_t>(address->ai_addrlen);
#endif
        if (::connect(candidate.native_handle(), address->ai_addr, address_length) == 0) {
            control_socket_ = std::move(candidate);
            break;
        }
        last_error = current_socket_error();
    }

    freeaddrinfo(addresses);
    if (!control_socket_.valid()) {
        return last_error == 0
            ? common::Status{common::Error::socket_error,
                             "Unable to connect to any resolved server address"}
            : socket_failure("Unable to connect to server", last_error);
    }
    connected_.store(true);
    return {};
}

common::Status Client::send_command(std::string_view command) {
    const std::scoped_lock send_lock(send_mutex_);
    if (!connected()) {
        return {common::Error::socket_error, "Control connection is not open"};
    }
    if (command.find_first_of("\r\n") != std::string_view::npos) {
        return {common::Error::invalid_argument, "Command must not contain CR or LF"};
    }

    std::string wire_command(command);
    wire_command.append("\r\n");

    std::size_t sent = 0;
    while (sent < wire_command.size()) {
        const std::size_t remaining = wire_command.size() - sent;
        const int chunk_size = static_cast<int>(
            remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
                ? std::numeric_limits<int>::max()
                : remaining);
        const int result = ::send(
            control_socket_.native_handle(), wire_command.data() + sent,
            chunk_size, send_flags());
        if (result <= 0) {
            const int error = current_socket_error();
            connected_.store(false);
            ::shutdown(control_socket_.native_handle(), close_direction());
            return socket_failure("Unable to send FTP command", error);
        }
        sent += static_cast<std::size_t>(result);
    }
    return {};
}

common::Status Client::receive_reply(std::vector<std::string>& replies) {
    replies.clear();
    if (!connected()) {
        return {common::Error::socket_error, "Control connection is not open"};
    }

    std::array<char, 4096> bytes{};
    while (replies.empty()) {
        const int received = ::recv(
            control_socket_.native_handle(), bytes.data(), static_cast<int>(bytes.size()), 0);
        if (received == 0) {
            connected_.store(false);
            return {common::Error::socket_error, "Server closed the control connection"};
        }
        if (received < 0) {
            const int error = current_socket_error();
            connected_.store(false);
            return socket_failure("Unable to receive FTP reply", error);
        }

        auto framed = reply_framer_.push(
            std::string_view(bytes.data(), static_cast<std::size_t>(received)));
        if (reply_framer_.failed()) {
            connected_.store(false);
            ::shutdown(control_socket_.native_handle(), close_direction());
            return {common::Error::protocol_error, "FTP reply exceeds maximum line length"};
        }
        for (auto& line : framed) {
            replies.push_back(std::move(line));
        }
    }
    return {};
}

common::Status Client::local_ipv4(std::string& address) const {
    address.clear();
    if (!connected() || !control_socket_.valid()) {
        return {common::Error::socket_error, "Control connection is not open"};
    }
    sockaddr_storage storage{};
#ifdef _WIN32
    int length = sizeof(storage);
#else
    socklen_t length = sizeof(storage);
#endif
    if (::getsockname(control_socket_.native_handle(),
                      reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        return socket_failure(
            "Unable to read local control address", current_socket_error());
    }
    if (storage.ss_family != AF_INET) {
        return {common::Error::invalid_argument,
                "Active UDP mode currently requires IPv4"};
    }
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&storage);
    std::array<char, INET_ADDRSTRLEN> text{};
    if (::inet_ntop(AF_INET, &ipv4->sin_addr, text.data(), text.size()) == nullptr) {
        return socket_failure(
            "Unable to format local IPv4 address", current_socket_error());
    }
    address = text.data();
    return {};
}

void Client::disconnect() noexcept {
    connected_.store(false);
    const std::scoped_lock send_lock(send_mutex_);
    if (control_socket_.valid()) {
        ::shutdown(control_socket_.native_handle(), close_direction());
        control_socket_.close();
    }
    reply_framer_ = control::CrlfFramer{};
}

} // namespace hftp::client
