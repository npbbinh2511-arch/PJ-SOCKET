#ifndef HFTP_TRANSFER_TRANSFER_H
#define HFTP_TRANSFER_TRANSFER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include "hftp/common/result.h"
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
    std::atomic_bool* cancellation{};
};

class TransferCoordinator {
public:
    virtual ~TransferCoordinator() = default;
    [[nodiscard]] virtual common::Status start(const TransferContext& context) = 0;
    virtual void request_cancel(std::uint64_t transfer_id) = 0;

    // TODO(A/B/C):
    // - Treat TransferContext as an immutable snapshot of validated session settings.
    // - Coordinate repository I/O and RDT without embedding either algorithm here.
    // - Return success/cancel/data-open/file errors for 226/426/425/550 mapping.
    // - Ensure only the matching active transfer observes ABOR cancellation.
    // - Tests: success placeholder, startup failure, cancellation, stale transfer ID.
};

} // namespace hftp::transfer

#endif // HFTP_TRANSFER_TRANSFER_H
