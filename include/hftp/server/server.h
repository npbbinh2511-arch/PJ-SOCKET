#ifndef HFTP_SERVER_SERVER_H
#define HFTP_SERVER_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"

namespace hftp::control {
class CommandDispatcher;
}
namespace hftp::logging {
class Logger;
}

namespace hftp::server {

class Server {
public:
    explicit Server(control::CommandDispatcher& dispatcher,
                    logging::Logger* logger = nullptr) noexcept
        : dispatcher_(dispatcher), logger_(logger) {}
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] common::Status start(std::uint16_t control_port);
    void request_stop() noexcept;
    void join();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] std::uint16_t control_port() const noexcept { return control_port_; }

private:
    struct ClientWorker {
        std::shared_ptr<std::atomic_bool> finished;
        std::jthread thread;
    };

    void accept_loop();
    void handle_client(network::Socket client, std::string peer_address);
    void reap_finished_clients();

    std::atomic_bool stopping_{false};
    std::atomic_bool running_{false};
    std::atomic_uint64_t next_session_id_{1};
    std::atomic_uint64_t active_sessions_{0};
    network::Socket listener_;
    std::jthread accept_thread_;
    std::mutex client_threads_mutex_;
    std::vector<ClientWorker> client_workers_;
    std::uint16_t control_port_{};
    control::CommandDispatcher& dispatcher_;
    logging::Logger* logger_{};
};

} // namespace hftp::server

#endif // HFTP_SERVER_SERVER_H
