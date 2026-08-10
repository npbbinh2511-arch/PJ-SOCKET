#ifndef HFTP_SERVER_SERVER_H
#define HFTP_SERVER_SERVER_H

#include <atomic>
#include <cstdint>
#include <thread>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"

namespace hftp::control {
class CommandDispatcher;
}

namespace hftp::server {

class Server {
public:
    explicit Server(control::CommandDispatcher& dispatcher) noexcept
        : dispatcher_(dispatcher) {}
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] common::Status start(std::uint16_t control_port);
    void request_stop() noexcept;
    void join();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] std::uint16_t control_port() const noexcept { return control_port_; }

private:
    void accept_loop();
    void handle_client(network::Socket client);

    std::atomic_bool stopping_{false};
    std::atomic_bool running_{false};
    std::atomic_uint64_t next_session_id_{1};
    network::Socket listener_;
    std::jthread accept_thread_;
    std::uint16_t control_port_{};
    control::CommandDispatcher& dispatcher_;
};

} // namespace hftp::server

#endif // HFTP_SERVER_SERVER_H
