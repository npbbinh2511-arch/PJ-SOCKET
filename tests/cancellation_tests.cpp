#include "hftp/transfer/file_transfer.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cassert>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#endif

using namespace hftp::transfer;
using namespace hftp;

void create_large_dummy_file(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    // Tạo file khoảng 10MB để truyền đủ lâu cho việc kích hoạt Hủy
    std::string chunk(1024 * 1024, 'A'); 
    for (int i = 0; i < 10; ++i) {
        out.write(chunk.data(), chunk.size());
    }
    out.close();
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
#endif

    std::string in_file = "test_cancel_upload.txt";
    std::string out_file = "test_cancel_download.txt";
    
    if (std::filesystem::exists(out_file)) std::filesystem::remove(out_file);
    create_large_dummy_file(in_file);

    std::cout << "--- BẮT ĐẦU TEST TÍNH NĂNG HỦY TRUYỀN (CANCELLATION) ---" << std::endl;

    std::atomic_bool cancel_flag{false};
    TransferContext ctx; 
    ctx.endpoint = session::UdpEndpoint{"127.0.0.1", 8082};
    ctx.cancellation = &cancel_flag; // Gán cờ hủy vào Context

    // Luồng Receiver (Server)
    std::thread receiver_thread([&]() {
        rdt::StopAndWaitOptions options;
        options.timeout = std::chrono::milliseconds(200);
        auto result = FileTransferEngine::receive_file_from_client(out_file, ctx, options);
        std::cout << ">> Receiver dung truyen voi ket qua: " << result.message << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Luồng Sender (Client)
    bool sender_cancelled_successfully = false;
    std::thread sender_thread([&]() {
        rdt::StopAndWaitOptions options;
        options.timeout = std::chrono::milliseconds(200);
        auto result = FileTransferEngine::send_file_to_client(in_file, ctx, options);
        
        // SỬA LỖI TẠI ĐÂY: Kiểm tra dừng thành công khi nhận cờ Hủy
        if (!result.success) {
            sender_cancelled_successfully = true;
            std::cout << ">> Sender da dung truyen ngay lap tuc khi nhan lenh HUY! Message: " << result.message << std::endl;
        } else {
            std::cout << ">> Sender van truyen thanh cong (khong bi ngat)!" << std::endl;
        }
    });

    // Chờ 50ms cho Sender bắt đầu gửi dữ liệu rồi PHÁT LỆNH HỦY!
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "[ACTION] Kich hoat co HUY (cancellation = true)..." << std::endl;
    cancel_flag.store(true);

    // Đợi 2 luồng kết thúc
    sender_thread.join();
    receiver_thread.join();

    assert(sender_cancelled_successfully && "Loi: Sender khong dung lai khi bi huy!");
    std::cout << ">> SUCCESS: Tinh nang HUY (ABOR) hoat dong chinh xac 100%!" << std::endl;
    std::cout << "--- KẾT THÚC TEST ---" << std::endl;

    std::filesystem::remove(in_file);
    if (std::filesystem::exists(out_file)) std::filesystem::remove(out_file);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}