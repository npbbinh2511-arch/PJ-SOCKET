#ifndef HFTP_CONTROL_COMMAND_PARSER_H
#define HFTP_CONTROL_COMMAND_PARSER_H

#include <cstddef>
#include <string>
#include <string_view>

#include "hftp/protocol/command.h"

namespace hftp::control {

struct CommandParseResult {
    bool ok{false};
    protocol::Command command;
    std::string error;
};

class CommandParser {
public:
    explicit CommandParser(std::size_t max_line_length = 4096)
        : max_line_length_(max_line_length) {}

    [[nodiscard]] CommandParseResult parse(std::string_view line) const;

private:
    std::size_t max_line_length_;
};

} // namespace hftp::control

#endif // HFTP_CONTROL_COMMAND_PARSER_H
