#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "hftp/common/result.hpp"

namespace hftp::rdt {

enum class PacketFlag : std::uint8_t { data = 1, ack = 2, finish = 4, reset = 8 };

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

    // TODO(A):
    // - Define a versioned byte layout and serialize each fixed-width field explicitly.
    // - Convert multi-byte integers to/from network order; never send the C++ struct.
    // - Validate flags, declared length, datagram size, transfer ID, and checksum.
    // - Leave output unchanged on malformed input.
    // - Tests: golden bytes, truncation, excess payload, bad flag, bad checksum.
};

} // namespace hftp::rdt

