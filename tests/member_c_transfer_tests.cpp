#include "hftp/network/socket.h"
#include "hftp/transfer/file_transfer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return false; \
        } \
    } while (false)

class TempDirectory {
public:
    TempDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("hftp_c_transfer_" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::uint16_t allocate_udp_port() {
    hftp::network::Socket socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) {
        return 0;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket.native_handle(), reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        return 0;
    }
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (::getsockname(socket.native_handle(), reinterpret_cast<sockaddr*>(&address),
                      &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes,
                 bool append = false) {
    std::ofstream output(path, std::ios::binary |
        (append ? std::ios::app : std::ios::trunc));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

bool transfer_to_engine(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        hftp::transfer::Operation operation,
                        std::uint64_t transfer_id) {
    const auto port = allocate_udp_port();
    TEST_CHECK(port != 0);

    hftp::rdt::StopAndWaitOptions options;
    options.timeout = std::chrono::milliseconds(50);
    options.max_retries = 20;
    options.window_size = 4;

    std::atomic_bool cancelled{false};
    hftp::transfer::TransferContext context;
    context.transfer_id = transfer_id;
    context.direction = hftp::transfer::Direction::upload;
    context.operation = operation;
    context.path = destination;
    context.type = hftp::session::TransferType::ascii;
    context.endpoint = hftp::session::UdpEndpoint{"127.0.0.1", port};
    context.cancellation = &cancelled;

    hftp::transfer::FileTransferEngine receiver(options);
    hftp::common::Status receive_status;
    std::thread receive_thread([&] { receive_status = receiver.start(context); });
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
    const auto send_result = hftp::transfer::FileTransferEngine::send_file_to_client(
        source.string(), context, options);
    receive_thread.join();

    TEST_CHECK(send_result.success);
    TEST_CHECK(receive_status);
    TEST_CHECK(receive_status.message.find("sha256=") != std::string::npos);
    const auto sender_hash = send_result.message.find("sha256=");
    const auto receiver_hash = receive_status.message.find("sha256=");
    TEST_CHECK(sender_hash != std::string::npos);
    TEST_CHECK(receiver_hash != std::string::npos);
    TEST_CHECK(send_result.message.substr(sender_hash) ==
               receive_status.message.substr(receiver_hash));
    return true;
}

bool test_ascii_store_and_append() {
    TempDirectory directory;
    const auto first = directory.path() / "first.txt";
    const auto second = directory.path() / "second.txt";
    const auto stored = directory.path() / "stored.txt";
    write_bytes(first, "line1\r\nline2\rline3\n");
    TEST_CHECK(transfer_to_engine(first, stored,
                                  hftp::transfer::Operation::store, 1001));
    TEST_CHECK(read_bytes(stored) == "line1\nline2\rline3\n");

    write_bytes(second, "tail\n");
    TEST_CHECK(transfer_to_engine(second, stored,
                                  hftp::transfer::Operation::append, 1002));
    TEST_CHECK(read_bytes(stored) == "line1\nline2\rline3\ntail\n");
    return true;
}

} // namespace

int main() {
    hftp::network::SocketRuntime runtime;
    return test_ascii_store_and_append() ? 0 : 1;
}
