#include "hftp/rdt/reliable_transport.h"
#include "hftp/rdt/packet.h"
#include "hftp/rdt/crc32.h"
#include "hftp/network/socket.h"

#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace hftp::rdt {

namespace {

void close_native_socket(network::NativeSocket sock) {
    if (sock != network::invalid_socket) {
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
    }
}

void set_socket_timeout(network::NativeSocket sock, std::chrono::milliseconds timeout) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout.count());
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

} // namespace

class StopAndWaitTransport : public ReliableTransport {
private:
    StopAndWaitOptions m_options;
    PacketWire m_wire;

public:
    explicit StopAndWaitTransport(StopAndWaitOptions options = {})
        : m_options(options) {}

    common::Status send(
        const transfer::TransferContext& context, 
        std::span<const std::byte> data) override 
    {
        if (!context.endpoint.has_value()) {
            return {common::Error::invalid_argument, "Missing UDP endpoint context"};
        }

        
        network::NativeSocket sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == network::invalid_socket) {
            return {common::Error::socket_error, "Failed to create UDP socket"};
        }

        set_socket_timeout(sock, m_options.timeout);

        sockaddr_in peer_addr{};
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(context.endpoint->port);
        ::inet_pton(AF_INET, context.endpoint->address.c_str(), &peer_addr.sin_addr);

        std::uint32_t seq_num = 0;
        std::size_t offset = 0;
        const std::size_t total_size = data.size();

        while (offset < total_size || total_size == 0) {
            if (context.cancellation && context.cancellation->load()) {
                close_native_socket(sock);
                return {common::Error::cancelled, "Transfer cancelled by client"};
            }

            std::size_t chunk_size = std::min(m_options.payload_size, total_size - offset);
            
            Packet pkt;
            pkt.flags = PacketFlag::data;
            pkt.transfer_id = static_cast<std::uint32_t>(context.transfer_id);
            pkt.sequence = seq_num;
            pkt.acknowledgment = 0;
            
            if (chunk_size > 0) {
                pkt.payload.assign(data.begin() + offset, data.begin() + offset + chunk_size);
            }
            pkt.checksum = crc32(pkt.payload);

            std::vector<std::byte> wire_bytes;
            if (auto st = m_wire.serialize(pkt, wire_bytes); st.error != common::Error::none) {
                close_native_socket(sock);
                return st;
            }

            std::uint32_t retries = 0;
            bool ack_received = false;

            while (retries < m_options.max_retries && !ack_received) {
                if (context.cancellation && context.cancellation->load()) {
                    close_native_socket(sock);
                    return {common::Error::cancelled, "Transfer cancelled during retry"};
                }

                int sent_bytes = ::sendto(
                    sock, 
                    reinterpret_cast<const char*>(wire_bytes.data()), 
                    static_cast<int>(wire_bytes.size()), 
                    0, 
                    reinterpret_cast<sockaddr*>(&peer_addr), 
                    sizeof(peer_addr)
                );

                if (sent_bytes < 0) {
                    retries++;
                    continue;
                }

                std::vector<std::byte> recv_buf(m_options.payload_size + 64);
                sockaddr_in from_addr{};
                socklen_t from_len = sizeof(from_addr);

                int recv_bytes = ::recvfrom(
                    sock, 
                    reinterpret_cast<char*>(recv_buf.data()), 
                    static_cast<int>(recv_buf.size()), 
                    0, 
                    reinterpret_cast<sockaddr*>(&from_addr), 
                    &from_len
                );

                if (recv_bytes > 0) {
                    recv_buf.resize(static_cast<std::size_t>(recv_bytes));
                    Packet ack_pkt;
                    if (m_wire.deserialize(recv_buf, ack_pkt).error == common::Error::none) {
                        if (ack_pkt.flags == PacketFlag::ack && ack_pkt.acknowledgment == seq_num) {
                            ack_received = true;
                            break;
                        }
                    }
                }

                retries++;
            }

            if (!ack_received) {
                close_native_socket(sock);
                return {common::Error::socket_error, "Retry limit exceeded (Timeout)"};
            }

            offset += chunk_size;
            seq_num++;

            if (total_size == 0) break;
        }

        // Gửi gói FIN báo kết thúc truyền
        Packet fin_pkt;
        fin_pkt.flags = PacketFlag::finish;
        fin_pkt.transfer_id = static_cast<std::uint32_t>(context.transfer_id);
        fin_pkt.sequence = seq_num;
        fin_pkt.checksum = crc32(fin_pkt.payload);

        std::vector<std::byte> fin_bytes;
        if (m_wire.serialize(fin_pkt, fin_bytes).error == common::Error::none) {
            ::sendto(
                sock, 
                reinterpret_cast<const char*>(fin_bytes.data()), 
                static_cast<int>(fin_bytes.size()), 
                0, 
                reinterpret_cast<sockaddr*>(&peer_addr), 
                sizeof(peer_addr)
            );
        }

        close_native_socket(sock);
        return {};
    }

    common::Status receive(
        const transfer::TransferContext& context, 
        std::vector<std::byte>& data) override 
    {
        if (!context.endpoint.has_value()) {
            return {common::Error::invalid_argument, "Missing UDP endpoint context"};
        }

        data.clear();
        
        network::NativeSocket sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == network::invalid_socket) {
            return {common::Error::socket_error, "Failed to create UDP socket"};
        }

        set_socket_timeout(sock, m_options.timeout);

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(context.endpoint->port);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Lắng nghe tất cả card mạng

        if (::bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            close_native_socket(sock);
            return {common::Error::socket_error, "Failed to bind UDP socket"};
        }

        std::uint32_t expected_seq = 0;
        std::uint32_t timeout_count = 0;

        while (true) {
            if (context.cancellation && context.cancellation->load()) {
                close_native_socket(sock);
                return {common::Error::cancelled, "Transfer cancelled by client"};
            }

            std::vector<std::byte> recv_buf(m_options.payload_size + 64);
            sockaddr_in sender_addr{};
            socklen_t sender_len = sizeof(sender_addr);

            int recv_bytes = ::recvfrom(
                sock, 
                reinterpret_cast<char*>(recv_buf.data()), 
                static_cast<int>(recv_buf.size()), 
                0, 
                reinterpret_cast<sockaddr*>(&sender_addr), 
                &sender_len
            );

            if (recv_bytes <= 0) {
                timeout_count++;
                // Thoát nếu quá 10 lần timeout liên tiếp không nhận được gì
                if (timeout_count > 10) {
                    close_native_socket(sock);
                    return {common::Error::socket_error, "Receive timeout"};
                }
                continue;
            }

            timeout_count = 0; // Reset bộ đếm timeout khi có gói tin đến

            recv_buf.resize(static_cast<std::size_t>(recv_bytes));
            Packet pkt;
            if (m_wire.deserialize(recv_buf, pkt).error != common::Error::none) {
                continue;
            }

            if (pkt.flags == PacketFlag::finish) {
                Packet ack_pkt;
                ack_pkt.flags = PacketFlag::ack;
                ack_pkt.transfer_id = pkt.transfer_id;
                ack_pkt.acknowledgment = pkt.sequence;
                
                std::vector<std::byte> ack_bytes;
                if (m_wire.serialize(ack_pkt, ack_bytes).error == common::Error::none) {
                    ::sendto(
                        sock, 
                        reinterpret_cast<const char*>(ack_bytes.data()), 
                        static_cast<int>(ack_bytes.size()), 
                        0, 
                        reinterpret_cast<sockaddr*>(&sender_addr), 
                        sender_len
                    );
                }
                break;
            }

            if (pkt.sequence < expected_seq) {
                Packet ack_pkt;
                ack_pkt.flags = PacketFlag::ack;
                ack_pkt.transfer_id = pkt.transfer_id;
                ack_pkt.acknowledgment = pkt.sequence;

                std::vector<std::byte> ack_bytes;
                if (m_wire.serialize(ack_pkt, ack_bytes).error == common::Error::none) {
                    ::sendto(
                        sock, 
                        reinterpret_cast<const char*>(ack_bytes.data()), 
                        static_cast<int>(ack_bytes.size()), 
                        0, 
                        reinterpret_cast<sockaddr*>(&sender_addr), 
                        sender_len
                    );
                }
                continue;
            }

            if (pkt.sequence == expected_seq) {
                data.insert(data.end(), pkt.payload.begin(), pkt.payload.end());

                Packet ack_pkt;
                ack_pkt.flags = PacketFlag::ack;
                ack_pkt.transfer_id = pkt.transfer_id;
                ack_pkt.acknowledgment = pkt.sequence;

                std::vector<std::byte> ack_bytes;
                if (m_wire.serialize(ack_pkt, ack_bytes).error == common::Error::none) {
                    ::sendto(
                        sock, 
                        reinterpret_cast<const char*>(ack_bytes.data()), 
                        static_cast<int>(ack_bytes.size()), 
                        0, 
                        reinterpret_cast<sockaddr*>(&sender_addr), 
                        sender_len
                    );
                }

                expected_seq++;
            }
        }

        close_native_socket(sock);
        return {};
    }
};

std::unique_ptr<ReliableTransport> create_stop_and_wait_transport(StopAndWaitOptions options) {
    return std::make_unique<StopAndWaitTransport>(options);
}

} // namespace hftp::rdt