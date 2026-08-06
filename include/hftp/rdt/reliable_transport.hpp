#pragma once

#include <chrono>
#include <span>
#include <vector>
#include "hftp/common/result.hpp"
#include "hftp/transfer/transfer.hpp"

namespace hftp::rdt {

struct StopAndWaitOptions {
    std::chrono::milliseconds timeout{500};
    std::uint32_t max_retries{8};
    std::size_t payload_size{1200};
};

class ReliableTransport {
public:
    virtual ~ReliableTransport() = default;
    [[nodiscard]] virtual common::Status send(
        const transfer::TransferContext& context, std::span<const std::byte> data) = 0;
    [[nodiscard]] virtual common::Status receive(
        const transfer::TransferContext& context, std::vector<std::byte>& data) = 0;

    // TODO(A) Basic:
    // - Implement Stop-and-Wait with sequence numbers, ACK, timeout, bounded retry,
    //   duplicate suppression, cancellation checks, and terminal handshake.
    // - Report timeout, malformed peer packet, cancellation, and retry exhaustion.
    // - Tests: happy path, lost ACK, duplicate data, corruption, cancellation.
    // TODO(A) Excellent: add a separate window strategy; do not change this contract.
};

} // namespace hftp::rdt
