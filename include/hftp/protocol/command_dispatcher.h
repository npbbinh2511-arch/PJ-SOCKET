#ifndef HFTP_PROTOCOL_COMMAND_DISPATCHER_H
#define HFTP_PROTOCOL_COMMAND_DISPATCHER_H

#include <string>
#include "hftp/protocol/command.h"
#include "hftp/protocol/reply.h"
#include "hftp/session/session.h"
#include "hftp/session/session_service.h"

namespace hftp::protocol {

class CommandDispatcher {
private:
    const session::SessionService& m_session_service;
    ReplyFormatter m_formatter;

public:
    explicit CommandDispatcher(const session::SessionService& session_service);

    // Trả về chuỗi reply đã format chuẩn FTP (ví dụ: "250 Directory changed.\r\n")
    std::string dispatch(session::Session& session, const Command& cmd);
};

} // namespace hftp::protocol

#endif // HFTP_PROTOCOL_COMMAND_DISPATCHER_H