#include "hftp/session/session.h"

#include <mutex>

namespace hftp::session {

Session::Session(std::uint64_t session_id)
    : id(session_id) {}

void Session::reset_authentication() {
    const std::scoped_lock lock(mutex);
    username.clear();
    auth = AuthState::unauthenticated;
}

void Session::reset_data_channel() {
    const std::scoped_lock lock(mutex);
    data_mode = DataMode::none;
    active_endpoint.reset();
    passive_port.reset();
}

void Session::clear_rename() {
    const std::scoped_lock lock(mutex);
    rename_from.reset();
}

bool Session::prepare_transfer(std::uint64_t new_transfer_id) {
    const std::scoped_lock lock(mutex);
    if (transfer != TransferState::idle || new_transfer_id == 0) {
        return false;
    }

    transfer = TransferState::preparing;
    transfer_id = new_transfer_id;
    cancel_requested.store(false, std::memory_order_release);
    return true;
}

bool Session::mark_transfer_running() {
    const std::scoped_lock lock(mutex);
    if (transfer != TransferState::preparing) {
        return false;
    }

    transfer = TransferState::running;
    return true;
}

bool Session::request_cancellation() {
    const std::scoped_lock lock(mutex);
    if (transfer != TransferState::preparing && transfer != TransferState::running) {
        return false;
    }

    transfer = TransferState::cancelling;
    cancel_requested.store(true, std::memory_order_release);
    return true;
}

void Session::finish_transfer() {
    const std::scoped_lock lock(mutex);
    transfer = TransferState::idle;
    transfer_id = 0;
    cancel_requested.store(false, std::memory_order_release);
}

} // namespace hftp::session
