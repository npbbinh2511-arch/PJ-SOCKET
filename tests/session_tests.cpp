#include "hftp/session/session.h"

#include <cassert>
#include <filesystem>

using hftp::session::AuthState;
using hftp::session::DataMode;
using hftp::session::Session;
using hftp::session::TransferState;
using hftp::session::TransferType;
using hftp::session::UdpEndpoint;

int main() {
    Session initial(7);
    assert(initial.id == 7);
    assert(initial.username.empty());
    assert(initial.auth == AuthState::unauthenticated);
    assert(initial.current_directory == std::filesystem::path("/"));
    assert(initial.type == TransferType::ascii);
    assert(initial.data_mode == DataMode::none);
    assert(!initial.active_endpoint.has_value());
    assert(!initial.passive_port.has_value());
    assert(!initial.rename_from.has_value());
    assert(initial.transfer == TransferState::idle);
    assert(initial.transfer_id == 0);
    assert(!initial.cancel_requested.load());

    assert(!initial.prepare_transfer(0));
    assert(initial.prepare_transfer(42));
    assert(initial.transfer == TransferState::preparing);
    assert(initial.transfer_id == 42);
    assert(!initial.prepare_transfer(43));
    assert(initial.mark_transfer_running());
    assert(initial.transfer == TransferState::running);
    assert(!initial.mark_transfer_running());
    assert(initial.request_cancellation());
    assert(initial.transfer == TransferState::cancelling);
    assert(initial.cancel_requested.load());
    assert(!initial.request_cancellation());
    initial.finish_transfer();
    assert(initial.transfer == TransferState::idle);
    assert(initial.transfer_id == 0);
    assert(!initial.cancel_requested.load());

    Session first(1);
    Session second(2);
    first.username = "alice";
    first.auth = AuthState::authenticated;
    first.current_directory = "/alice";
    first.type = TransferType::binary;
    first.data_mode = DataMode::active;
    first.active_endpoint = UdpEndpoint{"127.0.0.1", 50000};
    first.rename_from = "/alice/old.txt";
    assert(first.prepare_transfer(100));

    assert(second.id == 2);
    assert(second.username.empty());
    assert(second.auth == AuthState::unauthenticated);
    assert(second.current_directory == std::filesystem::path("/"));
    assert(second.type == TransferType::ascii);
    assert(second.data_mode == DataMode::none);
    assert(!second.active_endpoint.has_value());
    assert(!second.rename_from.has_value());
    assert(second.transfer == TransferState::idle);

    first.reset_authentication();
    first.reset_data_channel();
    first.clear_rename();
    assert(first.username.empty());
    assert(first.auth == AuthState::unauthenticated);
    assert(first.data_mode == DataMode::none);
    assert(!first.active_endpoint.has_value());
    assert(!first.passive_port.has_value());
    assert(!first.rename_from.has_value());
}
