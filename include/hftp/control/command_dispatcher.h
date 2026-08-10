#ifndef HFTP_CONTROL_COMMAND_DISPATCHER_H
#define HFTP_CONTROL_COMMAND_DISPATCHER_H

#include <string>
#include "hftp/control/authenticator.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/protocol/command.h"
#include "hftp/protocol/reply.h"
#include "hftp/transfer/data_connection_manager.h"
#include "hftp/transfer/transfer.h"

namespace hftp::control {

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

    void dispatch(const protocol::Command& command, session::Session& session, ReplySink& replies);

    // TODO(B):
    // - Normalize routing by verb, validate syntax/auth/state, and call the owning module.
    // - Basic handlers: USER/PASS/QUIT/NOOP/TYPE/PORT-or-PASV/STOR/RETR/ABOR.
    // - For transfers: snapshot context, transition state, emit 150, coordinate work,
    //   then emit exactly one terminal 226/425/426/550 through serialized ReplySink.
    // - RNFR/RNTO keeps per-session pending state; unrelated state-changing commands
    //   define whether that state is preserved or cleared before implementation.
    // - Never hold Session::mutex during blocking network/filesystem/transfer calls.
    // - Tests: auth gate, bad sequence, reply order, ABOR race, session isolation.

private:
    Authenticator& auth_;
    filesystem::FileRepository& files_;
    transfer::DataConnectionManager& data_;
    transfer::TransferCoordinator& transfers_;
};

} // namespace hftp::control

#endif // HFTP_CONTROL_COMMAND_DISPATCHER_H
