#ifndef HFTP_TRANSFER_TRANSFER_H
#define HFTP_TRANSFER_TRANSFER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"
#include "hftp/session/session.h"

namespace hftp::transfer {

enum class Direction { upload, download };

struct TransferContext {
    std::uint64_t transfer_id{};
    Direction direction{Direction::upload};
    std::filesystem::path path;
    session::TransferType type{session::TransferType::ascii};
    session::DataMode data_mode{session::DataMode::none};
    std::optional<session::UdpEndpoint> endpoint;
    network::NativeSocket passive_socket{network::invalid_socket};
    std::atomic_bool* cancellation{};
};

class TransferCoordinator {
public:
    virtual ~TransferCoordinator() = default;
    [[nodiscard]] virtual common::Status start(const TransferContext& context) = 0;
    virtual void request_cancel(std::uint64_t transfer_id) = 0;

};

} // namespace hftp::transfer

#endif // HFTP_TRANSFER_TRANSFER_H
