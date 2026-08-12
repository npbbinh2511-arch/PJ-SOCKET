#ifndef HFTP_RDT_RELIABLE_TRANSPORT_H
#define HFTP_RDT_RELIABLE_TRANSPORT_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "hftp/common/result.h"
#include "hftp/transfer/transfer.h"

namespace hftp::rdt {

struct StopAndWaitOptions {
    std::chrono::milliseconds timeout{500};
    std::uint32_t max_retries{8};
    std::size_t payload_size{1200};
    double drop_probability{0.0};
    std::size_t window_size{4};
    std::uint32_t fault_seed{0xC0FFEEU};
};

class ReliableTransport {
public:
    virtual ~ReliableTransport() = default;
    [[nodiscard]] virtual common::Status send(
        const transfer::TransferContext& context, std::span<const std::byte> data) = 0;
    [[nodiscard]] virtual common::Status receive(
        const transfer::TransferContext& context, std::vector<std::byte>& data) = 0;
};

std::unique_ptr<ReliableTransport> create_stop_and_wait_transport(
    StopAndWaitOptions options = {});

} // namespace hftp::rdt

#endif // HFTP_RDT_RELIABLE_TRANSPORT_H
