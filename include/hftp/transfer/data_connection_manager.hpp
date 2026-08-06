#pragma once

#include <cstdint>
#include <string_view>
#include "hftp/common/result.hpp"
#include "hftp/session/session.hpp"

namespace hftp::transfer {

struct PortParseResult { common::Status status; session::UdpEndpoint endpoint; };

class DataConnectionManager {
public:
    [[nodiscard]] PortParseResult parse_port_argument(std::string_view argument) const;
    [[nodiscard]] common::Status open_passive(session::Session& session);
    void reset(session::Session& session) noexcept;

    // TODO(B):
    // - PORT parses exactly six decimal octets in [0,255], computes p1*256+p2,
    //   rejects port zero/unsafe endpoints, and does not mutate Session on failure.
    // - PASV obtains and owns a bounded-lifetime UDP socket/port before committing state.
    // - reset closes passive resources and clears active/passive session state safely.
    // - Tests: malformed fields, boundaries, forbidden address, PASV/reset lifecycle.
};

} // namespace hftp::transfer
