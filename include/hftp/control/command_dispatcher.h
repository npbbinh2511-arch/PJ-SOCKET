#ifndef HFTP_CONTROL_COMMAND_DISPATCHER_H
#define HFTP_CONTROL_COMMAND_DISPATCHER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include "hftp/control/authenticator.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/protocol/command.h"
#include "hftp/protocol/reply.h"
#include "hftp/transfer/data_connection_manager.h"
#include "hftp/transfer/transfer.h"

namespace hftp::control {

enum class DispatchAction { continue_session, close_session };

class ReplySink {
public:
    virtual ~ReplySink() = default;
    virtual void send(protocol::ReplyCode code, std::string text) = 0;
};

class CommandDispatcher {
public:
    CommandDispatcher(Authenticator& auth, filesystem::FileRepository& files,
                      transfer::DataConnectionManager& data,
                      transfer::TransferCoordinator& transfers)
        : auth_(auth), files_(files), data_(data), transfers_(transfers) {}
    ~CommandDispatcher();
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    [[nodiscard]] DispatchAction dispatch(const protocol::Command& command,
                                          session::Session& session,
                                          ReplySink& replies);
    void end_session(session::Session& session) noexcept;

private:
    struct TransferJob {
        std::atomic_bool done{false};
        // Serializes an ABOR acknowledgement with the terminal transfer reply.
        // This prevents a fast worker from emitting 426 before the 200 ABOR reply.
        std::mutex completion_mutex;
        std::jthread worker;
    };

    void remove_finished_job(std::uint64_t session_id);
    void wait_for_job(std::uint64_t session_id) noexcept;
    [[nodiscard]] std::shared_ptr<TransferJob> find_job(
        std::uint64_t session_id) noexcept;

    Authenticator& auth_;
    filesystem::FileRepository& files_;
    transfer::DataConnectionManager& data_;
    transfer::TransferCoordinator& transfers_;
    std::atomic_uint64_t next_transfer_id_{1};
    std::mutex jobs_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<TransferJob>> jobs_;
};

} // namespace hftp::control

#endif // HFTP_CONTROL_COMMAND_DISPATCHER_H
