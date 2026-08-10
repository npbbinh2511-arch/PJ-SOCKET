#include "hftp/control/command_dispatcher.h"
#include "hftp/network/socket.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using hftp::common::Error;
using hftp::common::Status;
using hftp::control::Authenticator;
using hftp::control::CommandDispatcher;
using hftp::control::CredentialStore;
using hftp::control::DispatchAction;
using hftp::control::ReplySink;
using hftp::filesystem::Entry;
using hftp::filesystem::FileRepository;
using hftp::protocol::Command;
using hftp::protocol::ReplyCode;
using hftp::network::SocketRuntime;
using hftp::session::AuthState;
using hftp::session::DataMode;
using hftp::session::Session;
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
        if (requested == "missing") {
            return {Error::not_found, "File unavailable"};
        }
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
    Status result{};
    std::vector<TransferContext> contexts;
    std::vector<std::uint64_t> cancellations;

    Status start(const TransferContext& context) override {
        contexts.push_back(context);
        return result;
    }
    void request_cancel(std::uint64_t transfer_id) override {
        cancellations.push_back(transfer_id);
    }
};

class BlockingCoordinator final : public TransferCoordinator {
public:
    Status start(const TransferContext& context) override {
        std::unique_lock lock(mutex);
        active_transfer_id = context.transfer_id;
        cancellation = context.cancellation;
        started = true;
        condition.notify_all();
        condition.wait(lock, [this] {
            return cancel_called ||
                   (cancellation != nullptr && cancellation->load(std::memory_order_acquire));
        });
        return {Error::cancelled, "Transfer aborted"};
    }

    void request_cancel(std::uint64_t transfer_id) override {
        const std::scoped_lock lock(mutex);
        cancelled_transfer_id = transfer_id;
        cancel_called = true;
        condition.notify_all();
    }

    void wait_until_started() {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] { return started; });
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool started{false};
    bool cancel_called{false};
    std::uint64_t active_transfer_id{};
    std::uint64_t cancelled_transfer_id{};
    std::atomic_bool* cancellation{};
};

class Replies final : public ReplySink {
public:
    void send(ReplyCode code, std::string text) override {
        const std::scoped_lock lock(mutex);
        values.emplace_back(code, std::move(text));
    }

    bool contains(ReplyCode code) const {
        const std::scoped_lock lock(mutex);
        return std::any_of(values.begin(), values.end(),
                           [code](const auto& value) { return value.first == code; });
    }

    mutable std::mutex mutex;
    std::vector<std::pair<ReplyCode, std::string>> values;
};

void login(CommandDispatcher& dispatcher, Session& session, Replies& replies) {
    assert(dispatcher.dispatch(Command{"USER", "alice"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::need_password);
    assert(dispatcher.dispatch(Command{"PASS", "secret"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::logged_in);
    assert(session.auth == AuthState::authenticated);
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

    Session session(1);
    Replies replies;
    assert(dispatcher.dispatch(Command{"STOR", "file.txt"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::not_logged_in);

    login(dispatcher, session, replies);
    assert(dispatcher.dispatch(Command{"TYPE", "I"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::ok);
    assert(dispatcher.dispatch(Command{"PORT", "127,0,0,1,195,80"}, session, replies) ==
           DispatchAction::continue_session);
    assert(session.data_mode == DataMode::active);

    assert(dispatcher.dispatch(Command{"STOR", "file.txt"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.contains(ReplyCode::opening_data));
    assert(replies.values.back().first == ReplyCode::transfer_complete);
    assert(coordinator.contexts.size() == 1);
    assert(coordinator.contexts.front().path == std::filesystem::path("/") / "file.txt");
    assert(session.data_mode == DataMode::none);

    assert(dispatcher.dispatch(Command{"PORT", "127,0,0,1,195,80"}, session, replies) ==
           DispatchAction::continue_session);
    assert(dispatcher.dispatch(Command{"RETR", "missing"}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::file_unavailable);
    assert(coordinator.contexts.size() == 1);

    assert(dispatcher.dispatch(Command{"ABOR", ""}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::bad_sequence);

    assert(dispatcher.dispatch(Command{"PASV", ""}, session, replies) ==
           DispatchAction::continue_session);
    assert(replies.values.back().first == ReplyCode::passive);
    assert(session.data_mode == DataMode::passive);
    data.reset(session);

    assert(dispatcher.dispatch(Command{"QUIT", ""}, session, replies) ==
           DispatchAction::close_session);
    assert(replies.values.back().first == ReplyCode::goodbye);

    DataConnectionManager cancellable_data;
    BlockingCoordinator blocking;
    CommandDispatcher cancellable_dispatcher(
        authenticator, repository, cancellable_data, blocking);
    Session cancellable_session(2);
    Replies cancellation_replies;
    login(cancellable_dispatcher, cancellable_session, cancellation_replies);
    assert(cancellable_dispatcher.dispatch(
               Command{"PORT", "127,0,0,1,195,80"},
               cancellable_session, cancellation_replies) ==
           DispatchAction::continue_session);

    std::thread transfer_thread([&] {
        assert(cancellable_dispatcher.dispatch(
                   Command{"STOR", "cancel.txt"},
                   cancellable_session, cancellation_replies) ==
               DispatchAction::continue_session);
    });
    blocking.wait_until_started();
    assert(cancellable_dispatcher.dispatch(
               Command{"ABOR", ""}, cancellable_session, cancellation_replies) ==
           DispatchAction::continue_session);
    transfer_thread.join();

    assert(blocking.active_transfer_id != 0);
    assert(blocking.cancelled_transfer_id == blocking.active_transfer_id);
    assert(cancellation_replies.contains(ReplyCode::opening_data));
    assert(cancellation_replies.contains(ReplyCode::ok));
    assert(cancellation_replies.contains(ReplyCode::transfer_aborted));
    assert(cancellable_session.transfer == hftp::session::TransferState::idle);
    assert(cancellable_session.data_mode == DataMode::none);
}
