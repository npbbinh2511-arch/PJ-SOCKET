#ifndef HFTP_CONTROL_COMMAND_DISPATCHER_H
#define HFTP_CONTROL_COMMAND_DISPATCHER_H

#include <atomic>
#include <string>
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

    [[nodiscard]] DispatchAction dispatch(const protocol::Command& command,
                                          session::Session& session,
                                          ReplySink& replies);

private:
    Authenticator& auth_;
    filesystem::FileRepository& files_;
    transfer::DataConnectionManager& data_;
    transfer::TransferCoordinator& transfers_;
    std::atomic_uint64_t next_transfer_id_{1};
};

} // namespace hftp::control

#endif // HFTP_CONTROL_COMMAND_DISPATCHER_H
