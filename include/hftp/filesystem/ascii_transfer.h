#ifndef HFTP_FILESYSTEM_ASCII_TRANSFER_H
#define HFTP_FILESYSTEM_ASCII_TRANSFER_H

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace hftp::filesystem {

// Ma hoa text cuc bo sang Network Virtual Terminal ASCII:
// newline -> CRLF, ky tu CR don -> CR NUL.
[[nodiscard]] std::vector<std::uint8_t> encode_network_ascii(
    std::span<const std::uint8_t> local_bytes);

// Giai ma CRLF ve newline cuc bo va CR NUL ve CR. Mac dinh dung LF de
// ket qua doc lap he dieu hanh; caller tren Windows co the truyen "\r\n".
[[nodiscard]] std::vector<std::uint8_t> decode_network_ascii(
    std::span<const std::uint8_t> network_bytes,
    std::string_view local_newline = "\n");

} // namespace hftp::filesystem

#endif // HFTP_FILESYSTEM_ASCII_TRANSFER_H
