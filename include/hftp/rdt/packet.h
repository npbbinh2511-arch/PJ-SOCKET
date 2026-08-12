#ifndef HFTP_RDT_PACKET_H
#define HFTP_RDT_PACKET_H
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include "hftp/common/result.h"

namespace hftp::rdt {

enum class PacketFlag : std::uint8_t {
    data = 1,
    ack = 2,
    finish = 4,
    reset = 8,
    hello = 16,
};

struct Packet {
    std::uint32_t transfer_id{};
    std::uint32_t sequence{};
    std::uint32_t acknowledgment{};
    PacketFlag flags{PacketFlag::data};
    std::uint16_t payload_length{};
    std::uint32_t checksum{};
    std::vector<std::byte> payload;
};

class PacketWire {
public:
    [[nodiscard]] common::Status serialize(const Packet& packet, std::vector<std::byte>& bytes) const;
    [[nodiscard]] common::Status deserialize(std::span<const std::byte> bytes, Packet& packet) const;
};

} // namespace hftp::rdt

#endif // HFTP_RDT_PACKET_H
