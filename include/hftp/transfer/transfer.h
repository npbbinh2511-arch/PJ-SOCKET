#ifndef HFTP_TRANSFER_TRANSFER_H
#define HFTP_TRANSFER_TRANSFER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"
#include "hftp/session/session.h"

namespace hftp::transfer {

enum class Direction { upload, download };

enum class Operation {
    store,
    retrieve,
    append,
    store_unique,
    detailed_list,
    name_list,
};

enum class PeerDiscovery {
    none,
    send_probe,
    await_probe,
};

struct TransferContext {
    std::uint64_t transfer_id{};
    Direction direction{Direction::upload};
    Operation operation{Operation::store};
    std::filesystem::path path;
    std::filesystem::path result_name;
    session::TransferType type{session::TransferType::ascii};
    session::DataMode data_mode{session::DataMode::none};
    std::optional<session::UdpEndpoint> endpoint;
    std::optional<session::UdpEndpoint> local_endpoint;
    PeerDiscovery peer_discovery{PeerDiscovery::none};
    // Optional UDP socket already bound by PASV or by the active-mode client.
    // Keeping this lease avoids a close/rebind race between control negotiation
    // and the first data datagram.
    std::shared_ptr<network::Socket> bound_socket;
    std::atomic_bool* cancellation{};
    std::vector<std::byte> payload;
    std::uint64_t expected_size{};
    std::function<void(std::uint64_t, std::uint64_t)> progress;
    bool remove_target_on_failure{false};
};

class TransferCoordinator {
public:
    virtual ~TransferCoordinator() = default;
    [[nodiscard]] virtual common::Status start(const TransferContext& context) = 0;
    virtual void request_cancel(std::uint64_t transfer_id) = 0;

};

} // namespace hftp::transfer

#endif // HFTP_TRANSFER_TRANSFER_H
