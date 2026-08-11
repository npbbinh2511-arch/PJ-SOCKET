#include "hftp/transfer/file_transfer.h"
#include <fstream>
#include <filesystem>

namespace hftp::transfer {

FileTransferResult FileTransferEngine::send_file_to_client(
    const std::string& file_path, 
    const TransferContext& ctx, 
    const rdt::StopAndWaitOptions& options) 
{
    if (!std::filesystem::exists(file_path)) {
        return {false, "Error: File does not exist."};
    }

    std::ifstream infile(file_path, std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        return {false, "Error: Cannot open file."};
    }

    std::streamsize size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::vector<std::byte> file_data(size);
    if (!infile.read(reinterpret_cast<char*>(file_data.data()), size)) {
        return {false, "Error: Failed to read file."};
    }
    infile.close();

    auto sender = rdt::create_stop_and_wait_transport(options);
    auto status = sender->send(ctx, file_data);

    bool success = (static_cast<int>(status.error) == 0); 
    return {success, status.message};
}

FileTransferResult FileTransferEngine::receive_file_from_client(
    const std::string& save_path, 
    const TransferContext& ctx, 
    const rdt::StopAndWaitOptions& options) 
{
    auto receiver = rdt::create_stop_and_wait_transport(options);
    std::vector<std::byte> file_data;

    auto status = receiver->receive(ctx, file_data);
    bool receive_success = (static_cast<int>(status.error) == 0);
    
    if (!receive_success) {
        return {false, "RDT Receive Error: " + status.message};
    }

    std::ofstream outfile(save_path, std::ios::binary);
    if (!outfile.is_open()) {
        return {false, "Error: Cannot create file."};
    }

    outfile.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
    outfile.close();

    return {true, "File saved."};
}

} // namespace hftp::transfer