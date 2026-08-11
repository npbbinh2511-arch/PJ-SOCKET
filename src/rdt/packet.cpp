#include "hftp/rdt/packet.h"
#include "hftp/rdt/crc32.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace hftp::rdt {
namespace {

constexpr std::uint8_t packet_version = 1;
constexpr std::size_t header_size = 20;
constexpr std::size_t checksum_offset = 16;

void write_u16(std::vector<std::byte>& out, std::size_t pos, std::uint16_t value)
{
    out[pos] = static_cast<std::byte>((value >> 8) & 0xFFU);
    out[pos + 1] = static_cast<std::byte>(value & 0xFFU);
}

void write_u32(std::vector<std::byte>& out, std::size_t pos, std::uint32_t value)
{
    out[pos] = static_cast<std::byte>((value >> 24) & 0xFFU);
    out[pos + 1] = static_cast<std::byte>((value >> 16) & 0xFFU);
    out[pos + 2] = static_cast<std::byte>((value >> 8) & 0xFFU);
    out[pos + 3] = static_cast<std::byte>(value & 0xFFU);
}

std::uint16_t read_u16(std::span<const std::byte> in, std::size_t pos)
{
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(in[pos]) << 8) |
        std::to_integer<std::uint16_t>(in[pos + 1]));
}

std::uint32_t read_u32(std::span<const std::byte> in, std::size_t pos)
{
    return (std::to_integer<std::uint32_t>(in[pos]) << 24) |
           (std::to_integer<std::uint32_t>(in[pos + 1]) << 16) |
           (std::to_integer<std::uint32_t>(in[pos + 2]) << 8) |
           std::to_integer<std::uint32_t>(in[pos + 3]);
}

bool valid_flag(std::uint8_t flag)
{
    return flag == static_cast<std::uint8_t>(PacketFlag::data) ||
           flag == static_cast<std::uint8_t>(PacketFlag::ack) ||
           flag == static_cast<std::uint8_t>(PacketFlag::finish) ||
           flag == static_cast<std::uint8_t>(PacketFlag::reset) ||
           flag == static_cast<std::uint8_t>(PacketFlag::hello);
}

common::Status validate_packet(const Packet& packet)
{
    if (packet.transfer_id == 0) {
        return {common::Error::invalid_argument, "Transfer ID must be non-zero"};
    }
    if (!valid_flag(static_cast<std::uint8_t>(packet.flags))) {
        return {common::Error::invalid_argument, "Invalid packet flag"};
    }
    if (packet.payload.size() > (std::numeric_limits<std::uint16_t>::max)()) {
        return {common::Error::invalid_argument, "Payload exceeds wire length field"};
    }
    if (packet.payload_length != 0 && packet.payload_length != packet.payload.size()) {
        return {common::Error::invalid_argument, "Declared payload length mismatch"};
    }
    if (packet.flags != PacketFlag::data && !packet.payload.empty()) {
        return {common::Error::invalid_argument, "Control packet must not contain payload"};
    }
    return {};
}

std::uint32_t wire_checksum(std::span<const std::byte> bytes)
{
    std::vector<std::byte> protected_bytes;
    protected_bytes.reserve(bytes.size() - sizeof(std::uint32_t));
    protected_bytes.insert(
        protected_bytes.end(), bytes.begin(), bytes.begin() + checksum_offset);
    protected_bytes.insert(
        protected_bytes.end(), bytes.begin() + header_size, bytes.end());
    return crc32(protected_bytes);
}

} // namespace

common::Status PacketWire::serialize(
    const Packet& packet,
    std::vector<std::byte>& bytes) const
{
    if (auto status = validate_packet(packet); !status) {
        return status;
    }

    std::vector<std::byte> encoded(header_size + packet.payload.size());
    encoded[0] = static_cast<std::byte>(packet_version);
    encoded[1] = static_cast<std::byte>(packet.flags);
    write_u32(encoded, 2, packet.transfer_id);
    write_u32(encoded, 6, packet.sequence);
    write_u32(encoded, 10, packet.acknowledgment);
    write_u16(encoded, 14, static_cast<std::uint16_t>(packet.payload.size()));
    write_u32(encoded, checksum_offset, 0);

    if (!packet.payload.empty()) {
        std::memcpy(
            encoded.data() + header_size,
            packet.payload.data(),
            packet.payload.size());
    }
    write_u32(encoded, checksum_offset, wire_checksum(encoded));

    bytes = std::move(encoded);
    return {};
}

common::Status PacketWire::deserialize(
    std::span<const std::byte> bytes,
    Packet& packet) const
{
    if (bytes.size() < header_size) {
        return {common::Error::protocol_error, "Packet too small"};
    }
    if (std::to_integer<std::uint8_t>(bytes[0]) != packet_version) {
        return {common::Error::protocol_error, "Unsupported packet version"};
    }

    const auto flag = std::to_integer<std::uint8_t>(bytes[1]);
    if (!valid_flag(flag)) {
        return {common::Error::protocol_error, "Invalid packet flag"};
    }

    Packet decoded;
    decoded.flags = static_cast<PacketFlag>(flag);
    decoded.transfer_id = read_u32(bytes, 2);
    decoded.sequence = read_u32(bytes, 6);
    decoded.acknowledgment = read_u32(bytes, 10);
    decoded.payload_length = read_u16(bytes, 14);
    decoded.checksum = read_u32(bytes, checksum_offset);

    if (decoded.transfer_id == 0) {
        return {common::Error::protocol_error, "Transfer ID must be non-zero"};
    }
    if (bytes.size() != header_size + decoded.payload_length) {
        return {common::Error::protocol_error, "Payload length mismatch"};
    }
    if (decoded.flags != PacketFlag::data && decoded.payload_length != 0) {
        return {common::Error::protocol_error, "Control packet must not contain payload"};
    }
    if (wire_checksum(bytes) != decoded.checksum) {
        return {common::Error::integrity_error, "CRC mismatch"};
    }

    decoded.payload.resize(decoded.payload_length);
    if (decoded.payload_length != 0) {
        std::memcpy(
            decoded.payload.data(),
            bytes.data() + header_size,
            decoded.payload_length);
    }

    packet = std::move(decoded);
    return {};
}

} // namespace hftp::rdt
