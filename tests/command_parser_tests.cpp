#include "hftp/control/command_parser.h"

#include <cassert>
#include <string_view>

using hftp::control::CommandParser;

namespace {

void expect_error(const CommandParser& parser, std::string_view line) {
    const auto result = parser.parse(line);
    assert(!result.ok);
    assert(!result.error.empty());
}

} // namespace

int main() {
    const CommandParser parser;

    const auto user = parser.parse("USER alice");
    assert(user.ok);
    assert(user.command.verb == "USER");
    assert(user.command.argument == "alice");
    assert(user.error.empty());

    const auto mixed_case = parser.parse("rEtR Folder/My File.txt");
    assert(mixed_case.ok);
    assert(mixed_case.command.verb == "RETR");
    assert(mixed_case.command.argument == "Folder/My File.txt");

    const auto without_argument = parser.parse("NOOP");
    assert(without_argument.ok);
    assert(without_argument.command.verb == "NOOP");
    assert(without_argument.command.argument.empty());

    const auto preserved_argument = parser.parse("CWD   Folder With Spaces  ");
    assert(preserved_argument.ok);
    assert(preserved_argument.command.verb == "CWD");
    assert(preserved_argument.command.argument == "  Folder With Spaces  ");

    const auto tab_separator = parser.parse("USER\talice");
    assert(tab_separator.ok);
    assert(tab_separator.command.verb == "USER");
    assert(tab_separator.command.argument == "alice");

    expect_error(parser, "");
    expect_error(parser, "   ");
    expect_error(parser, " USER alice");
    expect_error(parser, "US3R alice");
    expect_error(parser, "USER! alice");
    expect_error(parser, "USER alice\r");
    expect_error(parser, "USER alice\nNOOP");

    const CommandParser limited(4);
    assert(limited.parse("NOOP").ok);
    expect_error(limited, "NOOP ");
}
