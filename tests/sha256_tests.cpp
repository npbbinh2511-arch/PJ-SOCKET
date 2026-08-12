#include "hftp/integrity/sha256.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_CHECK(condition)                                              \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__          \
                      << " -> " #condition "\n";                            \
            return false;                                                  \
        }                                                                  \
    } while (false)

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& value) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

std::span<const std::uint8_t> as_span(
    const std::vector<std::uint8_t>& bytes) {

    return std::span<const std::uint8_t>(
        bytes.data(),
        bytes.size()
    );
}

bool test_known_vectors() {
    const std::vector<std::uint8_t> empty;

    TEST_CHECK(
        hftp::integrity::sha256_hex(as_span(empty)) ==
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855"
    );

    const auto abc = bytes_of("abc");

    TEST_CHECK(
        hftp::integrity::sha256_hex(as_span(abc)) ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"
    );

    const auto long_vector = bytes_of(
        "abcdbcdecdefdefgefghfghighijhijk"
        "ijkljklmklmnlmnomnopnopq"
    );

    TEST_CHECK(
        hftp::integrity::sha256_hex(as_span(long_vector)) ==
        "248d6a61d20638b8e5c026930c3e6039"
        "a33ce45964ff2167f6ecedd419db06c1"
    );

    std::cout << "[PASS] SHA-256 official known vectors\n";
    return true;
}

bool test_incremental_and_large_input() {
    hftp::integrity::Sha256 incremental;

    const auto abc = bytes_of("abc");

    for (const std::uint8_t character : abc) {
        incremental.update(
            std::span<const std::uint8_t>(&character, 1)
        );
    }

    const auto first_digest = incremental.finish();

    TEST_CHECK(
        hftp::integrity::to_hex(
            std::span<const std::uint8_t>(
                first_digest.data(),
                first_digest.size()
            )
        ) ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"
    );

    TEST_CHECK(incremental.finish() == first_digest);

    bool update_after_finish_rejected = false;

    try {
        const auto extra = bytes_of("x");
        incremental.update(as_span(extra));
    } catch (const std::logic_error&) {
        update_after_finish_rejected = true;
    }

    TEST_CHECK(update_after_finish_rejected);

    const std::vector<std::uint8_t> million_a(
        1'000'000,
        static_cast<std::uint8_t>('a')
    );

    TEST_CHECK(
        hftp::integrity::sha256_hex(as_span(million_a)) ==
        "cdc76e5c9914fb9281a1c7e284d73e67"
        "f1809a48a497200e046d39ccc7112cd0"
    );

    std::cout
        << "[PASS] Incremental, idempotent finish "
        << "and one-million-byte vector\n";

    return true;
}

} // namespace

int main() {
    const bool passed =
        test_known_vectors() &&
        test_incremental_and_large_input();

    if (!passed) {
        return 1;
    }

    std::cout << "==> ALL SHA-256 TESTS PASSED! <==\n";
    return 0;
}