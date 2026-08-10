#ifndef HFTP_PROTOCOL_COMMAND_H
#define HFTP_PROTOCOL_COMMAND_H

#include <string>

namespace hftp::protocol {

struct Command {
    std::string verb;
    std::string argument;
};

} // namespace hftp::protocol

#endif // HFTP_PROTOCOL_COMMAND_H
