#ifndef HFTP_PROTOCOL_REPLY_H
#define HFTP_PROTOCOL_REPLY_H

#include <string>
#include <string_view>

namespace hftp::protocol {

enum class ReplyCode : int {
    data_already_open = 125, opening_data = 150, ok = 200, status = 211,
    file_status = 213, help = 214, ready = 220, goodbye = 221,
    transfer_complete = 226, passive = 227, logged_in = 230,
    file_action_ok = 250, path_created = 257, need_password = 331,
    rename_pending = 350, unavailable = 421, cannot_open_data = 425,
    transfer_aborted = 426, action_unavailable = 450, syntax_error = 500,
    parameter_error = 501, not_implemented = 502, bad_sequence = 503,
    not_logged_in = 530, file_unavailable = 550,
};

class ReplyFormatter {
public:
    [[nodiscard]] std::string format(ReplyCode code, std::string_view text) const;
};

} // namespace hftp::protocol

#endif // HFTP_PROTOCOL_REPLY_H
