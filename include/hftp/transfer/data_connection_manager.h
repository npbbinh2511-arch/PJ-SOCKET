#ifndef HFTP_TRANSFER_DATA_CONNECTION_MANAGER_H
#define HFTP_TRANSFER_DATA_CONNECTION_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"
#include "hftp/session/session.h"

namespace hftp::transfer {

struct PortParseResult { common::Status status; session::UdpEndpoint endpoint; };

class DataConnectionManager {
public:
    explicit DataConnectionManager(std::string passive_address = "127.0.0.1");
    DataConnectionManager(const DataConnectionManager&) = delete;
    DataConnectionManager& operator=(const DataConnectionManager&) = delete;

    [[nodiscard]] PortParseResult parse_port_argument(std::string_view argument) const;
    [[nodiscard]] common::Status set_active(session::Session& session,
                                            std::string_view argument);
    [[nodiscard]] common::Status open_passive(session::Session& session);
    void reset(session::Session& session) noexcept;
    [[nodiscard]] std::shared_ptr<network::Socket> acquire_passive_socket(
        std::uint64_t session_id) const noexcept;
    [[nodiscard]] const std::string& passive_address() const noexcept { return passive_address_; }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<network::Socket>> passive_sockets_;
    std::string passive_address_;
};

} // namespace hftp::transfer

#endif // HFTP_TRANSFER_DATA_CONNECTION_MANAGER_H
