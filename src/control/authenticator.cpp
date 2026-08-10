#include "hftp/control/authenticator.h"

#include <mutex>
#include <string>

namespace hftp::control {

common::Status Authenticator::user(session::Session& session, std::string_view username) const {
    if (username.empty() || username.find_first_of("\r\n") != std::string_view::npos) {
        return {common::Error::invalid_argument, "Username is empty or malformed"};
    }

    const std::scoped_lock lock(session.mutex);
    session.username.assign(username);
    session.auth = session::AuthState::username_accepted;
    return {};
}

common::Status Authenticator::pass(session::Session& session, std::string_view password) const {
    std::string username;
    {
        const std::scoped_lock lock(session.mutex);
        if (session.auth != session::AuthState::username_accepted) {
            return {common::Error::protocol_error, "PASS requires a preceding USER command"};
        }
        username = session.username;
    }

    const bool verified = credentials_.verify(username, password);

    const std::scoped_lock lock(session.mutex);
    if (session.auth != session::AuthState::username_accepted || session.username != username) {
        return {common::Error::protocol_error, "Authentication state changed during verification"};
    }
    if (!verified) {
        return {common::Error::authentication_failed, "Invalid username or password"};
    }

    session.auth = session::AuthState::authenticated;
    return {};
}

} // namespace hftp::control
