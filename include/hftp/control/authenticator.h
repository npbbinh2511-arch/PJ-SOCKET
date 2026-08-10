#ifndef HFTP_CONTROL_AUTHENTICATOR_H
#define HFTP_CONTROL_AUTHENTICATOR_H

#include <string_view>
#include "hftp/common/result.h"
#include "hftp/session/session.h"

namespace hftp::control {

class CredentialStore {
public:
    virtual ~CredentialStore() = default;
    [[nodiscard]] virtual bool verify(std::string_view username, std::string_view password) const = 0;
};

class Authenticator {
public:
    explicit Authenticator(const CredentialStore& credentials) : credentials_(credentials) {}
    [[nodiscard]] common::Status user(session::Session& session, std::string_view username) const;
    [[nodiscard]] common::Status pass(session::Session& session, std::string_view password) const;

    // TODO(B):
    // - USER validates a non-empty identifier and resets any previous login attempt.
    // - PASS is legal only after USER and delegates secret checking to CredentialStore.
    // - Update only this client's Session; never retain plaintext passwords.
    // - Map errors to 331/230/503/530 in the dispatcher.
    // - Tests: USER then PASS, PASS first, wrong password, repeated USER, isolation.

private:
    const CredentialStore& credentials_;
};

} // namespace hftp::control

#endif // HFTP_CONTROL_AUTHENTICATOR_H
