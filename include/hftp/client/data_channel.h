#ifndef HFTP_CLIENT_DATA_CHANNEL_H
#define HFTP_CLIENT_DATA_CHANNEL_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "hftp/common/result.h"
#include "hftp/rdt/reliable_transport.h"
#include "hftp/session/session.h"
#include "hftp/transfer/transfer.h"

namespace hftp::client {

struct DataTransferSpec {
    std::uint64_t transfer_id{};
    session::TransferType type{session::TransferType::ascii};
    session::DataMode mode{session::DataMode::none};
    session::UdpEndpoint server_endpoint;
    session::UdpEndpoint active_local_endpoint;
    std::shared_ptr<network::Socket> bound_socket;
    std::uint64_t expected_size{};
    std::atomic_bool* cancellation{};
    std::function<void(std::uint64_t, std::uint64_t)> progress;
};

class DataChannelClient {
public:
    explicit DataChannelClient(rdt::StopAndWaitOptions options = {})
        : options_(options) {}

    [[nodiscard]] common::Status upload(
        const std::filesystem::path& local_path,
        const DataTransferSpec& spec) const;
    [[nodiscard]] common::Status download(
        const std::filesystem::path& local_path,
        const DataTransferSpec& spec) const;
    [[nodiscard]] common::Status receive_listing(
        std::string& listing,
        const DataTransferSpec& spec) const;

private:
    [[nodiscard]] common::Status make_context(
        const DataTransferSpec& spec, transfer::Direction direction,
        transfer::TransferContext& context) const;

    rdt::StopAndWaitOptions options_;
};

} // namespace hftp::client

#endif // HFTP_CLIENT_DATA_CHANNEL_H
