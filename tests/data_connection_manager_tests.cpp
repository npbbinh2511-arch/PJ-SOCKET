#include "hftp/network/socket.h"
#include "hftp/transfer/data_connection_manager.h"

#include <cassert>

using hftp::common::Error;
using hftp::network::SocketRuntime;
using hftp::session::DataMode;
using hftp::session::Session;
using hftp::transfer::DataConnectionManager;

int main() {
    SocketRuntime runtime;
    DataConnectionManager manager;

    const auto valid = manager.parse_port_argument("127,0,0,1,195,80");
    assert(valid.status);
    assert(valid.endpoint.address == "127.0.0.1");
    assert(valid.endpoint.port == 50000);

    assert(!manager.parse_port_argument("127,0,0,1,1").status);
    assert(!manager.parse_port_argument("127,0,0,1,1,2,3").status);
    assert(!manager.parse_port_argument("127,0,0,1,256,1").status);
    assert(!manager.parse_port_argument("127,0,0,1,x,1").status);
    assert(!manager.parse_port_argument("0,0,0,0,1,1").status);
    assert(!manager.parse_port_argument("255,255,255,255,1,1").status);
    assert(!manager.parse_port_argument("224,0,0,1,1,1").status);
    assert(!manager.parse_port_argument("127,0,0,1,0,0").status);

    Session session(10);
    session.control_peer_address = "127.0.0.1";
    const auto malformed = manager.set_active(session, "127,0,0,1,999,1");
    assert(!malformed);
    assert(malformed.error == Error::invalid_argument);
    assert(session.data_mode == DataMode::none);

    assert(manager.open_passive(session));
    assert(session.data_mode == DataMode::passive);
    assert(session.passive_port.has_value());
    assert(*session.passive_port != 0);
    assert(!session.active_endpoint.has_value());
    auto passive_lease = manager.acquire_passive_socket(session.id);
    assert(passive_lease && passive_lease->valid());

    assert(manager.set_active(session, "127,0,0,1,195,80"));
    assert(session.data_mode == DataMode::active);
    assert(session.active_endpoint.has_value());
    assert(session.active_endpoint->port == 50000);
    assert(!session.passive_port.has_value());
    assert(passive_lease->valid());

    assert(!manager.set_active(session, "127,0,0,2,195,80"));
    assert(session.data_mode == DataMode::active);

    assert(session.prepare_transfer(99));
    const auto active_while_busy =
        manager.set_active(session, "127,0,0,1,195,81");
    assert(!active_while_busy);
    assert(active_while_busy.error == Error::busy);
    const auto passive_while_busy = manager.open_passive(session);
    assert(!passive_while_busy);
    assert(passive_while_busy.error == Error::busy);
    session.finish_transfer();

    manager.reset(session);
    assert(session.data_mode == DataMode::none);
    assert(!session.active_endpoint.has_value());
    assert(!session.passive_port.has_value());

    DataConnectionManager invalid_passive_address("not-an-ipv4-address");
    Session invalid_passive_session(11);
    const auto invalid_passive =
        invalid_passive_address.open_passive(invalid_passive_session);
    assert(!invalid_passive);
    assert(invalid_passive.error == Error::invalid_argument);
}
