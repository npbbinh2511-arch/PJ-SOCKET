#include "hftp/client/cli.h"

#include "hftp/client/client.h"
#include "hftp/client/data_channel.h"
#include "hftp/network/socket.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace hftp::client {
namespace {

struct CliState {
    session::DataMode preference{session::DataMode::passive};
    session::DataMode prepared{session::DataMode::none};
    session::TransferType type{session::TransferType::ascii};
    session::UdpEndpoint passive_endpoint;
    session::UdpEndpoint active_endpoint;
    std::shared_ptr<network::Socket> active_socket;
    std::string active_address;
};

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return value;
}

std::vector<std::string> tokenize(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::vector<std::string> tokens;
    std::string token;
    while (input >> std::quoted(token)) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::optional<int> reply_code(std::string_view reply) {
    if (reply.size() < 3 || !std::isdigit(static_cast<unsigned char>(reply[0])) ||
        !std::isdigit(static_cast<unsigned char>(reply[1])) ||
        !std::isdigit(static_cast<unsigned char>(reply[2]))) {
        return std::nullopt;
    }
    return (reply[0] - '0') * 100 + (reply[1] - '0') * 10 + (reply[2] - '0');
}

void print_replies(const std::vector<std::string>& replies) {
    for (const auto& reply : replies) {
        std::cout << "< " << reply << '\n';
    }
}

common::Status send_and_receive(Client& client, std::string_view command,
                                std::vector<std::string>& replies) {
    auto status = client.send_command(command);
    if (!status) {
        return status;
    }
    status = client.receive_reply(replies);
    if (status) {
        print_replies(replies);
    }
    return status;
}

bool has_code(const std::vector<std::string>& replies, int expected) {
    return std::any_of(replies.begin(), replies.end(), [expected](const auto& reply) {
        return reply_code(reply) == expected;
    });
}

std::optional<std::uint64_t> marker_value(std::string_view text,
                                          std::string_view marker) {
    const auto position = text.find(marker);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const auto begin = position + marker.size();
    auto end = begin;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data() + begin, text.data() + end, value);
    return result.ec == std::errc{} && result.ptr == text.data() + end && end > begin
        ? std::optional<std::uint64_t>(value) : std::nullopt;
}

bool parse_six_fields(std::string_view argument,
                      std::array<unsigned int, 6>& fields) {
    std::size_t start = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto comma = argument.find(',', start);
        const bool last = index + 1 == fields.size();
        if ((!last && comma == std::string_view::npos) ||
            (last && comma != std::string_view::npos)) {
            return false;
        }
        const auto end = last ? argument.size() : comma;
        unsigned int value = 0;
        const auto result = std::from_chars(argument.data() + start,
                                            argument.data() + end, value);
        if (result.ec != std::errc{} || result.ptr != argument.data() + end ||
            value > 255) {
            return false;
        }
        fields[index] = value;
        start = end + 1;
    }
    return true;
}

bool parse_pasv_reply(std::string_view reply, session::UdpEndpoint& endpoint) {
    const auto open = reply.find('(');
    const auto close = reply.find(')', open == std::string_view::npos ? 0 : open + 1);
    if (open == std::string_view::npos || close == std::string_view::npos) {
        return false;
    }
    std::array<unsigned int, 6> fields{};
    if (!parse_six_fields(reply.substr(open + 1, close - open - 1), fields)) {
        return false;
    }
    endpoint.address = std::to_string(fields[0]) + '.' +
                       std::to_string(fields[1]) + '.' +
                       std::to_string(fields[2]) + '.' +
                       std::to_string(fields[3]);
    endpoint.port = static_cast<std::uint16_t>(fields[4] * 256U + fields[5]);
    return endpoint.port != 0;
}

bool parse_port_argument(std::string_view argument,
                         session::UdpEndpoint& endpoint) {
    std::array<unsigned int, 6> fields{};
    if (!parse_six_fields(argument, fields)) {
        return false;
    }
    endpoint.address = std::to_string(fields[0]) + '.' +
                       std::to_string(fields[1]) + '.' +
                       std::to_string(fields[2]) + '.' +
                       std::to_string(fields[3]);
    endpoint.port = static_cast<std::uint16_t>(fields[4] * 256U + fields[5]);
    return endpoint.port != 0;
}

std::pair<std::shared_ptr<network::Socket>, std::uint16_t>
make_bound_udp_socket(std::uint16_t requested_port = 0) {
    auto socket = std::make_shared<network::Socket>(
        ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket->valid()) {
        return {std::move(socket), 0};
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(requested_port);
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

std::optional<std::string> port_command(const session::UdpEndpoint& endpoint) {
    in_addr address{};
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address) != 1) {
        return std::nullopt;
    }
    const auto host = ntohl(address.s_addr);
    return "PORT " + std::to_string((host >> 24U) & 0xffU) + ',' +
           std::to_string((host >> 16U) & 0xffU) + ',' +
           std::to_string((host >> 8U) & 0xffU) + ',' +
           std::to_string(host & 0xffU) + ',' +
           std::to_string(endpoint.port / 256U) + ',' +
           std::to_string(endpoint.port % 256U);
}

bool prepare_data_channel(Client& client, CliState& state) {
    if (state.prepared != session::DataMode::none) {
        return true;
    }
    std::vector<std::string> replies;
    if (state.preference == session::DataMode::passive) {
        state.active_socket.reset();
        const auto status = send_and_receive(client, "PASV", replies);
        if (!status || !has_code(replies, 227)) {
            return false;
        }
        const auto found = std::find_if(replies.begin(), replies.end(),
            [](const auto& reply) { return reply_code(reply) == 227; });
        if (found == replies.end() ||
            !parse_pasv_reply(*found, state.passive_endpoint)) {
            std::cerr << "Invalid PASV reply\n";
            return false;
        }
        state.prepared = session::DataMode::passive;
        return true;
    }

    auto [socket, port] = make_bound_udp_socket();
    if (!socket->valid() || port == 0) {
        std::cerr << "Unable to allocate active UDP port\n";
        return false;
    }
    state.active_socket = std::move(socket);
    state.active_endpoint = {state.active_address, port};
    const auto command = port_command(state.active_endpoint);
    if (!command) {
        std::cerr << "Active address is not valid IPv4\n";
        return false;
    }
    const auto status = send_and_receive(client, *command, replies);
    if (!status || !has_code(replies, 200)) {
        return false;
    }
    state.prepared = session::DataMode::active;
    return true;
}

struct TransferRequest {
    std::string verb;
    std::filesystem::path local_path;
    std::string remote_argument;
    bool listing{false};
    bool upload{false};
};

std::optional<TransferRequest> make_transfer_request(
    const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return std::nullopt;
    }
    TransferRequest request;
    request.verb = uppercase(tokens[0]);
    if (request.verb == "LIST" || request.verb == "NLST") {
        if (tokens.size() > 2) {
            return std::nullopt;
        }
        request.listing = true;
        request.remote_argument = tokens.size() == 2 ? tokens[1] : std::string{};
        return request;
    }
    if (request.verb == "STOR" || request.verb == "APPE") {
        if (tokens.size() < 2 || tokens.size() > 3) {
            return std::nullopt;
        }
        request.upload = true;
        request.local_path = tokens[1];
        request.remote_argument = tokens.size() == 3
            ? tokens[2] : request.local_path.filename().string();
        return request;
    }
    if (request.verb == "STOU") {
        if (tokens.size() != 2) {
            return std::nullopt;
        }
        request.upload = true;
        request.local_path = tokens[1];
        return request;
    }
    if (request.verb == "RETR") {
        if (tokens.size() < 2 || tokens.size() > 3) {
            return std::nullopt;
        }
        request.remote_argument = tokens[1];
        request.local_path = tokens.size() == 3
            ? std::filesystem::path(tokens[2])
            : std::filesystem::path(tokens[1]).filename();
        return request;
    }
    return std::nullopt;
}

std::string wire_command(const TransferRequest& request) {
    if (request.verb == "STOU") {
        return "STOU";
    }
    return request.remote_argument.empty()
        ? request.verb : request.verb + ' ' + request.remote_argument;
}

bool terminal_reply(const std::vector<std::string>& replies) {
    return std::any_of(replies.begin(), replies.end(), [](const auto& reply) {
        const auto code = reply_code(reply);
        return code && (*code == 226 || *code >= 400);
    });
}

bool run_transfer(Client& client, DataChannelClient& data, CliState& state,
                  const TransferRequest& request) {
    if (request.upload && !std::filesystem::is_regular_file(request.local_path)) {
        std::cerr << "Local file not found: " << request.local_path.string() << '\n';
        return false;
    }
    if (!prepare_data_channel(client, state)) {
        return false;
    }

    auto status = client.send_command(wire_command(request));
    if (!status) {
        state.prepared = session::DataMode::none;
        return false;
    }
    std::vector<std::string> replies;
    status = client.receive_reply(replies);
    if (!status) {
        state.prepared = session::DataMode::none;
        return false;
    }
    print_replies(replies);
    const auto opening = std::find_if(replies.begin(), replies.end(),
        [](const auto& reply) { return reply_code(reply) == 150; });
    if (opening == replies.end()) {
        state.prepared = session::DataMode::none;
        return false;
    }
    const auto transfer_id = marker_value(*opening, "id=");
    if (!transfer_id) {
        std::cerr << "Server did not provide a transfer ID\n";
        state.prepared = session::DataMode::none;
        return false;
    }

    DataTransferSpec spec;
    spec.transfer_id = *transfer_id;
    spec.type = state.type;
    spec.mode = state.prepared;
    spec.server_endpoint = state.passive_endpoint;
    spec.active_local_endpoint = state.active_endpoint;
    spec.bound_socket = state.active_socket;
    spec.expected_size = marker_value(*opening, "bytes=").value_or(0);
    unsigned int last_percentage = 101;
    spec.progress = [&last_percentage](std::uint64_t completed,
                                       std::uint64_t total) {
        if (total == 0) {
            std::cout << "\r[data] " << completed << " bytes" << std::flush;
            return;
        }
        const auto percentage = static_cast<unsigned int>(
            (static_cast<long double>(std::min(completed, total)) * 100.0L) /
            static_cast<long double>(total));
        if (percentage != last_percentage) {
            last_percentage = percentage;
            std::cout << "\r[data] " << std::setw(3) << percentage << "% "
                      << completed << '/' << total << " bytes" << std::flush;
        }
    };

    std::string listing;
    if (request.listing) {
        status = data.receive_listing(listing, spec);
    } else if (request.upload) {
        status = data.upload(request.local_path, spec);
    } else {
        status = data.download(request.local_path, spec);
    }
    std::cout << '\n';
    state.prepared = session::DataMode::none;
    state.active_socket.reset();
    if (!status) {
        std::cerr << "Data transfer failed: " << status.message << '\n';
        static_cast<void>(client.send_command("ABOR"));
    } else {
        std::cout << "[data] " << status.message << '\n';
        if (request.listing) {
            std::cout << listing;
        } else if (!request.upload) {
            std::cout << "[client] saved " << request.local_path.string() << '\n';
        }
    }

    while (!terminal_reply(replies) && client.connected()) {
        replies.clear();
        const auto receive_status = client.receive_reply(replies);
        if (!receive_status) {
            return false;
        }
        print_replies(replies);
    }
    return static_cast<bool>(status);
}

} // namespace

int run_cli(std::string_view host, std::uint16_t port) {
    try {
        network::SocketRuntime socket_runtime;
        Client client;
        auto status = client.connect(host, port);
        if (!status) {
            std::cerr << "Connection failed: " << status.message << '\n';
            return 1;
        }
        std::vector<std::string> replies;
        status = client.receive_reply(replies);
        if (!status) {
            std::cerr << "Greeting failed: " << status.message << '\n';
            return 1;
        }
        print_replies(replies);

        CliState state;
        if (!client.local_ipv4(state.active_address)) {
            state.active_address = "127.0.0.1";
        }
        DataChannelClient data;
        std::cout << "Local helpers: PASSIVE, ACTIVE [client_ipv4].\n"
                     "Transfers: STOR <local> [remote], RETR <remote> [local],\n"
                     "APPE <local> [remote], STOU <local>, LIST/NLST [path].\n";

        std::string line;
        while (client.connected()) {
            std::cout << "hybrid-ftp> " << std::flush;
            if (!std::getline(std::cin, line)) {
                line = "QUIT";
            }
            const auto tokens = tokenize(line);
            if (tokens.empty()) {
                continue;
            }
            const auto verb = uppercase(tokens[0]);
            if (verb == "PASSIVE") {
                state.preference = session::DataMode::passive;
                state.prepared = session::DataMode::none;
                state.active_socket.reset();
                std::cout << "[client] Passive UDP mode selected\n";
                continue;
            }
            if (verb == "ACTIVE") {
                if (tokens.size() > 2) {
                    std::cerr << "Usage: ACTIVE [client_ipv4]\n";
                    continue;
                }
                if (tokens.size() == 2) {
                    state.active_address = tokens[1];
                }
                state.preference = session::DataMode::active;
                state.prepared = session::DataMode::none;
                state.active_socket.reset();
                std::cout << "[client] Active UDP mode selected address="
                          << state.active_address << '\n';
                continue;
            }
            if (const auto transfer = make_transfer_request(tokens)) {
                static_cast<void>(run_transfer(client, data, state, *transfer));
                continue;
            }

            std::shared_ptr<network::Socket> requested_active_socket;
            session::UdpEndpoint requested_active_endpoint;
            if (verb == "PORT" && tokens.size() == 2 &&
                parse_port_argument(tokens[1], requested_active_endpoint)) {
                auto [socket, bound_port] =
                    make_bound_udp_socket(requested_active_endpoint.port);
                if (!socket->valid() ||
                    bound_port != requested_active_endpoint.port) {
                    std::cerr << "Unable to reserve requested active UDP port\n";
                    continue;
                }
                requested_active_socket = std::move(socket);
            }

            replies.clear();
            status = send_and_receive(client, line, replies);
            if (!status) {
                std::cerr << "Command failed: " << status.message << '\n';
                break;
            }
            if (verb == "TYPE" && has_code(replies, 200) && tokens.size() == 2) {
                state.type = uppercase(tokens[1]) == "I"
                    ? session::TransferType::binary
                    : session::TransferType::ascii;
            } else if (verb == "PASV" && has_code(replies, 227)) {
                const auto found = std::find_if(replies.begin(), replies.end(),
                    [](const auto& reply) { return reply_code(reply) == 227; });
                if (found != replies.end() &&
                    parse_pasv_reply(*found, state.passive_endpoint)) {
                    state.preference = session::DataMode::passive;
                    state.prepared = session::DataMode::passive;
                    state.active_socket.reset();
                }
            } else if (verb == "PORT" && has_code(replies, 200) &&
                       tokens.size() == 2 &&
                       parse_port_argument(tokens[1], state.active_endpoint)) {
                state.preference = session::DataMode::active;
                state.prepared = session::DataMode::active;
                state.active_socket = std::move(requested_active_socket);
                state.active_address = state.active_endpoint.address;
            }
            if (verb == "QUIT") {
                break;
            }
        }
        client.disconnect();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Client error: " << error.what() << '\n';
        return 1;
    }
}

} // namespace hftp::client
