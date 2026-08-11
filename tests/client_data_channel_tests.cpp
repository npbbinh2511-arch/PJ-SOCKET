#include "hftp/client/data_channel.h"
#include "hftp/network/socket.h"
#include "hftp/transfer/file_transfer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
                ("hftp_client_data_" + std::to_string(unique));
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

std::pair<std::shared_ptr<hftp::network::Socket>, std::uint16_t>
make_bound_socket() {
    auto socket = std::make_shared<hftp::network::Socket>(
        ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket->valid()) {
        return {socket, 0};
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket->native_handle(), reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        return {socket, 0};
    }
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (::getsockname(socket->native_handle(), reinterpret_cast<sockaddr*>(&address),
                      &length) != 0) {
        return {socket, 0};
    }
    return {socket, ntohs(address.sin_port)};
}

void write_file(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

bool run_file_transfer(hftp::session::DataMode mode,
                       hftp::transfer::Direction direction,
                       std::uint64_t transfer_id) {
    TempDirectory directory;
    const std::vector<std::uint8_t> expected{
        0x00, 0x01, 0x7f, 0x80, 0xff, 'H', 'F', 'T', 'P'};
    const auto source = directory.path() / "source.bin";
    const auto destination = directory.path() / "destination.bin";
    write_file(source, expected);

    hftp::rdt::StopAndWaitOptions options;
    options.timeout = std::chrono::milliseconds(50);
    options.max_retries = 30;
    options.window_size = 4;

    std::shared_ptr<hftp::network::Socket> bound_socket;
    std::uint16_t passive_port = 0;
    if (mode == hftp::session::DataMode::passive) {
        auto bound = make_bound_socket();
        bound_socket = std::move(bound.first);
        passive_port = bound.second;
        TEST_CHECK(bound_socket->valid() && passive_port != 0);
    }
    std::uint16_t active_port = 0;
    if (mode == hftp::session::DataMode::active) {
        auto bound = make_bound_socket();
        bound_socket = std::move(bound.first);
        active_port = bound.second;
    }
    TEST_CHECK(mode != hftp::session::DataMode::active || active_port != 0);

    const hftp::session::UdpEndpoint passive_endpoint{"127.0.0.1", passive_port};
    const hftp::session::UdpEndpoint active_endpoint{"127.0.0.1", active_port};

    std::atomic_bool cancelled{false};
    hftp::transfer::TransferContext server_context;
    server_context.transfer_id = transfer_id;
    server_context.direction = direction;
    server_context.operation = direction == hftp::transfer::Direction::upload
        ? hftp::transfer::Operation::store
        : hftp::transfer::Operation::retrieve;
    server_context.path = direction == hftp::transfer::Direction::upload
        ? destination : source;
    server_context.type = hftp::session::TransferType::binary;
    server_context.data_mode = mode;
    server_context.endpoint = mode == hftp::session::DataMode::passive
        ? passive_endpoint : active_endpoint;
    server_context.bound_socket = mode == hftp::session::DataMode::passive
        ? bound_socket : nullptr;
    server_context.cancellation = &cancelled;
    if (mode == hftp::session::DataMode::passive &&
        direction == hftp::transfer::Direction::download) {
        server_context.peer_discovery = hftp::transfer::PeerDiscovery::await_probe;
    }
    if (mode == hftp::session::DataMode::active &&
        direction == hftp::transfer::Direction::upload) {
        server_context.peer_discovery = hftp::transfer::PeerDiscovery::send_probe;
        server_context.local_endpoint = hftp::session::UdpEndpoint{"0.0.0.0", 0};
    }

    hftp::client::DataTransferSpec client_spec;
    client_spec.transfer_id = transfer_id;
    client_spec.type = hftp::session::TransferType::binary;
    client_spec.mode = mode;
    client_spec.server_endpoint = passive_endpoint;
    client_spec.active_local_endpoint = active_endpoint;
    client_spec.bound_socket = mode == hftp::session::DataMode::active
        ? bound_socket : nullptr;
    client_spec.expected_size = expected.size();

    hftp::transfer::FileTransferEngine server_engine(options);
    hftp::common::Status server_status;
    std::thread server([&] { server_status = server_engine.start(server_context); });
    std::this_thread::sleep_for(std::chrono::milliseconds(75));

    hftp::client::DataChannelClient client(options);
    const auto client_status = direction == hftp::transfer::Direction::upload
        ? client.upload(source, client_spec)
        : client.download(destination, client_spec);
    server.join();

    TEST_CHECK(client_status);
    TEST_CHECK(server_status);
    TEST_CHECK(read_file(destination) == expected);
    return true;
}

} // namespace

int main() {
    hftp::network::SocketRuntime runtime;
    TEST_CHECK(run_file_transfer(hftp::session::DataMode::passive,
                                 hftp::transfer::Direction::upload, 2001));
    TEST_CHECK(run_file_transfer(hftp::session::DataMode::passive,
                                 hftp::transfer::Direction::download, 2002));
    TEST_CHECK(run_file_transfer(hftp::session::DataMode::active,
                                 hftp::transfer::Direction::upload, 2003));
    TEST_CHECK(run_file_transfer(hftp::session::DataMode::active,
                                 hftp::transfer::Direction::download, 2004));
    return 0;
}
