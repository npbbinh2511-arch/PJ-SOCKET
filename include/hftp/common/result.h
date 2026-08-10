#ifndef HFTP_COMMON_RESULT_H
#define HFTP_COMMON_RESULT_H

#include <string>

namespace hftp::common {

enum class Error {
    none,
    invalid_argument,
    protocol_error,
    socket_error,
    authentication_failed,
    permission_denied,
    not_found,
    busy,
    cancelled,
    integrity_error,
};

struct Status {
    Error error{Error::none};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept { return error == Error::none; }
};

} // namespace hftp::common

#endif // HFTP_COMMON_RESULT_H
