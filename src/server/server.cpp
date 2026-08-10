#include "hftp/server/server.h"

#include <array>
#include <string>
#include <string_view>

#include "hftp/control/command_parser.h"
#include "hftp/control/command_dispatcher.h"
#include "hftp/control/crlf_framer.h"
#include "hftp/protocol/reply.h"
#include "hftp/session/session.h"

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace hftp::server {
namespace {

common::Status socket_failure(std::string message) {
#ifdef _WIN32
    message += " (WinSock error " + std::to_string(WSAGetLastError()) + ')';
#else
    message += ": ";
    message += std::strerror(errno);
#endif
    return {common::Error::socket_error, std::move(message)};
}

bool send_all(network::NativeSocket socket, std::string_view bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const int result = ::send(
            socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

class SocketReplySink final : public control::ReplySink {
public:
    explicit SocketReplySink(network::NativeSocket socket) : socket_(socket) {}

    void send(protocol::ReplyCode code, std::string text) override {
        if (!failed_) {
            failed_ = !send_all(socket_, formatter_.format(code, text));
        }
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    network::NativeSocket socket_;
    protocol::ReplyFormatter formatter_;
    bool failed_{false};
};

} // namespace

Server::~Server() {
    request_stop();
    join();
}

common::Status Server::start(std::uint16_t control_port) {
    if (running()) {
        return {common::Error::busy, "Server is already running"};
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    network::Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener.valid()) {
        return socket_failure("Unable to create TCP listener");
    }

    const int reuse_address = 1;
    ::setsockopt(listener.native_handle(), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(control_port);
    if (::bind(listener.native_handle(), reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        return socket_failure("Unable to bind TCP listener");
    }
    if (::listen(listener.native_handle(), SOMAXCONN) != 0) {
        return socket_failure("Unable to listen on TCP socket");
    }

#ifdef _WIN32
    int address_length = sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    if (::getsockname(listener.native_handle(), reinterpret_cast<sockaddr*>(&address),
                      &address_length) != 0) {
        return socket_failure("Unable to read TCP listener endpoint");
    }

    listener_ = std::move(listener);
    control_port_ = ntohs(address.sin_port);
    stopping_.store(false);
    running_.store(true);
    accept_thread_ = std::jthread([this] { accept_loop(); });
    return {};
}

void Server::request_stop() noexcept {
    stopping_.store(true);
}

void Server::join() {
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    listener_.close();
}

void Server::accept_loop() {
    const network::NativeSocket listener = listener_.native_handle();
    while (!stopping_.load()) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{};
        timeout.tv_usec = 100000;

#ifdef _WIN32
        const int selected = ::select(0, &readable, nullptr, nullptr, &timeout);
#else
        const int selected = ::select(listener + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (selected <= 0) {
            continue;
        }

        network::Socket client(::accept(listener, nullptr, nullptr));
        if (!client.valid()) {
            if (stopping_.load()) {
                break;
            }
            continue;
        }
        if (stopping_.load()) {
            break;
        }

        handle_client(std::move(client));
    }
    running_.store(false);
}

void Server::handle_client(network::Socket client) {
    session::Session session(next_session_id_.fetch_add(1));
    control::CrlfFramer framer;
    const control::CommandParser parser;
    SocketReplySink replies(client.native_handle());

    replies.send(protocol::ReplyCode::ready, "Service ready");
    if (replies.failed()) {
        return;
    }

    std::array<char, 4096> bytes{};
    while (!stopping_.load()) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(client.native_handle(), &readable);
        timeval timeout{};
        timeout.tv_usec = 100000;

#ifdef _WIN32
        const int selected = ::select(0, &readable, nullptr, nullptr, &timeout);
#else
        const int selected =
            ::select(client.native_handle() + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (selected <= 0) {
            continue;
        }

        const int received = ::recv(
            client.native_handle(), bytes.data(), static_cast<int>(bytes.size()), 0);
        if (received <= 0) {
            return;
        }

        auto lines = framer.push(
            std::string_view(bytes.data(), static_cast<std::size_t>(received)));
        if (framer.failed()) {
            replies.send(protocol::ReplyCode::syntax_error,
                         "Command line exceeds maximum length");
            return;
        }

        for (const auto& line : lines) {
            const auto parsed = parser.parse(line);
            if (!parsed.ok) {
                replies.send(protocol::ReplyCode::syntax_error, "Syntax error");
                if (replies.failed()) {
                    return;
                }
                continue;
            }

            const auto action = dispatcher_.dispatch(parsed.command, session, replies);
            if (replies.failed() || action == control::DispatchAction::close_session) {
                return;
            }
        }
    }
}

} // namespace hftp::server
