#ifndef HFTP_TRANSFER_FILE_TRANSFER_H
#define HFTP_TRANSFER_FILE_TRANSFER_H

#include "hftp/rdt/reliable_transport.h"
#include "hftp/transfer/transfer.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hftp::transfer {

struct FileTransferResult {
    bool success;
    std::string message;
};

class FileTransferEngine final : public TransferCoordinator {
public:
    explicit FileTransferEngine(rdt::StopAndWaitOptions options = {})
        : options_(options) {}

    [[nodiscard]] common::Status start(const TransferContext& context) override;
    void request_cancel(std::uint64_t transfer_id) override;

    // Xử lý lệnh RETR (Download)
    static FileTransferResult send_file_to_client(
        const std::string& file_path, 
        const TransferContext& ctx, 
        const rdt::StopAndWaitOptions& options = {}
    );

    // Xử lý lệnh STOR (Upload)
    static FileTransferResult receive_file_from_client(
        const std::string& save_path, 
        const TransferContext& ctx, 
        const rdt::StopAndWaitOptions& options = {}
    );

private:
    rdt::StopAndWaitOptions options_;
    std::mutex active_mutex_;
    std::unordered_map<std::uint64_t, std::atomic_bool*> active_cancellations_;
};

} // namespace hftp::transfer

#endif // HFTP_TRANSFER_FILE_TRANSFER_H
