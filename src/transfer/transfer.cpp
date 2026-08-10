#include "hftp/transfer/transfer.hpp"
#include <mutex>
#include <unordered_map>
#include <iostream>

namespace hftp::transfer {

class StdTransferCoordinator : public TransferCoordinator {
private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, TransferContext> m_active_transfers;

public:
    StdTransferCoordinator() = default;

    common::Status start(const TransferContext& context) override {
        if (context.cancellation && context.cancellation->load()) {
            return common::Status{common::Error::cancelled, "Transfer cancelled before start (426)"};
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active_transfers[context.transfer_id] = context;
        }

        std::cout << "[TRANSFER] Starting transfer ID: " << context.transfer_id << " | Path: " << context.path << " | Dir: " << (context.direction == Direction::upload ? "STOR" : "RETR") << "\n";

        common::Status status{common::Error::none};

        if (context.cancellation && context.cancellation->load()) {
            std::cout << "[TRANSFER] Transfer ID " << context.transfer_id << " interrupted by ABOR\n";
            status = common::Status{common::Error::cancelled, "Transfer aborted by client (426)"};
        } else {
            std::cout << "[TRANSFER] Transfer ID " << context.transfer_id << " completed successfully (226)\n";
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active_transfers.erase(context.transfer_id);
        }

        return status;
    }

    void request_cancel(std::uint64_t transfer_id) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_active_transfers.find(transfer_id);

        if (it != m_active_transfers.end()) {
            if (it->second.cancellation) {
                it->second.cancellation->store(true);
                std::cout << "[TRANSFER] Successfully sent cancellation (ABOR) signal to ID: " << transfer_id << "\n";
            }
        } else {
            std::cerr << "[TRANSFER] Cancel ignored: Stale or non-existent transfer ID: " << transfer_id << "\n";
        }
    }
};

}