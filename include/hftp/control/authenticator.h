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

private:
    const CredentialStore& credentials_;
};

} // namespace hftp::control

#endif // HFTP_CONTROL_AUTHENTICATOR_H
