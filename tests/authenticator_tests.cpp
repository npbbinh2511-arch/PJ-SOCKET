#include "hftp/control/authenticator.h"

#include <cassert>
#include <string_view>

using hftp::common::Error;
using hftp::control::Authenticator;
using hftp::control::CredentialStore;
using hftp::session::AuthState;
using hftp::session::Session;

namespace {

class TestCredentialStore final : public CredentialStore {
public:
    bool verify(std::string_view username, std::string_view password) const override {
        return username == "alice" && password == "secret";
    }
};

} // namespace

int main() {
    const TestCredentialStore credentials;
    const Authenticator authenticator(credentials);

    Session valid(1);
    assert(authenticator.user(valid, "alice"));
    assert(valid.username == "alice");
    assert(valid.auth == AuthState::username_accepted);
    assert(authenticator.pass(valid, "secret"));
    assert(valid.auth == AuthState::authenticated);

    Session pass_first(2);
    const auto sequence_error = authenticator.pass(pass_first, "secret");
    assert(!sequence_error);
    assert(sequence_error.error == Error::protocol_error);
    assert(pass_first.auth == AuthState::unauthenticated);

    Session wrong_password(3);
    assert(authenticator.user(wrong_password, "alice"));
    const auto rejected = authenticator.pass(wrong_password, "wrong");
    assert(!rejected);
    assert(rejected.error == Error::authentication_failed);
    assert(wrong_password.auth == AuthState::username_accepted);

    Session repeated_user(4);
    assert(authenticator.user(repeated_user, "alice"));
    assert(authenticator.pass(repeated_user, "secret"));
    assert(authenticator.user(repeated_user, "bob"));
    assert(repeated_user.username == "bob");
    assert(repeated_user.auth == AuthState::username_accepted);

    Session malformed(5);
    const auto empty_user = authenticator.user(malformed, "");
    assert(!empty_user);
    assert(empty_user.error == Error::invalid_argument);
    assert(malformed.auth == AuthState::unauthenticated);

    Session first(6);
    Session second(7);
    assert(authenticator.user(first, "alice"));
    assert(authenticator.pass(first, "secret"));
    assert(first.auth == AuthState::authenticated);
    assert(second.auth == AuthState::unauthenticated);
    assert(second.username.empty());
}
