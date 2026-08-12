#include "hftp/control/command_dispatcher.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/network/socket.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class Credentials final : public hftp::control::CredentialStore {
public:
    bool verify(std::string_view username,
                std::string_view password) const override {
        return username == "alice" && password == "secret";
    }
};

class Transfers final : public hftp::transfer::TransferCoordinator {
public:
    hftp::common::Status start(
        const hftp::transfer::TransferContext&) override {
        return {};
    }
    void request_cancel(std::uint64_t) override {}
};

class Replies final : public hftp::control::ReplySink {
public:
    void send(hftp::protocol::ReplyCode code, std::string text) override {
        values.emplace_back(code, std::move(text));
    }

    std::vector<std::pair<hftp::protocol::ReplyCode, std::string>> values;
};

void expect(hftp::control::CommandDispatcher& dispatcher,
            hftp::session::Session& session,
            Replies& replies,
            std::string verb,
            std::string argument,
            hftp::protocol::ReplyCode expected) {
    const auto action = dispatcher.dispatch(
        {std::move(verb), std::move(argument)}, session, replies);
    assert(action == hftp::control::DispatchAction::continue_session);
    assert(!replies.values.empty());
    assert(replies.values.back().first == expected);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using hftp::protocol::ReplyCode;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
        ("hftp_unified_dispatcher_" + std::to_string(unique));
    fs::create_directories(root);

    hftp::network::SocketRuntime runtime;
    Credentials credentials;
    hftp::control::Authenticator authenticator(credentials);
    hftp::filesystem::Std_FileRepository repository(root);
    hftp::transfer::DataConnectionManager data_connections;
    Transfers transfers;
    hftp::control::CommandDispatcher dispatcher(
        authenticator, repository, data_connections, transfers);
    hftp::session::Session session(1);
    session.control_peer_address = "127.0.0.1";
    Replies replies;

    expect(dispatcher, session, replies, "USER", "alice", ReplyCode::need_password);
    expect(dispatcher, session, replies, "PASS", "secret", ReplyCode::logged_in);
    expect(dispatcher, session, replies, "PWD", "", ReplyCode::path_created);
    expect(dispatcher, session, replies, "MKD", "docs", ReplyCode::path_created);
    expect(dispatcher, session, replies, "CWD", "docs", ReplyCode::file_action_ok);
    assert(session.current_directory.generic_string() == "/docs");

    std::ofstream(root / "docs" / "sample.txt", std::ios::binary) << "hello";
    expect(dispatcher, session, replies, "SIZE", "sample.txt", ReplyCode::file_status);
    assert(replies.values.back().second == "5");
    expect(dispatcher, session, replies, "MDTM", "sample.txt", ReplyCode::file_status);
    assert(replies.values.back().second.size() == 14);
    expect(dispatcher, session, replies, "HASH", "sample.txt", ReplyCode::file_status);
    assert(replies.values.back().second ==
           "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");

    expect(dispatcher, session, replies, "RNFR", "sample.txt", ReplyCode::rename_pending);
    expect(dispatcher, session, replies, "RNTO", "renamed.txt", ReplyCode::file_action_ok);
    assert(fs::exists(root / "docs" / "renamed.txt"));
    expect(dispatcher, session, replies, "DELE", "renamed.txt", ReplyCode::file_action_ok);
    expect(dispatcher, session, replies, "CDUP", "", ReplyCode::file_action_ok);
    expect(dispatcher, session, replies, "RMD", "docs", ReplyCode::file_action_ok);
    expect(dispatcher, session, replies, "SYST", "", ReplyCode::system_type);
    expect(dispatcher, session, replies, "STRU", "F", ReplyCode::ok);

    dispatcher.end_session(session);
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    return 0;
}
