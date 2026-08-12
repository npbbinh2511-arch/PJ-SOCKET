#include "hftp/client/client.h"
#include "hftp/network/socket.h"

#include <array>
#include <cassert>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

using hftp::client::Client;
using hftp::common::Error;
using hftp::network::Socket;
using hftp::network::SocketRuntime;

namespace {

std::uint16_t listener_port(Socket& listener) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    listener = Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    assert(listener.valid());
    assert(::bind(listener.native_handle(), reinterpret_cast<sockaddr*>(&address),
                  static_cast<int>(sizeof(address))) == 0);
    assert(::listen(listener.native_handle(), 1) == 0);

#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    assert(::getsockname(listener.native_handle(), reinterpret_cast<sockaddr*>(&address),
                         &length) == 0);
    return ntohs(address.sin_port);
}

void send_bytes(hftp::network::NativeSocket socket, std::string_view bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const int result = ::send(socket, bytes.data() + sent,
                                  static_cast<int>(bytes.size() - sent), 0);
        assert(result > 0);
        sent += static_cast<std::size_t>(result);
    }
}

} // namespace

int main() {
    SocketRuntime runtime;
    Socket listener;
    const std::uint16_t port = listener_port(listener);

    std::thread peer([&listener] {
        Socket accepted(::accept(listener.native_handle(), nullptr, nullptr));
        assert(accepted.valid());

        std::string command;
        std::array<char, 16> bytes{};
        while (command.find("\r\n") == std::string::npos) {
            const int received = ::recv(accepted.native_handle(), bytes.data(),
                                        static_cast<int>(bytes.size()), 0);
            assert(received > 0);
            command.append(bytes.data(), static_cast<std::size_t>(received));
        }
        assert(command == "NOOP\r\n");

        send_bytes(accepted.native_handle(), "200 Command ");
        send_bytes(accepted.native_handle(), "okay\r\n220 Next reply\r\n");
    });

    Client client;
    assert(client.connect("127.0.0.1", port));
    assert(client.connected());
    assert(client.send_command("NOOP"));

    std::vector<std::string> replies;
    assert(client.receive_reply(replies));
    assert(replies.size() == 2);
    assert(replies[0] == "200 Command okay");
    assert(replies[1] == "220 Next reply");

    const auto injected = client.send_command("NOOP\r\nQUIT");
    assert(!injected);
    assert(injected.error == Error::invalid_argument);

    peer.join();
    const auto closed = client.receive_reply(replies);
    assert(!closed);
    assert(closed.error == Error::socket_error);
    assert(!client.connected());

    listener.close();
    Client refused;
    const auto refused_status = refused.connect("127.0.0.1", port);
    assert(!refused_status);
    assert(refused_status.error == Error::socket_error);
    assert(refused_status.message.find("error 0") == std::string::npos);
}
