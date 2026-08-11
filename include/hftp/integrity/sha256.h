#ifndef HFTP_INTEGRITY_SHA256_H
#define HFTP_INTEGRITY_SHA256_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace hftp::integrity {

class Sha256 {
public:
    Sha256();

    void update(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::array<std::uint8_t, 32> finish();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t total_bytes_{0};
    std::size_t buffered_bytes_{0};
    bool finished_{false};
    std::array<std::uint8_t, 32> digest_{};
};

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);

} // namespace hftp::integrity

#endif // HFTP_INTEGRITY_SHA256_H
