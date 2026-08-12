#include "hftp/filesystem/ascii_transfer.h"

#include <stdexcept>

namespace hftp::filesystem {

std::vector<std::uint8_t> encode_network_ascii(
    std::span<const std::uint8_t> local_bytes) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(local_bytes.size() + local_bytes.size() / 16);

    for (std::size_t index = 0; index < local_bytes.size(); ++index) {
        const auto byte = local_bytes[index];
        if (byte == '\r') {
            if (index + 1 < local_bytes.size() && local_bytes[index + 1] == '\n') {
                ++index;
                encoded.push_back('\r');
                encoded.push_back('\n');
            } else {
                encoded.push_back('\r');
                encoded.push_back(0);
            }
        } else if (byte == '\n') {
            encoded.push_back('\r');
            encoded.push_back('\n');
        } else {
            encoded.push_back(byte);
        }
    }
    return encoded;
}

std::vector<std::uint8_t> decode_network_ascii(
    std::span<const std::uint8_t> network_bytes,
    std::string_view local_newline) {
    if (local_newline.empty()) {
        throw std::invalid_argument("Local newline must not be empty");
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve(network_bytes.size());

    for (std::size_t index = 0; index < network_bytes.size(); ++index) {
        const auto byte = network_bytes[index];
        if (byte != '\r' || index + 1 >= network_bytes.size()) {
            decoded.push_back(byte);
            continue;
        }

        const auto next = network_bytes[index + 1];
        if (next == '\n') {
            ++index;
            decoded.insert(decoded.end(), local_newline.begin(), local_newline.end());
        } else if (next == 0) {
            ++index;
            decoded.push_back('\r');
        } else {
            // Du lieu NVT khong chuan: bao toan CR thay vi lam mat byte.
            decoded.push_back('\r');
        }
    }
    return decoded;
}

} // namespace hftp::filesystem
