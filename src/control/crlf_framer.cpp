#include "hftp/control/crlf_framer.h"

namespace hftp::control {

std::vector<std::string> CrlfFramer::push(std::string_view bytes) {
    if (failed_) {
        return {};
    }

    buffer_.append(bytes);

    std::vector<std::string> lines;
    std::size_t consumed = 0;
    std::size_t delimiter = buffer_.find("\r\n", consumed);
    while (delimiter != std::string::npos) {
        const std::size_t line_length = delimiter - consumed;
        if (line_length > max_line_length_) {
            failed_ = true;
            buffer_.clear();
            return {};
        }

        lines.emplace_back(buffer_.substr(consumed, line_length));
        consumed = delimiter + 2;
        delimiter = buffer_.find("\r\n", consumed);
    }

    if (consumed != 0) {
        buffer_.erase(0, consumed);
    }

    const bool trailing_carriage_return = !buffer_.empty() && buffer_.back() == '\r';
    const std::size_t pending_length = buffer_.size() - (trailing_carriage_return ? 1U : 0U);
    if (pending_length > max_line_length_) {
        failed_ = true;
        buffer_.clear();
        return {};
    }

    return lines;
}

} // namespace hftp::control
