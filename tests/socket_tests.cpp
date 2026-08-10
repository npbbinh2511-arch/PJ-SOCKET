#include "hftp/network/socket.h"

#include <cassert>
#include <type_traits>
#include <utility>

using hftp::network::Socket;
using hftp::network::NativeSocket;
using hftp::network::SocketRuntime;

int main() {
    SocketRuntime runtime;

    static_assert(!std::is_copy_constructible_v<Socket>);
    static_assert(!std::is_copy_assignable_v<Socket>);
    static_assert(std::is_nothrow_move_constructible_v<Socket>);
    static_assert(std::is_nothrow_move_assignable_v<Socket>);

    Socket empty;
    assert(!empty.valid());

    const auto fake_handle = static_cast<NativeSocket>(42);
    Socket source(fake_handle);
    Socket destination(std::move(source));
    assert(!source.valid());
    assert(destination.native_handle() == fake_handle);

    const NativeSocket released = destination.release();
    assert(released == fake_handle);
    assert(!destination.valid());
    destination.close();
}
