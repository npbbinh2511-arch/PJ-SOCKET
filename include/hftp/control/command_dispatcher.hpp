#pragma once

#include <string>
#include "hftp/control/authenticator.hpp"
#include "hftp/filesystem/file_repository.hpp"
#include "hftp/protocol/command.hpp"
#include "hftp/protocol/reply.hpp"
#include "hftp/transfer/data_connection_manager.hpp"
#include "hftp/transfer/transfer.hpp"

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

