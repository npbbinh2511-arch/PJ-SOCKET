#pragma once

#include <string>
#include <string_view>

namespace hftp::protocol {

struct Command {
    std::string verb;
    std::string argument;
};

struct CommandParseResult {
    bool ok{false};
    Command command;
    std::string error;
};

class CommandParser {
public:
    [[nodiscard]] CommandParseResult parse(std::string_view line) const;

    // TODO(B):
    // - Reject embedded CR/LF, empty verbs, and lines above the configured limit.
    // - Uppercase only the verb; preserve the argument after the first separator.
    // - Return a structured parse error and perform no auth/filesystem/RDT work.
    // - Tests: empty, whitespace-only, mixed-case verb, spaces in argument, long line.
};

} // namespace hftp::protocol

