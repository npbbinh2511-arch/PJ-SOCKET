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
    const auto malformed = manager.set_active(session, "127,0,0,1,999,1");
    assert(!malformed);
    assert(malformed.error == Error::invalid_argument);
    assert(session.data_mode == DataMode::none);

    assert(manager.open_passive(session));
    assert(session.data_mode == DataMode::passive);
    assert(session.passive_port.has_value());
    assert(*session.passive_port != 0);
    assert(!session.active_endpoint.has_value());

    assert(manager.set_active(session, "127,0,0,1,195,80"));
    assert(session.data_mode == DataMode::active);
    assert(session.active_endpoint.has_value());
    assert(session.active_endpoint->port == 50000);
    assert(!session.passive_port.has_value());

    manager.reset(session);
    assert(session.data_mode == DataMode::none);
    assert(!session.active_endpoint.has_value());
    assert(!session.passive_port.has_value());
}
