#include "hftp/client/cli.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool parse_port(std::string_view text, std::uint16_t& port) {
    unsigned int value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() || value == 0 || value > 65535) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 3) {
        std::cerr << "Usage: hftp_client [host] [port]\n";
        return 2;
    }
    const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
    std::uint16_t port = 2121;
    if (argc >= 3 && !parse_port(argv[2], port)) {
        std::cerr << "Invalid TCP control port\n";
        return 2;
    }

    return hftp::client::run_cli(host, port);
}
