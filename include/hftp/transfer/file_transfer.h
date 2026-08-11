#pragma once
#include "hftp/rdt/reliable_transport.h"
#include "hftp/transfer/transfer.h"
#include <string>
#include <vector>

namespace hftp::transfer {

struct FileTransferResult {
    bool success;
    std::string message;
};

class FileTransferEngine {
public:
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
};

} // namespace hftp::transfer