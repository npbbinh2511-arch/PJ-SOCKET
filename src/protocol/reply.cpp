#include "hftp/protocol/reply.h"

#include <stdexcept>
#include <string>

namespace hftp::protocol {

std::string ReplyFormatter::format(ReplyCode code, std::string_view text) const {
    const int numeric_code = static_cast<int>(code);
    if (numeric_code < 100 || numeric_code > 599) {
        throw std::invalid_argument("FTP reply code must contain three digits");
    }
    if (text.find_first_of("\r\n") != std::string_view::npos) {
        throw std::invalid_argument("FTP reply text must not contain CR or LF");
    }

    std::string reply = std::to_string(numeric_code);
    reply.reserve(3 + 1 + text.size() + 2);
    reply.push_back(' ');
    reply.append(text);
    reply.append("\r\n");
    return reply;
}

} // namespace hftp::protocol
