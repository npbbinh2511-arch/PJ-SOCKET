#ifndef HFTP_SERVER_SERVER_H
#define HFTP_SERVER_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "hftp/common/result.h"
#include "hftp/network/socket.h"

namespace hftp::server {

class Server {
public:
    [[nodiscard]] common::Status start(std::uint16_t control_port);
    void request_stop() noexcept;
    void join();

    // TODO(B):
    // - Create/bind/listen on one owned TCP socket and accept until stop is requested.
    // - Give each accepted client a fresh Session and a joinable managed worker.
    // - Define ownership so shutdown unblocks accept/recv, joins workers, and closes once.
    // - Bound or reap completed worker handles; never detach unmanaged threads.
    // - Tests: bind failure, one client, two isolated clients, shutdown during recv.

private:
    std::atomic_bool stopping_{false};
    network::Socket listener_;
    // TODO(B): choose std::jthread or a worker registry after documenting shutdown order.
};

} // namespace hftp::server

#endif // HFTP_SERVER_SERVER_H
