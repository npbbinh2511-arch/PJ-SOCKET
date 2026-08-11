#include "hftp/control/authenticator.h"
#include "hftp/control/command_dispatcher.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/logging/logger.h"
#include "hftp/network/socket.h"
#include "hftp/server/server.h"
#include "hftp/transfer/data_connection_manager.h"
#include "hftp/transfer/file_transfer.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

class ConfiguredCredentials final : public hftp::control::CredentialStore {
public:
    ConfiguredCredentials(std::string username, std::string password)
        : username_(std::move(username)), password_(std::move(password)) {}

    bool verify(std::string_view username,
                std::string_view password) const override {
        return username == username_ && password == password_;
    }

private:
    std::string username_;
    std::string password_;
};

bool parse_port(std::string_view text, std::uint16_t& port) {
    unsigned int value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() || value == 0 || value > 65535) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 6) {
        std::cerr << "Usage: hftp_server [port] [root] [username] [password] "
                     "[passive_ipv4]\n";
        return 2;
    }

    std::uint16_t port = 2121;
    if (argc >= 2 && !parse_port(argv[1], port)) {
        std::cerr << "Invalid TCP control port\n";
        return 2;
    }
    const std::filesystem::path root = argc >= 3
        ? std::filesystem::path(argv[2])
        : std::filesystem::current_path();
    const std::string username = argc >= 4 ? argv[3] : "student";
    const std::string password = argc >= 5 ? argv[4] : "hftp";
    const std::string passive_address = argc >= 6 ? argv[5] : "127.0.0.1";

    try {
        hftp::network::SocketRuntime socket_runtime;
        ConfiguredCredentials credentials(username, password);
        hftp::control::Authenticator authenticator(credentials);
        hftp::filesystem::Std_FileRepository repository(root);
        hftp::transfer::DataConnectionManager data_connections(passive_address);
        hftp::transfer::FileTransferEngine transfers;
        hftp::logging::Logger logger(std::cout);
        hftp::control::CommandDispatcher dispatcher(
            authenticator, repository, data_connections, transfers);
        hftp::server::Server server(dispatcher, &logger);

        const auto status = server.start(port);
        if (!status) {
            std::cerr << "Server start failed: " << status.message << '\n';
            return 1;
        }

        std::cout << "Hybrid FTP server listening on TCP port "
                  << server.control_port() << "\nFTP root: "
                  << std::filesystem::absolute(root).string()
                  << "\nLogin: " << username
                  << "\nPress Enter to stop the server.\n";
        std::string line;
        std::getline(std::cin, line);
        server.request_stop();
        server.join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Server error: " << error.what() << '\n';
        return 1;
    }
}
