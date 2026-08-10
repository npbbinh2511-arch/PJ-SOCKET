#include "hftp/transfer/data_connection_manager.h"

#include <array>
#include <charconv>
#include <string>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace hftp::transfer {
namespace {

common::Status socket_failure(std::string message) {
#ifdef _WIN32
    message += " (WinSock error " + std::to_string(WSAGetLastError()) + ')';
#else
    message += ": ";
    message += std::strerror(errno);
#endif
    return {common::Error::socket_error, std::move(message)};
}

} // namespace

DataConnectionManager::DataConnectionManager(std::string passive_address)
    : passive_address_(std::move(passive_address)) {}

PortParseResult DataConnectionManager::parse_port_argument(std::string_view argument) const {
    std::array<unsigned int, 6> values{};
    std::size_t start = 0;

    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t comma = argument.find(',', start);
        const bool last = index + 1 == values.size();
        if ((last && comma != std::string_view::npos) ||
            (!last && comma == std::string_view::npos)) {
            return {{common::Error::invalid_argument, "PORT requires six fields"}, {}};
        }

        const std::size_t end = last ? argument.size() : comma;
        const std::string_view field = argument.substr(start, end - start);
        if (field.empty()) {
            return {{common::Error::invalid_argument, "PORT fields must not be empty"}, {}};
        }

        unsigned int value = 0;
        const auto conversion = std::from_chars(field.data(), field.data() + field.size(), value);
        if (conversion.ec != std::errc{} || conversion.ptr != field.data() + field.size() ||
            value > 255) {
            return {{common::Error::invalid_argument, "PORT fields must be decimal octets"}, {}};
        }
        values[index] = value;
        start = end + 1;
    }

    const std::uint16_t port = static_cast<std::uint16_t>(values[4] * 256U + values[5]);
    const bool unspecified = values[0] == 0 && values[1] == 0 && values[2] == 0 && values[3] == 0;
    const bool broadcast = values[0] == 255 && values[1] == 255 &&
                           values[2] == 255 && values[3] == 255;
    const bool multicast = values[0] >= 224 && values[0] <= 239;
    if (port == 0 || unspecified || broadcast || multicast) {
        return {{common::Error::invalid_argument, "PORT endpoint is not usable"}, {}};
    }

    session::UdpEndpoint endpoint;
    endpoint.address = std::to_string(values[0]) + '.' + std::to_string(values[1]) + '.' +
                       std::to_string(values[2]) + '.' + std::to_string(values[3]);
    endpoint.port = port;
    return {{}, std::move(endpoint)};
}

common::Status DataConnectionManager::set_active(session::Session& session,
                                                  std::string_view argument) {
    auto parsed = parse_port_argument(argument);
    if (!parsed.status) {
        return parsed.status;
    }

    const std::scoped_lock lock(mutex_, session.mutex);
    passive_sockets_.erase(session.id);
    session.passive_port.reset();
    session.active_endpoint = std::move(parsed.endpoint);
    session.data_mode = session::DataMode::active;
    return {};
}

common::Status DataConnectionManager::open_passive(session::Session& session) {
    if (session.id == 0) {
        return {common::Error::invalid_argument, "Passive mode requires a valid session ID"};
    }

    network::Socket socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) {
        return socket_failure("Unable to create passive UDP socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = 0;
    if (::bind(socket.native_handle(), reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        return socket_failure("Unable to bind passive UDP socket");
    }

#ifdef _WIN32
    int address_length = sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    if (::getsockname(socket.native_handle(), reinterpret_cast<sockaddr*>(&address),
                      &address_length) != 0) {
        return socket_failure("Unable to read passive UDP endpoint");
    }

    const std::uint16_t port = ntohs(address.sin_port);
    const std::scoped_lock lock(mutex_, session.mutex);
    passive_sockets_.insert_or_assign(session.id, std::move(socket));
    session.active_endpoint.reset();
    session.passive_port = port;
    session.data_mode = session::DataMode::passive;
    return {};
}

void DataConnectionManager::reset(session::Session& session) noexcept {
    const std::scoped_lock lock(mutex_, session.mutex);
    passive_sockets_.erase(session.id);
    session.active_endpoint.reset();
    session.passive_port.reset();
    session.data_mode = session::DataMode::none;
}

network::NativeSocket DataConnectionManager::passive_socket(std::uint64_t session_id) const noexcept {
    const std::scoped_lock lock(mutex_);
    const auto found = passive_sockets_.find(session_id);
    return found == passive_sockets_.end()
               ? network::invalid_socket
               : found->second.native_handle();
}

} // namespace hftp::transfer
