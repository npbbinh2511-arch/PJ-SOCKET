#include "hftp/control/command_parser.h"

#include <utility>

namespace hftp::control {
namespace {

bool is_ascii_letter(char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

char to_ascii_upper(char value) noexcept {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - 'a' + 'A');
    }
    return value;
}

} // namespace

CommandParseResult CommandParser::parse(std::string_view line) const {
    if (line.size() > max_line_length_) {
        return {false, {}, "Command line exceeds maximum length"};
    }
    if (line.empty()) {
        return {false, {}, "Command line is empty"};
    }
    if (line.find_first_of("\r\n") != std::string_view::npos) {
        return {false, {}, "Command line must not contain CR or LF"};
    }

    const std::size_t separator = line.find_first_of(" \t");
    const std::string_view verb_view = line.substr(0, separator);
    if (verb_view.empty()) {
        return {false, {}, "Command verb is empty"};
    }

    std::string verb;
    verb.reserve(verb_view.size());
    for (const char value : verb_view) {
        if (!is_ascii_letter(value)) {
            return {false, {}, "Command verb must contain only ASCII letters"};
        }
        verb.push_back(to_ascii_upper(value));
    }

    std::string argument;
    if (separator != std::string_view::npos) {
        argument.assign(line.substr(separator + 1));
    }

    return {true, {std::move(verb), std::move(argument)}, {}};
}

} // namespace hftp::control
