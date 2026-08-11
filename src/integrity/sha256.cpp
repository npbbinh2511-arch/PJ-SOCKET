#include "hftp/integrity/sha256.h"

#include <algorithm>
#include <stdexcept>

namespace hftp::integrity {

namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

std::uint32_t read_big_endian(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = read_big_endian(block + index * 4);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = rotate_right(words[index - 15], 7) ^
                        rotate_right(words[index - 15], 18) ^
                        (words[index - 15] >> 3U);
        const auto s1 = rotate_right(words[index - 2], 17) ^
                        rotate_right(words[index - 2], 19) ^
                        (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto upper_sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const auto choose = (e & f) ^ ((~e) & g);
        const auto temporary1 = h + upper_sigma1 + choose + round_constants[index] + words[index];
        const auto upper_sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = upper_sigma0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) {
    if (finished_) {
        throw std::logic_error("SHA-256 context is already finalized");
    }
    if (bytes.size() > UINT64_MAX - total_bytes_) {
        throw std::length_error("SHA-256 input is too large");
    }
    total_bytes_ += static_cast<std::uint64_t>(bytes.size());

    std::size_t offset = 0;
    if (buffered_bytes_ > 0) {
        const auto count = std::min(buffer_.size() - buffered_bytes_, bytes.size());
        std::copy_n(bytes.data(), count, buffer_.data() + buffered_bytes_);
        buffered_bytes_ += count;
        offset += count;
        if (buffered_bytes_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_bytes_ = 0;
        }
    }

    while (bytes.size() - offset >= buffer_.size()) {
        transform(bytes.data() + offset);
        offset += buffer_.size();
    }

    if (offset < bytes.size()) {
        buffered_bytes_ = bytes.size() - offset;
        std::copy_n(bytes.data() + offset, buffered_bytes_, buffer_.data());
    }
}

std::array<std::uint8_t, 32> Sha256::finish() {
    if (finished_) {
        return digest_;
    }

    const std::uint64_t message_bits = total_bytes_ * 8U;
    const std::array<std::uint8_t, 1> marker{0x80U};
    update(marker);

    const std::array<std::uint8_t, 64> zeros{};
    const std::size_t zero_count = buffered_bytes_ <= 56
        ? 56 - buffered_bytes_
        : 64 + 56 - buffered_bytes_;
    if (zero_count > 0) {
        update(std::span<const std::uint8_t>(zeros.data(), zero_count));
    }

    std::array<std::uint8_t, 8> encoded_length{};
    for (std::size_t index = 0; index < encoded_length.size(); ++index) {
        encoded_length[encoded_length.size() - 1 - index] =
            static_cast<std::uint8_t>(message_bits >> (index * 8U));
    }
    update(encoded_length);

    for (std::size_t word = 0; word < state_.size(); ++word) {
        digest_[word * 4] = static_cast<std::uint8_t>(state_[word] >> 24U);
        digest_[word * 4 + 1] = static_cast<std::uint8_t>(state_[word] >> 16U);
        digest_[word * 4 + 2] = static_cast<std::uint8_t>(state_[word] >> 8U);
        digest_[word * 4 + 3] = static_cast<std::uint8_t>(state_[word]);
    }
    finished_ = true;
    return digest_;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    Sha256 context;
    context.update(bytes);
    return context.finish();
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    const auto digest = sha256(bytes);
    return to_hex(digest);
}

} // namespace hftp::integrity
