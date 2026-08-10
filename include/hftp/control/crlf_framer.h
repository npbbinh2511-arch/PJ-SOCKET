#ifndef HFTP_CONTROL_CRLF_FRAMER_H
#define HFTP_CONTROL_CRLF_FRAMER_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hftp::control {

class CrlfFramer {
public:
    explicit CrlfFramer(std::size_t max_line_length = 4096) : max_line_length_(max_line_length) {}
    [[nodiscard]] std::vector<std::string> push(std::string_view bytes);
    [[nodiscard]] bool failed() const noexcept { return failed_; }

    // TODO(B):
    // - Append fragmented TCP bytes and extract every complete CRLF-delimited line.
    // - Retain an incomplete suffix and support multiple lines in one recv.
    // - Enter a failed state if the buffered command exceeds max_line_length_.
    // - Tests: byte-by-byte input, split CRLF, coalesced lines, oversized input.

private:
    std::size_t max_line_length_;
    std::string buffer_;
    bool failed_{false};
};

} // namespace hftp::control

#endif // HFTP_CONTROL_CRLF_FRAMER_H
