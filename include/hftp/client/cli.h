#ifndef HFTP_CLIENT_CLI_H
#define HFTP_CLIENT_CLI_H

#include <cstdint>
#include <string_view>

namespace hftp::client {

int run_cli(std::string_view host, std::uint16_t port);

} // namespace hftp::client

#endif // HFTP_CLIENT_CLI_H
