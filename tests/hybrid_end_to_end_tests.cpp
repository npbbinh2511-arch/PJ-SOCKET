#include "hftp/client/client.h"
#include "hftp/client/cli.h"
#include "hftp/client/data_channel.h"
#include "hftp/control/authenticator.h"
#include "hftp/control/command_dispatcher.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/network/socket.h"
#include "hftp/server/server.h"
#include "hftp/transfer/data_connection_manager.h"
#include "hftp/transfer/file_transfer.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
                ("hftp_e2e_" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class Credentials final : public hftp::control::CredentialStore {
public:
    [[nodiscard]] bool verify(std::string_view username,
                              std::string_view password) const override {
        return username == "student" && password == "socket2026";
    }
};

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

std::optional<int> reply_code(std::string_view reply) {
    if (reply.size() < 3) {
        return std::nullopt;
    }
    int code = 0;
    const auto result = std::from_chars(reply.data(), reply.data() + 3, code);
    return result.ec == std::errc{} && result.ptr == reply.data() + 3
        ? std::optional<int>(code) : std::nullopt;
}

bool receive_code(hftp::client::Client& client, int expected,
                  std::string& matched) {
    std::vector<std::string> replies;
    const auto status = client.receive_reply(replies);
    if (!status) {
        std::cerr << "[FAIL] receive reply: " << status.message << '\n';
        return false;
    }
    for (const auto& reply : replies) {
        if (reply_code(reply) == expected) {
            matched = reply;
            return true;
        }
    }
    std::cerr << "[FAIL] expected reply " << expected << '\n';
    return false;
}

bool command(hftp::client::Client& client, std::string_view text, int expected,
             std::string& matched) {
    const auto status = client.send_command(text);
    if (!status) {
        std::cerr << "[FAIL] send command: " << status.message << '\n';
        return false;
    }
    return receive_code(client, expected, matched);
}

std::optional<std::uint64_t> marker(std::string_view reply,
                                    std::string_view name) {
    const auto position = reply.find(name);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const auto begin = position + name.size();
    auto end = begin;
    while (end < reply.size() && reply[end] >= '0' && reply[end] <= '9') {
        ++end;
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(reply.data() + begin,
                                        reply.data() + end, value);
    return result.ec == std::errc{} && result.ptr == reply.data() + end &&
                   end != begin
        ? std::optional<std::uint64_t>(value) : std::nullopt;
}

bool parse_pasv(std::string_view reply,
                hftp::session::UdpEndpoint& endpoint) {
    const auto open = reply.find('(');
    const auto close = reply.find(')', open);
    if (open == std::string_view::npos || close == std::string_view::npos) {
        return false;
    }
    std::array<unsigned int, 6> values{};
    auto field = reply.substr(open + 1, close - open - 1);
    std::size_t begin = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto comma = field.find(',', begin);
        const bool last = index + 1 == values.size();
        if ((!last && comma == std::string_view::npos) ||
            (last && comma != std::string_view::npos)) {
            return false;
        }
        const auto end = last ? field.size() : comma;
        const auto result = std::from_chars(field.data() + begin,
                                            field.data() + end, values[index]);
        if (result.ec != std::errc{} || result.ptr != field.data() + end ||
            values[index] > 255U) {
            return false;
        }
        begin = end + 1;
    }
    endpoint.address = std::to_string(values[0]) + '.' +
                       std::to_string(values[1]) + '.' +
                       std::to_string(values[2]) + '.' +
                       std::to_string(values[3]);
    endpoint.port = static_cast<std::uint16_t>(
        values[4] * 256U + values[5]);
    return endpoint.port != 0;
}

std::pair<std::shared_ptr<hftp::network::Socket>, std::uint16_t>
make_bound_udp_socket() {
    auto socket = std::make_shared<hftp::network::Socket>(
        ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket->valid()) {
        return {std::move(socket), 0};
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket->native_handle(), reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        return {std::move(socket), 0};
    }
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (::getsockname(socket->native_handle(), reinterpret_cast<sockaddr*>(&address),
                      &length) != 0) {
        return {std::move(socket), 0};
    }
    return {std::move(socket), ntohs(address.sin_port)};
}

struct ModeEndpoints {
    hftp::session::DataMode mode{hftp::session::DataMode::none};
    hftp::session::UdpEndpoint passive;
    hftp::session::UdpEndpoint active;
    std::shared_ptr<hftp::network::Socket> bound_socket;
};

bool prepare_mode(hftp::client::Client& client,
                  hftp::session::DataMode mode, ModeEndpoints& endpoints) {
    endpoints = {};
    endpoints.mode = mode;
    std::string reply;
    if (mode == hftp::session::DataMode::passive) {
        return command(client, "PASV", 227, reply) &&
               parse_pasv(reply, endpoints.passive);
    }
    auto [socket, port] = make_bound_udp_socket();
    if (!socket->valid() || port == 0) {
        return false;
    }
    endpoints.bound_socket = std::move(socket);
    endpoints.active = {"127.0.0.1", port};
    const auto port_command = "PORT 127,0,0,1," +
        std::to_string(port / 256U) + ',' + std::to_string(port % 256U);
    return command(client, port_command, 200, reply);
}

bool transfer_file(hftp::client::Client& control,
                   const hftp::client::DataChannelClient& data,
                   hftp::session::DataMode mode, bool upload,
                   const std::filesystem::path& local_path,
                   std::string_view remote_name) {
    ModeEndpoints endpoints;
    TEST_CHECK(prepare_mode(control, mode, endpoints));

    std::string opening;
    const auto wire_command = std::string(upload ? "STOR " : "RETR ") +
                              std::string(remote_name);
    TEST_CHECK(command(control, wire_command, 150, opening));
    const auto transfer_id = marker(opening, "id=");
    TEST_CHECK(transfer_id.has_value());

    hftp::client::DataTransferSpec spec;
    spec.transfer_id = *transfer_id;
    spec.type = hftp::session::TransferType::binary;
    spec.mode = mode;
    spec.server_endpoint = endpoints.passive;
    spec.active_local_endpoint = endpoints.active;
    spec.bound_socket = endpoints.bound_socket;
    spec.expected_size = marker(opening, "bytes=").value_or(0);
    const auto status = upload ? data.upload(local_path, spec)
                               : data.download(local_path, spec);
    TEST_CHECK(status);

    std::string completion;
    TEST_CHECK(receive_code(control, 226, completion));
    TEST_CHECK(hftp::client::verify_transfer_sha256(
        status.message, std::vector<std::string>{completion}));
    return true;
}

bool run_test() {
    TempDirectory directory;
    const auto server_root = directory.path() / "server";
    const auto client_root = directory.path() / "client";
    std::filesystem::create_directories(server_root);
    std::filesystem::create_directories(client_root);

    const std::vector<std::uint8_t> passive_bytes{
        0x00, 0x01, 0x7f, 0x80, 0xff, 'P', 'A', 'S', 'V'};
    const std::vector<std::uint8_t> active_bytes{
        0xff, 0x00, 0x10, 0x20, 0x30, 'P', 'O', 'R', 'T'};
    const auto passive_source = client_root / "passive-source.bin";
    const auto active_source = client_root / "active-source.bin";
    write_file(passive_source, passive_bytes);
    write_file(active_source, active_bytes);

    Credentials credentials;
    hftp::control::Authenticator authenticator(credentials);
    hftp::filesystem::Std_FileRepository repository(server_root);
    hftp::transfer::DataConnectionManager data_connections("127.0.0.1");
    hftp::rdt::StopAndWaitOptions options;
    options.timeout = std::chrono::milliseconds(50);
    options.max_retries = 30;
    options.window_size = 4;
    hftp::transfer::FileTransferEngine transfer_engine(options);
    hftp::control::CommandDispatcher dispatcher(
        authenticator, repository, data_connections, transfer_engine);
    hftp::server::Server server(dispatcher);
    TEST_CHECK(server.start(0));

    hftp::client::Client control;
    TEST_CHECK(control.connect("127.0.0.1", server.control_port()));
    std::string reply;
    TEST_CHECK(receive_code(control, 220, reply));
    TEST_CHECK(command(control, "USER student", 331, reply));
    TEST_CHECK(command(control, "PASS socket2026", 230, reply));
    TEST_CHECK(command(control, "TYPE I", 200, reply));

    hftp::client::DataChannelClient data(options);
    TEST_CHECK(transfer_file(control, data, hftp::session::DataMode::passive,
                             true, passive_source, "passive.bin"));
    TEST_CHECK(read_file(server_root / "passive.bin") == passive_bytes);
    const auto passive_download = client_root / "passive-download.bin";
    TEST_CHECK(transfer_file(control, data, hftp::session::DataMode::passive,
                             false, passive_download, "passive.bin"));
    TEST_CHECK(read_file(passive_download) == passive_bytes);

    TEST_CHECK(transfer_file(control, data, hftp::session::DataMode::active,
                             true, active_source, "active.bin"));
    TEST_CHECK(read_file(server_root / "active.bin") == active_bytes);
    const auto active_download = client_root / "active-download.bin";
    TEST_CHECK(transfer_file(control, data, hftp::session::DataMode::active,
                             false, active_download, "active.bin"));
    TEST_CHECK(read_file(active_download) == active_bytes);

    TEST_CHECK(command(control, "QUIT", 221, reply));
    server.request_stop();
    server.join();
    return true;
}

} // namespace

int main() {
    hftp::network::SocketRuntime runtime;
    return run_test() ? 0 : 1;
}
