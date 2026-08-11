#include "hftp/rdt/packet.h"
#include "hftp/rdt/crc32.h"

#include <cstddef>
#include <cstring>

namespace hftp::rdt {

namespace {

constexpr std::uint8_t packet_version = 1;
constexpr std::size_t header_size = 20;

void write_u16(std::vector<std::byte>& out, std::size_t pos, std::uint16_t value)
{
    out[pos]     = static_cast<std::byte>((value >> 8) & 0xFF);
    out[pos + 1] = static_cast<std::byte>(value & 0xFF);
}

void write_u32(std::vector<std::byte>& out, std::size_t pos, std::uint32_t value)
{
    out[pos]     = static_cast<std::byte>((value >> 24) & 0xFF);
    out[pos + 1] = static_cast<std::byte>((value >> 16) & 0xFF);
    out[pos + 2] = static_cast<std::byte>((value >> 8) & 0xFF);
    out[pos + 3] = static_cast<std::byte>(value & 0xFF);
}

std::uint16_t read_u16(std::span<const std::byte> in, std::size_t pos)
{
    return (std::to_integer<std::uint16_t>(in[pos]) << 8) |
           std::to_integer<std::uint16_t>(in[pos + 1]);
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
           flag == static_cast<std::uint8_t>(PacketFlag::reset);
}

} // namespace

common::Status PacketWire::serialize(
    const Packet& packet,
    std::vector<std::byte>& bytes) const
{
    bytes.clear();

    const std::uint32_t checksum = crc32(packet.payload);

    bytes.resize(header_size + packet.payload.size());

    bytes[0] = static_cast<std::byte>(packet_version);
    bytes[1] = static_cast<std::byte>(packet.flags);

    write_u32(bytes, 2, packet.transfer_id);
    write_u32(bytes, 6, packet.sequence);
    write_u32(bytes, 10, packet.acknowledgment);

    write_u16(
        bytes,
        14,
        static_cast<std::uint16_t>(packet.payload.size()));

    write_u32(bytes, 16, checksum);

    std::memcpy(
        bytes.data() + header_size,
        packet.payload.data(),
        packet.payload.size());

    return {};
}

common::Status PacketWire::deserialize(
    std::span<const std::byte> bytes,
    Packet& packet) const
{
    if (bytes.size() < header_size)
    {
        return {
            common::Error::protocol_error,
            "Packet too small"
        };
    }

    if (static_cast<std::uint8_t>(bytes[0]) != packet_version)
    {
        return {
            common::Error::protocol_error,
            "Unsupported packet version"
        };
    }

    const auto flag =
        static_cast<std::uint8_t>(bytes[1]);

    if (!valid_flag(flag))
    {
        return {
            common::Error::protocol_error,
            "Invalid packet flag"
        };
    }

    Packet temp;

    temp.flags = static_cast<PacketFlag>(flag);

    temp.transfer_id = read_u32(bytes, 2);
    temp.sequence = read_u32(bytes, 6);
    temp.acknowledgment = read_u32(bytes, 10);

    temp.payload_length = read_u16(bytes, 14);
    temp.checksum = read_u32(bytes, 16);

    if (bytes.size() != header_size + temp.payload_length)
    {
        return {
            common::Error::protocol_error,
            "Payload length mismatch"
        };
    }

    temp.payload.resize(temp.payload_length);

    std::memcpy(
        temp.payload.data(),
        bytes.data() + header_size,
        temp.payload_length);

    const auto computed_crc =
        crc32(temp.payload);

    if (computed_crc != temp.checksum)
    {
        return {
            common::Error::integrity_error,
            "CRC mismatch"
        };
    }

    packet = std::move(temp);

    return {};
}

} // namespace hftp::rdt