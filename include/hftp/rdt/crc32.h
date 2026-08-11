#ifndef HFTP_RDT_CRC32_H
#define HFTP_RDT_CRC32_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace hftp::rdt {

std::uint32_t crc32(std::span<const std::byte> data);

} // namespace hftp::rdt

#endif // HFTP_RDT_CRC32_H