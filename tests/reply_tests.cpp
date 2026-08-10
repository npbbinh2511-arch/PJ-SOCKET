#include "hftp/protocol/reply.h"

#include <cassert>
#include <stdexcept>
#include <string_view>

using hftp::protocol::ReplyCode;
using hftp::protocol::ReplyFormatter;

namespace {

void expect_invalid(const ReplyFormatter& formatter, ReplyCode code, std::string_view text) {
    bool thrown = false;
    try {
        static_cast<void>(formatter.format(code, text));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    assert(thrown);
}

} // namespace

int main() {
    const ReplyFormatter formatter;

    assert(formatter.format(ReplyCode::data_already_open, "Data connection already open") ==
           "125 Data connection already open\r\n");
    assert(formatter.format(ReplyCode::ok, "Command okay") == "200 Command okay\r\n");
    assert(formatter.format(ReplyCode::need_password, "Need password") ==
           "331 Need password\r\n");
    assert(formatter.format(ReplyCode::unavailable, "Service unavailable") ==
           "421 Service unavailable\r\n");
    assert(formatter.format(ReplyCode::file_unavailable, "File unavailable") ==
           "550 File unavailable\r\n");
    assert(formatter.format(ReplyCode::ok, "") == "200 \r\n");

    expect_invalid(formatter, ReplyCode::ok, "First line\rSecond line");
    expect_invalid(formatter, ReplyCode::ok, "First line\nSecond line");
    expect_invalid(formatter, static_cast<ReplyCode>(99), "Invalid code");
    expect_invalid(formatter, static_cast<ReplyCode>(600), "Invalid code");
}
