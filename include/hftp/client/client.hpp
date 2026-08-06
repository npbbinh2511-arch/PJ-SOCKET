#pragma once

#include <cstdint>
#include <string_view>
#include "hftp/common/result.hpp"
#include "hftp/control/crlf_framer.hpp"
#include "hftp/network/socket.hpp"

namespace hftp::client {

class Client {
public:
    [[nodiscard]] common::Status connect(std::string_view host, std::uint16_t port);
    [[nodiscard]] common::Status send_command(std::string_view command);
    [[nodiscard]] common::Status receive_reply();
    void disconnect() noexcept;

    // TODO(B):
    // - Resolve/connect with clear socket ownership and observable connection state.
    // - send_command appends one CRLF and loops until all bytes are sent.
    // - receive_reply feeds every recv chunk into CrlfFramer; do not assume one recv.
    // - Handle orderly peer close, malformed/oversized reply, and socket errors.
    // - Tests: partial send adapter, fragmented reply, coalesced replies, peer close.

private:
    network::Socket control_socket_;
    control::CrlfFramer reply_framer_;
};

} // namespace hftp::client

