#ifndef HFTP_SESSION_SESSION_H
#define HFTP_SESSION_SESSION_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace hftp::session {

enum class AuthState { unauthenticated, username_accepted, authenticated };
enum class TransferType { ascii, binary };
enum class TransferMode { stream, block, compressed };
enum class DataMode { none, active, passive };
enum class TransferState { idle, preparing, running, cancelling };

struct UdpEndpoint { std::string address; std::uint16_t port{}; };

struct Session {
    std::uint64_t id{};
    std::string username;
    AuthState auth{AuthState::unauthenticated};
    std::filesystem::path current_directory{"/"};
    TransferType type{TransferType::ascii};
    TransferMode mode{TransferMode::stream};
    DataMode data_mode{DataMode::none};
    std::optional<UdpEndpoint> active_endpoint;
    std::optional<std::uint16_t> passive_port;
    std::optional<std::filesystem::path> rename_from;
    TransferState transfer{TransferState::idle};
    std::uint64_t transfer_id{};
    std::atomic_bool cancel_requested{false};

    // Reply writes and compound state transitions use this mutex briefly.
    // Never hold it during blocking socket or filesystem I/O.
    mutable std::mutex mutex;
};

} // namespace hftp::session

#endif // HFTP_SESSION_SESSION_H
