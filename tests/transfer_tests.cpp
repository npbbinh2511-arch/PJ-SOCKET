#include "hftp/transfer/file_transfer.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cassert>

// Thêm thư viện mạng của Windows
#ifdef _WIN32
#include <winsock2.h>
#endif

using namespace hftp::transfer;
using namespace hftp;

void create_dummy_file(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << "TESTING FILE TRANSFER ENGINE\n";
    out << "Project: PJ-SOCKET\n";
    out << "Module: STOR / RETR over UDP (Stop-and-Wait RDT)\n";
    out.close();
}

int main() {
    // --- BẬT CÔNG TẮC MẠNG TRÊN WINDOWS ---
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Khong the khoi tao Winsock!" << std::endl;
        return 1;
    }
#endif
    // ---------------------------------------

    std::string in_file = "test_upload.txt";
    std::string out_file = "test_download.txt";
    
    if (std::filesystem::exists(out_file)) std::filesystem::remove(out_file);
    create_dummy_file(in_file);

    std::cout << "--- BẮT ĐẦU TEST TRUYỀN FILE ---" << std::endl;

    TransferContext ctx; 
    ctx.endpoint = session::UdpEndpoint{"127.0.0.1", 8081};

    std::thread receiver_thread([&]() {
        rdt::StopAndWaitOptions options;
        auto result = FileTransferEngine::receive_file_from_client(out_file, ctx, options);
        
        if (!result.success) {
            std::cerr << "\n[RECEIVER LỖI] Chi tiết: " << result.message << std::endl;
        }
        assert(result.success && "Lỗi: Server nhận file thất bại!");
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    rdt::StopAndWaitOptions options;

    options.drop_probability = 0.2;
    
    auto result = FileTransferEngine::send_file_to_client(in_file, ctx, options);
    
    if (!result.success) {
        std::cerr << "\n[SENDER LỖI] Chi tiết: " << result.message << std::endl;
    }
    assert(result.success && "Lỗi: Client gửi file thất bại!");

    receiver_thread.join();

    assert(std::filesystem::exists(out_file));
    auto in_size = std::filesystem::file_size(in_file);
    auto out_size = std::filesystem::file_size(out_file);
    assert(in_size == out_size);

    std::cout << ">> SUCCESS: File duoc truyen nguyen ven! (" << out_size << " bytes)" << std::endl;
    std::cout << "--- KẾT THÚC TEST ---" << std::endl;

    std::filesystem::remove(in_file);
    std::filesystem::remove(out_file);

    // --- TẮT CÔNG TẮC MẠNG TRÊN WINDOWS ---
#ifdef _WIN32
    WSACleanup();
#endif
    // ---------------------------------------

    return 0;
}