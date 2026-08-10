#include "hftp/client/client.h"
#include "hftp/control/command_dispatcher.h"
#include "hftp/network/socket.h"
#include "hftp/server/server.h"

#include <cassert>
#include <filesystem>
#include <vector>

using hftp::client::Client;
using hftp::common::Error;
using hftp::common::Status;
using hftp::control::Authenticator;
using hftp::control::CommandDispatcher;
using hftp::control::CredentialStore;
using hftp::filesystem::Entry;
using hftp::filesystem::FileRepository;
using hftp::network::SocketRuntime;
using hftp::server::Server;
using hftp::transfer::DataConnectionManager;
using hftp::transfer::TransferContext;
using hftp::transfer::TransferCoordinator;

namespace {

class Credentials final : public CredentialStore {
public:
    bool verify(std::string_view username, std::string_view password) const override {
        return username == "alice" && password == "secret";
    }
};

class Repository final : public FileRepository {
public:
    Status resolve_safe(const std::filesystem::path& cwd,
                        const std::filesystem::path& requested,
                        std::filesystem::path& resolved) const override {
        resolved = cwd / requested;
        return {};
    }
    Status list_file(const std::filesystem::path&, std::vector<Entry>&) const override {
        return {Error::not_found, "Not used"};
    }
    Status read_file(const std::filesystem::path&, std::vector<std::uint8_t>&) const override {
        return {Error::not_found, "Not used"};
    }
    Status write_file(const std::filesystem::path&,
                      const std::vector<std::uint8_t>&) const override {
        return {Error::not_found, "Not used"};
    }
};

class Coordinator final : public TransferCoordinator {
public:
    Status start(const TransferContext&) override { return {}; }
    void request_cancel(std::uint64_t) override {}
};

void expect_reply(Client& client, std::string_view expected) {
    std::vector<std::string> replies;
    assert(client.receive_reply(replies));
    assert(replies.size() == 1);
    assert(replies.front() == expected);
}

void complete_session(Server& server) {
    Client client;
    assert(client.connect("127.0.0.1", server.control_port()));
    expect_reply(client, "220 Service ready");
    assert(client.send_command("USER alice"));
    expect_reply(client, "331 Username okay, need password");
    assert(client.send_command("PASS secret"));
    expect_reply(client, "230 Login successful");
    assert(client.send_command("NOOP"));
    expect_reply(client, "200 Command okay");
    assert(client.send_command("QUIT"));
    expect_reply(client, "221 Goodbye");
}

} // namespace

int main() {
    SocketRuntime runtime;
    Credentials credentials;
    Authenticator authenticator(credentials);
    Repository repository;
    DataConnectionManager data;
    Coordinator coordinator;
    CommandDispatcher dispatcher(authenticator, repository, data, coordinator);

    Server sequential(dispatcher);
    assert(sequential.start(0));
    assert(sequential.running());
    assert(sequential.control_port() != 0);
    complete_session(sequential);
    complete_session(sequential);
    sequential.request_stop();
    sequential.join();
    assert(!sequential.running());

    Server interrupted(dispatcher);
    assert(interrupted.start(0));
    Client waiting;
    assert(waiting.connect("127.0.0.1", interrupted.control_port()));
    expect_reply(waiting, "220 Service ready");
    interrupted.request_stop();
    interrupted.join();
    assert(!interrupted.running());
}
