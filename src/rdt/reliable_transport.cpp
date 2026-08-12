#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "hftp/rdt/reliable_transport.h"
#include "hftp/rdt/packet.h"
#include "hftp/network/socket.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace hftp::rdt {
namespace {

constexpr std::size_t wire_header_size = 20;
constexpr std::size_t max_udp_datagram_size = 65'507;

#ifdef _WIN32
using SocketLength = int;
#else
using SocketLength = socklen_t;
#endif

class SocketGuard {
public:
    explicit SocketGuard(network::NativeSocket socket) noexcept : socket_(socket) {}
    ~SocketGuard() noexcept
    {
        if (socket_ == network::invalid_socket) {
            return;
        }
#ifdef _WIN32
        ::closesocket(socket_);
#else
        ::close(socket_);
#endif
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

private:
    network::NativeSocket socket_;
};

int last_socket_error() noexcept
{
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool is_timeout_error(int error) noexcept
{
#ifdef _WIN32
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

common::Status socket_failure(const char* operation)
{
    return {
        common::Error::socket_error,
        std::string(operation) + " failed (socket error " +
            std::to_string(last_socket_error()) + ")"
    };
}

common::Status set_socket_timeout(
    network::NativeSocket socket,
    std::chrono::milliseconds timeout)
{
#ifdef _WIN32
    const DWORD value = static_cast<DWORD>(timeout.count());
    const int result = ::setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&value),
        static_cast<int>(sizeof(value)));
#else
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
    value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
    const int result = ::setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &value,
        static_cast<socklen_t>(sizeof(value)));
#endif
    if (result != 0) {
        return socket_failure("setsockopt(SO_RCVTIMEO)");
    }
    return {};
}

bool same_peer(const sockaddr_in& lhs, const sockaddr_in& rhs) noexcept
{
    return lhs.sin_family == rhs.sin_family &&
           lhs.sin_port == rhs.sin_port &&
           lhs.sin_addr.s_addr == rhs.sin_addr.s_addr;
}

common::Status validate_context_and_options(
    const transfer::TransferContext& context,
    const StopAndWaitOptions& options)
{
    if (!context.endpoint.has_value()) {
        return {common::Error::invalid_argument, "Missing UDP endpoint context"};
    }
    if (context.transfer_id == 0 ||
        context.transfer_id > (std::numeric_limits<std::uint32_t>::max)()) {
        return {common::Error::invalid_argument, "Transfer ID is outside the wire range"};
    }
    if (context.endpoint->port == 0) {
        return {common::Error::invalid_argument, "UDP endpoint port must be non-zero"};
    }
    if (options.timeout.count() <= 0 || options.max_retries == 0) {
        return {common::Error::invalid_argument, "Timeout and retry count must be positive"};
    }
    if (options.payload_size == 0 ||
        options.payload_size > max_udp_datagram_size - wire_header_size) {
        return {common::Error::invalid_argument, "RDT payload size is outside the UDP range"};
    }
    if (options.window_size == 0) {
        return {common::Error::invalid_argument, "Window size must be positive"};
    }
    if (options.window_size > (std::numeric_limits<std::uint32_t>::max)()) {
        return {common::Error::invalid_argument, "Window size exceeds the sequence range"};
    }
    if (!std::isfinite(options.drop_probability) ||
        options.drop_probability < 0.0 || options.drop_probability > 1.0) {
        return {common::Error::invalid_argument, "Drop probability must be in [0, 1]"};
    }
    return {};
}

common::Status make_ipv4_endpoint(
    const session::UdpEndpoint& endpoint,
    sockaddr_in& address)
{
    address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    const int parsed = ::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr);
    if (parsed != 1) {
        return {common::Error::invalid_argument, "UDP endpoint is not a valid IPv4 address"};
    }
    return {};
}

common::Status bind_udp_socket(network::NativeSocket socket,
                               std::uint16_t port) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&local),
               static_cast<SocketLength>(sizeof(local))) != 0) {
        return socket_failure("bind");
    }
    return {};
}

void report_progress(const transfer::TransferContext& context,
                     std::uint64_t completed, std::uint64_t total) noexcept {
    if (!context.progress) {
        return;
    }
    try {
        context.progress(completed, total);
    } catch (...) {
        // UI callbacks must never change protocol correctness.
    }
}

enum class ReceiveResult { received, timeout, failed };

ReceiveResult receive_datagram(
    network::NativeSocket socket,
    std::vector<std::byte>& bytes,
    sockaddr_in& sender,
    int& error)
{
    SocketLength sender_length = static_cast<SocketLength>(sizeof(sender));
    const int received = ::recvfrom(
        socket,
        reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0,
        reinterpret_cast<sockaddr*>(&sender),
        &sender_length);

    if (received >= 0) {
        bytes.resize(static_cast<std::size_t>(received));
        return ReceiveResult::received;
    }

    error = last_socket_error();
    return is_timeout_error(error) ? ReceiveResult::timeout : ReceiveResult::failed;
}

} // namespace

class StopAndWaitTransport final : public ReliableTransport {
public:
    explicit StopAndWaitTransport(StopAndWaitOptions options)
        : options_(options), random_(options.fault_seed)
    {
    }

    common::Status send(
        const transfer::TransferContext& context,
        std::span<const std::byte> data) override
    {
        if (auto status = validate_context_and_options(context, options_); !status) {
            return status;
        }

        const auto transfer_id = static_cast<std::uint32_t>(context.transfer_id);
        const std::size_t total_packets = data.empty()
            ? 0
            : 1 + ((data.size() - 1) / options_.payload_size);
        if (total_packets > (std::numeric_limits<std::uint32_t>::max)()) {
            return {common::Error::invalid_argument, "Transfer needs too many RDT packets"};
        }

        network::NativeSocket socket = network::invalid_socket;
        std::unique_ptr<SocketGuard> owned_socket;
        const bool use_bound_socket =
            context.bound_socket && context.bound_socket->valid();
        if (use_bound_socket) {
            socket = context.bound_socket->native_handle();
        } else {
            socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket == network::invalid_socket) {
                return socket_failure("socket");
            }
            owned_socket = std::make_unique<SocketGuard>(socket);
        }
        if (auto status = set_socket_timeout(socket, options_.timeout); !status) {
            return status;
        }

        sockaddr_in peer{};
        if (context.peer_discovery == transfer::PeerDiscovery::await_probe) {
            if (!use_bound_socket) {
                if (!context.local_endpoint) {
                    return {common::Error::invalid_argument,
                            "Await-probe sender requires a local endpoint"};
                }
                if (auto status = bind_udp_socket(
                        socket, context.local_endpoint->port); !status) {
                    return status;
                }
            }

            bool discovered = false;
            for (std::uint32_t attempt = 0;
                 attempt < options_.max_retries && !discovered; ++attempt) {
                Packet hello;
                sockaddr_in sender{};
                int error = 0;
                const auto result = receive_packet(socket, hello, sender, error);
                if (result == ReceiveResult::failed) {
                    return socket_failure("recvfrom");
                }
                discovered = result == ReceiveResult::received &&
                             hello.flags == PacketFlag::hello &&
                             hello.transfer_id == transfer_id;
                if (discovered) {
                    peer = sender;
                }
            }
            if (!discovered) {
                return {common::Error::socket_error,
                        "RDT peer discovery retry limit exceeded"};
            }
        } else {
            if (auto status = make_ipv4_endpoint(*context.endpoint, peer); !status) {
                return status;
            }
        }

        std::uint32_t base = 0;
        std::uint32_t next_sequence = 0;
        std::uint32_t retries = 0;
        const auto packet_count = static_cast<std::uint32_t>(total_packets);

        while (base < packet_count) {
            if (cancelled(context)) {
                return {common::Error::cancelled, "Transfer cancelled"};
            }

            const auto window_end = static_cast<std::uint64_t>(base) + options_.window_size;
            while (next_sequence < packet_count && next_sequence < window_end) {
                const std::size_t offset =
                    static_cast<std::size_t>(next_sequence) * options_.payload_size;
                const std::size_t length =
                    (std::min)(options_.payload_size, data.size() - offset);

                Packet packet;
                packet.transfer_id = transfer_id;
                packet.sequence = next_sequence;
                packet.flags = PacketFlag::data;
                packet.payload.assign(data.begin() + offset, data.begin() + offset + length);
                if (auto status = send_packet(socket, peer, packet); !status) {
                    return status;
                }
                ++next_sequence;
            }

            Packet acknowledgment;
            sockaddr_in sender{};
            int error = 0;
            const auto result = receive_packet(socket, acknowledgment, sender, error);
            if (result == ReceiveResult::failed) {
                return socket_failure("recvfrom");
            }
            if (result == ReceiveResult::timeout) {
                if (++retries >= options_.max_retries) {
                    return {common::Error::socket_error, "RDT data retry limit exceeded"};
                }
                next_sequence = base;
                continue;
            }

            if (!same_peer(sender, peer) ||
                acknowledgment.flags != PacketFlag::ack ||
                acknowledgment.transfer_id != transfer_id ||
                acknowledgment.acknowledgment < base ||
                acknowledgment.acknowledgment >= next_sequence ||
                acknowledgment.acknowledgment >= packet_count) {
                continue;
            }

            base = acknowledgment.acknowledgment + 1;
            retries = 0;
            report_progress(
                context,
                (std::min)(static_cast<std::uint64_t>(data.size()),
                           static_cast<std::uint64_t>(base) *
                               static_cast<std::uint64_t>(options_.payload_size)),
                static_cast<std::uint64_t>(data.size()));
        }

        Packet finish;
        finish.transfer_id = transfer_id;
        finish.sequence = packet_count;
        finish.flags = PacketFlag::finish;

        for (std::uint32_t attempt = 0; attempt < options_.max_retries; ++attempt) {
            if (cancelled(context)) {
                return {common::Error::cancelled, "Transfer cancelled"};
            }
            if (auto status = send_packet(socket, peer, finish); !status) {
                return status;
            }

            Packet acknowledgment;
            sockaddr_in sender{};
            int error = 0;
            const auto result = receive_packet(socket, acknowledgment, sender, error);
            if (result == ReceiveResult::failed) {
                return socket_failure("recvfrom");
            }
            if (result == ReceiveResult::received &&
                same_peer(sender, peer) &&
                acknowledgment.flags == PacketFlag::ack &&
                acknowledgment.transfer_id == transfer_id &&
                acknowledgment.acknowledgment == packet_count) {
                report_progress(context, static_cast<std::uint64_t>(data.size()),
                                static_cast<std::uint64_t>(data.size()));
                return {};
            }
        }

        return {common::Error::socket_error, "RDT finish handshake retry limit exceeded"};
    }

    common::Status receive(
        const transfer::TransferContext& context,
        std::vector<std::byte>& data) override
    {
        if (auto status = validate_context_and_options(context, options_); !status) {
            return status;
        }

        const auto transfer_id = static_cast<std::uint32_t>(context.transfer_id);
        network::NativeSocket socket = network::invalid_socket;
        std::unique_ptr<SocketGuard> owned_socket;
        const bool use_bound_socket =
            context.bound_socket && context.bound_socket->valid();
        if (use_bound_socket) {
            socket = context.bound_socket->native_handle();
        } else {
            socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket == network::invalid_socket) {
                return socket_failure("socket");
            }
            owned_socket = std::make_unique<SocketGuard>(socket);

            int reuse = 1;
            if (::setsockopt(
                    socket,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    reinterpret_cast<const char*>(&reuse),
                    static_cast<SocketLength>(sizeof(reuse))) != 0) {
                return socket_failure("setsockopt(SO_REUSEADDR)");
            }
        }
        if (auto status = set_socket_timeout(socket, options_.timeout); !status) {
            return status;
        }

        if (!use_bound_socket) {
            std::uint16_t local_port = context.endpoint->port;
            if (context.local_endpoint) {
                local_port = context.local_endpoint->port;
            } else if (context.peer_discovery == transfer::PeerDiscovery::send_probe) {
                local_port = 0;
            }
            if (auto status = bind_udp_socket(socket, local_port); !status) {
                return status;
            }
        }

        std::vector<std::byte> received_data;
        std::uint32_t expected_sequence = 0;
        std::uint32_t consecutive_timeouts = 0;
        bool peer_locked = false;
        sockaddr_in peer{};
        if (context.peer_discovery == transfer::PeerDiscovery::send_probe) {
            if (auto status = make_ipv4_endpoint(*context.endpoint, peer); !status) {
                return status;
            }
            Packet hello;
            hello.transfer_id = transfer_id;
            hello.flags = PacketFlag::hello;
            if (auto status = send_packet(socket, peer, hello); !status) {
                return status;
            }
            peer_locked = true;
        }

        while (true) {
            if (cancelled(context)) {
                return {common::Error::cancelled, "Transfer cancelled"};
            }

            Packet packet;
            sockaddr_in sender{};
            int error = 0;
            const auto result = receive_packet(socket, packet, sender, error);
            if (result == ReceiveResult::failed) {
                return socket_failure("recvfrom");
            }
            if (result == ReceiveResult::timeout) {
                if (++consecutive_timeouts >= options_.max_retries) {
                    return {common::Error::socket_error, "RDT receive retry limit exceeded"};
                }
                if (context.peer_discovery == transfer::PeerDiscovery::send_probe) {
                    Packet hello;
                    hello.transfer_id = transfer_id;
                    hello.flags = PacketFlag::hello;
                    if (auto status = send_packet(socket, peer, hello); !status) {
                        return status;
                    }
                }
                continue;
            }
            consecutive_timeouts = 0;

            if (packet.transfer_id != transfer_id) {
                continue;
            }
            if (packet.flags != PacketFlag::data &&
                packet.flags != PacketFlag::finish &&
                packet.flags != PacketFlag::reset) {
                continue;
            }
            if (peer_locked && !same_peer(sender, peer)) {
                continue;
            }
            if (!peer_locked) {
                peer = sender;
                peer_locked = true;
            }

            if (packet.flags == PacketFlag::reset) {
                return {common::Error::cancelled, "Peer reset the RDT transfer"};
            }
            if (packet.flags == PacketFlag::finish) {
                if (packet.sequence != expected_sequence) {
                    if (expected_sequence != 0) {
                        if (auto status = send_ack(
                                socket, peer, transfer_id, expected_sequence - 1); !status) {
                            return status;
                        }
                    }
                    continue;
                }

                if (auto status = send_ack(
                        socket, peer, transfer_id, expected_sequence); !status) {
                    return status;
                }
                finish_grace_period(socket, peer, transfer_id, expected_sequence);
                data = std::move(received_data);
                const auto total = context.expected_size == 0
                    ? static_cast<std::uint64_t>(data.size())
                    : context.expected_size;
                report_progress(context, static_cast<std::uint64_t>(data.size()), total);
                return {};
            }
            if (packet.flags != PacketFlag::data) {
                continue;
            }

            if (packet.sequence == expected_sequence) {
                received_data.insert(
                    received_data.end(), packet.payload.begin(), packet.payload.end());
                report_progress(context,
                                static_cast<std::uint64_t>(received_data.size()),
                                context.expected_size);
                if (auto status = send_ack(
                        socket, peer, transfer_id, expected_sequence); !status) {
                    return status;
                }
                ++expected_sequence;
            } else if (expected_sequence != 0) {
                if (auto status = send_ack(
                        socket, peer, transfer_id, expected_sequence - 1); !status) {
                    return status;
                }
            }
        }
    }

private:
    bool cancelled(const transfer::TransferContext& context) const noexcept
    {
        return context.cancellation != nullptr && context.cancellation->load();
    }

    bool should_drop()
    {
        if (options_.drop_probability == 0.0) {
            return false;
        }
        return std::bernoulli_distribution(options_.drop_probability)(random_);
    }

    common::Status send_packet(
        network::NativeSocket socket,
        const sockaddr_in& peer,
        const Packet& packet)
    {
        std::vector<std::byte> bytes;
        if (auto status = wire_.serialize(packet, bytes); !status) {
            return status;
        }
        if (should_drop()) {
            return {};
        }

        const int sent = ::sendto(
            socket,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            0,
            reinterpret_cast<const sockaddr*>(&peer),
            static_cast<SocketLength>(sizeof(peer)));
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return socket_failure("sendto");
        }
        return {};
    }

    ReceiveResult receive_packet(
        network::NativeSocket socket,
        Packet& packet,
        sockaddr_in& sender,
        int& error)
    {
        std::vector<std::byte> bytes(options_.payload_size + wire_header_size);
        const auto result = receive_datagram(socket, bytes, sender, error);
        if (result != ReceiveResult::received) {
            return result;
        }
        if (auto status = wire_.deserialize(bytes, packet); !status) {
            return ReceiveResult::received;
        }
        return ReceiveResult::received;
    }

    common::Status send_ack(
        network::NativeSocket socket,
        const sockaddr_in& peer,
        std::uint32_t transfer_id,
        std::uint32_t acknowledgment)
    {
        Packet packet;
        packet.transfer_id = transfer_id;
        packet.acknowledgment = acknowledgment;
        packet.flags = PacketFlag::ack;
        return send_packet(socket, peer, packet);
    }

    void finish_grace_period(
        network::NativeSocket socket,
        const sockaddr_in& peer,
        std::uint32_t transfer_id,
        std::uint32_t finish_sequence)
    {
        for (int timeout_count = 0; timeout_count < 2;) {
            Packet packet;
            sockaddr_in sender{};
            int error = 0;
            const auto result = receive_packet(socket, packet, sender, error);
            if (result == ReceiveResult::timeout) {
                ++timeout_count;
                continue;
            }
            if (result == ReceiveResult::failed) {
                return;
            }
            if (same_peer(sender, peer) &&
                packet.transfer_id == transfer_id &&
                packet.flags == PacketFlag::finish &&
                packet.sequence == finish_sequence) {
                static_cast<void>(send_ack(
                    socket, peer, transfer_id, finish_sequence));
            }
        }
    }

    StopAndWaitOptions options_;
    PacketWire wire_;
    std::mt19937 random_;
};

std::unique_ptr<ReliableTransport> create_stop_and_wait_transport(
    StopAndWaitOptions options)
{
    return std::make_unique<StopAndWaitTransport>(std::move(options));
}

} // namespace hftp::rdt
