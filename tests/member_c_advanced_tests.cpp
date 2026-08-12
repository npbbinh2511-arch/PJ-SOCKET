#include "hftp/control/command_dispatcher.h"
#include "hftp/network/socket.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hftp::common::Error;
using hftp::common::Status;
using hftp::control::Authenticator;
using hftp::control::CommandDispatcher;
using hftp::control::CredentialStore;
using hftp::control::ReplySink;
using hftp::filesystem::Entry;
using hftp::filesystem::FileMetadata;
using hftp::filesystem::FileRepository;
using hftp::protocol::Command;
using hftp::protocol::ReplyCode;
using hftp::session::Session;
using hftp::transfer::DataConnectionManager;
using hftp::transfer::Operation;
using hftp::transfer::TransferContext;
using hftp::transfer::TransferCoordinator;

class Credentials final : public CredentialStore {
public:
    bool verify(std::string_view username,
                std::string_view password) const override {
        return username == "student" && password == "socket";
    }
};

class Repository final : public FileRepository {
public:
    Status resolve_safe(const std::filesystem::path& cwd,
                        const std::filesystem::path& requested,
                        std::filesystem::path& resolved) const override {
        if (requested == "missing") {
            return {Error::not_found, "Missing path"};
        }
        resolved = (cwd / requested).lexically_normal();
        return {};
    }

    Status list_file(const std::filesystem::path& path,
                     std::vector<Entry>& entries) const override {
        entries.clear();
        if (path == "/missing") {
            return {Error::not_found, "Missing directory"};
        }
        entries.push_back({"alpha.txt", false, 5,
                           std::filesystem::file_time_type::clock::now(),
                           std::filesystem::perms::owner_read});
        entries.push_back({"folder", true, 0,
                           std::filesystem::file_time_type::clock::now(),
                           std::filesystem::perms::owner_all});
        return {};
    }

    Status read_file(const std::filesystem::path&,
                     std::vector<std::uint8_t>&) const override {
        return {Error::not_found, "Not used"};
    }
    Status write_file(const std::filesystem::path&,
                      const std::vector<std::uint8_t>&) const override {
        return {};
    }
    Status append_file(const std::filesystem::path&,
                       const std::vector<std::uint8_t>&) const override {
        return {};
    }
    Status write_unique(const std::filesystem::path&,
                        const std::vector<std::uint8_t>&,
                        std::filesystem::path& created_name) const override {
        created_name = "upload_42.bin";
        return {};
    }
    Status metadata(const std::filesystem::path& path,
                    FileMetadata& metadata) const override {
        if (path == "/missing") {
            return {Error::not_found, "Missing path"};
        }
        metadata.directory = path.filename() == "folder";
        metadata.size = metadata.directory ? 0 : 5;
        metadata.modified = std::filesystem::file_time_type::clock::now();
        metadata.permissions = std::filesystem::perms::owner_read;
        return {};
    }
    Status make_directory(const std::filesystem::path&) const override { return {}; }
    Status remove_directory(const std::filesystem::path&) const override { return {}; }
    Status remove_file(const std::filesystem::path&) const override { return {}; }
    Status rename_entry(const std::filesystem::path&,
                        const std::filesystem::path&) const override { return {}; }
    Status sha256_file(const std::filesystem::path&,
                       std::string& digest) const override {
        digest.assign(64, 'a');
        return {};
    }
};

class Coordinator final : public TransferCoordinator {
public:
    Status start(const TransferContext& context) override {
        const std::scoped_lock lock(mutex_);
        contexts_.push_back(context);
        return {Error::none, "Transfer complete bytes=5 sha256=test"};
    }
    void request_cancel(std::uint64_t) override {}

    TransferContext last() const {
        const std::scoped_lock lock(mutex_);
        assert(!contexts_.empty());
        return contexts_.back();
    }

private:
    mutable std::mutex mutex_;
    std::vector<TransferContext> contexts_;
};

class Replies final : public ReplySink {
public:
    void send(ReplyCode code, std::string text) override {
        const std::scoped_lock lock(mutex_);
        values_.emplace_back(code, std::move(text));
    }

    std::pair<ReplyCode, std::string> last() const {
        const std::scoped_lock lock(mutex_);
        assert(!values_.empty());
        return values_.back();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::pair<ReplyCode, std::string>> values_;
};

void issue(CommandDispatcher& dispatcher, const Command& command,
           Session& session, Replies& replies) {
    static_cast<void>(dispatcher.dispatch(command, session, replies));
}

void login(CommandDispatcher& dispatcher, Session& session, Replies& replies) {
    session.control_peer_address = "127.0.0.1";
    issue(dispatcher, Command{"USER", "student"}, session, replies);
    issue(dispatcher, Command{"PASS", "socket"}, session, replies);
    assert(replies.last().first == ReplyCode::logged_in);
}

void select_active(CommandDispatcher& dispatcher, Session& session,
                   Replies& replies) {
    issue(dispatcher, Command{"PORT", "127,0,0,1,195,80"}, session, replies);
    assert(replies.last().first == ReplyCode::ok);
}

void finish_transfer(CommandDispatcher& dispatcher, Session& session) {
    dispatcher.end_session(session);
}

std::string payload_text(const TransferContext& context) {
    std::string text;
    text.reserve(context.payload.size());
    for (const auto value : context.payload) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return text;
}

} // namespace

int main() {
    hftp::network::SocketRuntime runtime;
    Credentials credentials;
    Authenticator authenticator(credentials);
    Repository repository;
    DataConnectionManager data;
    Coordinator transfers;
    CommandDispatcher dispatcher(authenticator, repository, data, transfers);
    Session session(77);
    Replies replies;
    login(dispatcher, session, replies);

    issue(dispatcher, Command{"HELP", "LIST"}, session, replies);
    assert(replies.last() == std::make_pair(ReplyCode::help,
                                             std::string("LIST [path]")));
    issue(dispatcher, Command{"HELP", "UNKNOWN"}, session, replies);
    assert(replies.last().first == ReplyCode::parameter_error);

    issue(dispatcher, Command{"STAT", ""}, session, replies);
    assert(replies.last().first == ReplyCode::status);
    assert(replies.last().second.find("session=77") != std::string::npos);
    issue(dispatcher, Command{"STAT", "alpha.txt"}, session, replies);
    assert(replies.last().first == ReplyCode::file_status);
    assert(replies.last().second.find("size=5") != std::string::npos);

    issue(dispatcher, Command{"LIST", ""}, session, replies);
    assert(replies.last().first == ReplyCode::cannot_open_data);
    select_active(dispatcher, session, replies);
    issue(dispatcher, Command{"LIST", ""}, session, replies);
    finish_transfer(dispatcher, session);
    auto context = transfers.last();
    assert(context.operation == Operation::detailed_list);
    assert(context.direction == hftp::transfer::Direction::download);
    assert(payload_text(context).find("alpha.txt\r\n") != std::string::npos);

    select_active(dispatcher, session, replies);
    issue(dispatcher, Command{"NLST", "/"}, session, replies);
    finish_transfer(dispatcher, session);
    context = transfers.last();
    assert(context.operation == Operation::name_list);
    assert(payload_text(context) == "alpha.txt\r\nfolder\r\n");

    select_active(dispatcher, session, replies);
    issue(dispatcher, Command{"APPE", "journal.txt"}, session, replies);
    finish_transfer(dispatcher, session);
    context = transfers.last();
    assert(context.operation == Operation::append);
    assert(context.direction == hftp::transfer::Direction::upload);

    issue(dispatcher, Command{"STOU", "forbidden-name"}, session, replies);
    assert(replies.last().first == ReplyCode::parameter_error);
    select_active(dispatcher, session, replies);
    issue(dispatcher, Command{"STOU", ""}, session, replies);
    finish_transfer(dispatcher, session);
    context = transfers.last();
    assert(context.operation == Operation::store_unique);
    assert(context.result_name == "/upload_42.bin");
    assert(context.remove_target_on_failure);

    return 0;
}
