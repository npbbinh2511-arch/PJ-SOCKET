#include "hftp/rdt/crc32.h"

namespace hftp::rdt {

std::uint32_t crc32(std::span<const std::byte> data)
{
    std::uint32_t crc = 0xFFFFFFFFu;

    for (std::byte b : data)
    {
        crc ^= static_cast<std::uint8_t>(b);

        for (int i = 0; i < 8; ++i)
        {
            if (crc & 1u)
            {
                crc = (crc >> 1u) ^ 0xEDB88320u;
            }
            else
            {
                crc >>= 1u;
            }
        }
    }

    return ~crc;
}

} // namespace hftp::rdt