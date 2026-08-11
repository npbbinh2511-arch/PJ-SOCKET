#include "hftp/filesystem/ascii_transfer.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return false; \
        } \
    } while (false)

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& value) {
    return {value.begin(), value.end()};
}

std::string string_of(const std::vector<std::uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

bool test_encode() {
    TEST_CHECK(string_of(hftp::filesystem::encode_network_ascii(bytes_of("a\nb\r\nc"))) ==
               "a\r\nb\r\nc");

    const auto encoded_cr = hftp::filesystem::encode_network_ascii(bytes_of("a\rb"));
    TEST_CHECK(encoded_cr == std::vector<std::uint8_t>({'a', '\r', 0, 'b'}));

    const std::vector<std::uint8_t> binary_like{0x00, 'A', 0xFF};
    TEST_CHECK(hftp::filesystem::encode_network_ascii(binary_like) == binary_like);

    std::cout << "[PASS] Local text to NVT ASCII\n";
    return true;
}

bool test_decode_and_round_trip() {
    TEST_CHECK(string_of(hftp::filesystem::decode_network_ascii(bytes_of("a\r\nb\r\nc"))) ==
               "a\nb\nc");
    TEST_CHECK(string_of(hftp::filesystem::decode_network_ascii(bytes_of("a\r\nb"), "\r\n")) ==
               "a\r\nb");

    const std::vector<std::uint8_t> encoded_cr{'a', '\r', 0, 'b'};
    TEST_CHECK(string_of(hftp::filesystem::decode_network_ascii(encoded_cr)) == "a\rb");

    const auto local = bytes_of("first\nsecond\rboth\r\nlast");
    const auto network = hftp::filesystem::encode_network_ascii(local);
    const auto decoded = hftp::filesystem::decode_network_ascii(network);
    TEST_CHECK(string_of(decoded) == "first\nsecond\rboth\nlast");

    bool empty_newline_rejected = false;
    try {
        (void)hftp::filesystem::decode_network_ascii(network, "");
    } catch (const std::invalid_argument&) {
        empty_newline_rejected = true;
    }
    TEST_CHECK(empty_newline_rejected);

    std::cout << "[PASS] NVT ASCII decode and normalized round trip\n";
    return true;
}

} // namespace

int main() {
    if (!test_encode() || !test_decode_and_round_trip()) {
        return 1;
    }
    std::cout << "==> ALL ASCII TRANSFER TESTS PASSED! <==\n";
    return 0;
}
