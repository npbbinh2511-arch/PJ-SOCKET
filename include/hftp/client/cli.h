#ifndef HFTP_CLIENT_CLI_H
#define HFTP_CLIENT_CLI_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hftp/common/result.h"

namespace hftp::client {

[[nodiscard]] common::Status tokenize_cli_line(
    std::string_view line, std::vector<std::string>& tokens);
[[nodiscard]] common::Status verify_transfer_sha256(
    std::string_view local_completion,
    const std::vector<std::string>& server_replies);
int run_cli(std::string_view host, std::uint16_t port);

} // namespace hftp::client

#endif // HFTP_CLIENT_CLI_H
