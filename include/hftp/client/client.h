#ifndef HFTP_CLIENT_CLIENT_H
#define HFTP_CLIENT_CLIENT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "hftp/common/result.h"
#include "hftp/control/crlf_framer.h"
#include "hftp/network/socket.h"

namespace hftp::client {

class Client {
public:
    [[nodiscard]] common::Status connect(std::string_view host, std::uint16_t port);
    [[nodiscard]] common::Status send_command(std::string_view command);
    [[nodiscard]] common::Status receive_reply(std::vector<std::string>& replies);
    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept { return control_socket_.valid(); }

private:
    network::Socket control_socket_;
    control::CrlfFramer reply_framer_;
};

} // namespace hftp::client

#endif // HFTP_CLIENT_CLIENT_H
