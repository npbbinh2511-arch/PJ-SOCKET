#include "hftp/transfer/file_transfer.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cassert>

#ifdef _WIN32
#include <winsock2.h>
#endif

using namespace hftp::transfer;
using namespace hftp;

void create_dummy_file(const std::string& path, std::size_t size_mb) {
    std::ofstream out(path, std::ios::binary);
    std::string chunk(1024 * 1024, 'X'); 
    for (std::size_t i = 0; i < size_mb; ++i) {
        out.write(chunk.data(), chunk.size());
    }
    out.close();
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
#endif

    std::string in_file = "test_gbn_upload.txt";
    std::string out_file = "test_gbn_download.txt";
    
    if (std::filesystem::exists(out_file)) std::filesystem::remove(out_file);
    create_dummy_file(in_file, 2); // Tạo file dung lượng 2MB

    std::cout << "--- BAT DAU TEST SLIDING WINDOW (GO-BACK-N / PIPELINING) ---" << std::endl;

    TransferContext ctx; 
    ctx.endpoint = session::UdpEndpoint{"127.0.0.1", 8083};

    auto start_time = std::chrono::high_resolution_clock::now();

    // Luồng Receiver (Server)
    std::thread receiver_thread([&]() {
        rdt::StopAndWaitOptions options;
        auto result = FileTransferEngine::receive_file_from_client(out_file, ctx, options);
        assert(result.success && "Loi: Receiver nhan file that bai!");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Cấu hình Sender với Sliding Window size = 8
    rdt::StopAndWaitOptions options;
    options.window_size = 8;         // Kích thước Cửa sổ trượt: 8 gói tin
    options.drop_probability = 0.05; // Giả lập rớt 5% gói tin để kiểm tra khôi phục lỗi theo Window

    auto result = FileTransferEngine::send_file_to_client(in_file, ctx, options);
    assert(result.success && "Loi: Sender gui file that bai!");

    receiver_thread.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    // Kiểm tra tính toàn vẹn file sau khi truyền
    assert(std::filesystem::exists(out_file));
    auto in_size = std::filesystem::file_size(in_file);
    auto out_size = std::filesystem::file_size(out_file);
    assert(in_size == out_size);

    std::cout << ">> SUCCESS: Truyen file " << (out_size / (1024 * 1024)) 
              << " MB thanh cong trong " << diff.count() << " giay!" << std::endl;
    std::cout << "--- KET THUC TEST ---" << std::endl;

    std::filesystem::remove(in_file);
    std::filesystem::remove(out_file);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}