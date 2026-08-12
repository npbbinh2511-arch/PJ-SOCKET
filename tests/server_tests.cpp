#include "hftp/client/client.h"
#include "hftp/control/command_dispatcher.h"
#include "hftp/network/socket.h"
#include "hftp/server/server.h"

#include <algorithm>
#include <cassert>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <vector>

using hftp::client::Client;
using hftp::common::Error;
using hftp::common::Status;
using hftp::control::Authenticator;
using hftp::control::CommandDispatcher;
using hftp::control::CredentialStore;
using hftp::filesystem::Entry;
using hftp::filesystem::FileMetadata;
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
    Status append_file(const std::filesystem::path&,
                       const std::vector<std::uint8_t>&) const override {
        return {Error::not_found, "Not used"};
    }
    Status write_unique(const std::filesystem::path&,
                        const std::vector<std::uint8_t>&,
                        std::filesystem::path&) const override {
        return {Error::not_found, "Not used"};
    }
    Status metadata(const std::filesystem::path&, FileMetadata&) const override {
        return {Error::not_found, "Not used"};
    }
    Status make_directory(const std::filesystem::path&) const override {
        return {Error::not_found, "Not used"};
    }
    Status remove_directory(const std::filesystem::path&) const override {
        return {Error::not_found, "Not used"};
    }
    Status remove_file(const std::filesystem::path&) const override {
        return {Error::not_found, "Not used"};
    }
    Status rename_entry(const std::filesystem::path&,
                        const std::filesystem::path&) const override {
        return {Error::not_found, "Not used"};
    }
    Status sha256_file(const std::filesystem::path&, std::string&) const override {
        return {Error::not_found, "Not used"};
    }
};

class Coordinator final : public TransferCoordinator {
public:
    Status start(const TransferContext&) override { return {}; }
    void request_cancel(std::uint64_t) override {}
};

class BlockingCoordinator final : public TransferCoordinator {
public:
    Status start(const TransferContext& context) override {
        {
            const std::scoped_lock lock(mutex_);
            started_ = true;
            cancellation_ = context.cancellation;
            condition_.notify_all();
        }
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] {
            return cancel_called_ ||
                   (cancellation_ && cancellation_->load());
        });
        return {Error::cancelled, "Transfer aborted"};
    }

    void request_cancel(std::uint64_t) override {
        const std::scoped_lock lock(mutex_);
        cancel_called_ = true;
        condition_.notify_all();
    }

    void wait_until_started() {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return started_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic_bool* cancellation_{};
    bool started_{false};
    bool cancel_called_{false};
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

    Server concurrent(dispatcher);
    assert(concurrent.start(0));
    assert(concurrent.running());
    assert(concurrent.control_port() != 0);

    Client first;
    Client second;
    assert(first.connect("127.0.0.1", concurrent.control_port()));
    expect_reply(first, "220 Service ready");
    assert(second.connect("127.0.0.1", concurrent.control_port()));
    expect_reply(second, "220 Service ready");
    assert(first.send_command("NOOP"));
    expect_reply(first, "200 Command okay");
    assert(second.send_command("NOOP"));
    expect_reply(second, "200 Command okay");
    assert(first.send_command("QUIT"));
    expect_reply(first, "221 Goodbye");
    assert(second.send_command("QUIT"));
    expect_reply(second, "221 Goodbye");

    complete_session(concurrent);
    concurrent.request_stop();
    concurrent.join();
    assert(!concurrent.running());

    Server interrupted(dispatcher);
    assert(interrupted.start(0));
    Client waiting;
    assert(waiting.connect("127.0.0.1", interrupted.control_port()));
    expect_reply(waiting, "220 Service ready");
    interrupted.request_stop();
    interrupted.join();
    assert(!interrupted.running());

    BlockingCoordinator blocking_coordinator;
    DataConnectionManager blocking_data;
    CommandDispatcher blocking_dispatcher(
        authenticator, repository, blocking_data, blocking_coordinator);
    Server abortable(blocking_dispatcher);
    assert(abortable.start(0));
    Client transfer_client;
    assert(transfer_client.connect("127.0.0.1", abortable.control_port()));
    expect_reply(transfer_client, "220 Service ready");
    assert(transfer_client.send_command("USER alice"));
    expect_reply(transfer_client, "331 Username okay, need password");
    assert(transfer_client.send_command("PASS secret"));
    expect_reply(transfer_client, "230 Login successful");
    assert(transfer_client.send_command("PORT 127,0,0,1,195,80"));
    expect_reply(transfer_client, "200 Active data endpoint accepted");
    assert(transfer_client.send_command("STOR cancel.bin"));
    expect_reply(transfer_client, "150 Opening data connection id=1");
    blocking_coordinator.wait_until_started();
    assert(transfer_client.send_command("ABOR"));

    bool abort_acknowledged = false;
    bool transfer_aborted = false;
    std::vector<std::string> transfer_replies;
    while (!abort_acknowledged || !transfer_aborted) {
        std::vector<std::string> replies;
        assert(transfer_client.receive_reply(replies));
        for (const auto& reply : replies) {
            transfer_replies.push_back(reply);
            abort_acknowledged = abort_acknowledged || reply == "200 Abort requested";
            transfer_aborted = transfer_aborted || reply == "426 Transfer aborted";
        }
    }
    const auto abort_reply = std::find(
        transfer_replies.begin(), transfer_replies.end(), "200 Abort requested");
    const auto terminal_reply = std::find(
        transfer_replies.begin(), transfer_replies.end(), "426 Transfer aborted");
    assert(abort_reply < terminal_reply);
    assert(transfer_client.send_command("QUIT"));
    expect_reply(transfer_client, "221 Goodbye");
    abortable.request_stop();
    abortable.join();

    BlockingCoordinator shutdown_coordinator;
    DataConnectionManager shutdown_data;
    CommandDispatcher shutdown_dispatcher(
        authenticator, repository, shutdown_data, shutdown_coordinator);
    Server shutdown_during_transfer(shutdown_dispatcher);
    assert(shutdown_during_transfer.start(0));
    Client shutdown_client;
    assert(shutdown_client.connect(
        "127.0.0.1", shutdown_during_transfer.control_port()));
    expect_reply(shutdown_client, "220 Service ready");
    assert(shutdown_client.send_command("USER alice"));
    expect_reply(shutdown_client, "331 Username okay, need password");
    assert(shutdown_client.send_command("PASS secret"));
    expect_reply(shutdown_client, "230 Login successful");
    assert(shutdown_client.send_command("PORT 127,0,0,1,195,81"));
    expect_reply(shutdown_client, "200 Active data endpoint accepted");
    assert(shutdown_client.send_command("STOR shutdown.bin"));
    expect_reply(shutdown_client, "150 Opening data connection id=1");
    shutdown_coordinator.wait_until_started();
    shutdown_during_transfer.request_stop();
    shutdown_during_transfer.join();
    assert(!shutdown_during_transfer.running());
}
