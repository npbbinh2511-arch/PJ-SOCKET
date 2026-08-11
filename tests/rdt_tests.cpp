#include "hftp/rdt/reliable_transport.h"
#include "hftp/transfer/transfer.h"
#include "hftp/network/socket.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>

using namespace hftp;

void test_stop_and_wait_loopback() {
    std::printf("[1/4] Preparing test payload...\n");
    std::fflush(stdout);

    uint16_t test_port = 54321;
    std::string test_payload = "Data transfer test using Stop-and-Wait UDP Engine!";
    std::vector<std::byte> send_data;
    for (char c : test_payload) {
        send_data.push_back(static_cast<std::byte>(c));
    }

    std::vector<std::byte> recv_data;

    transfer::TransferContext recv_ctx{};
    recv_ctx.transfer_id = 1;
    recv_ctx.endpoint = session::UdpEndpoint{"127.0.0.1", test_port};

    transfer::TransferContext send_ctx{};
    send_ctx.transfer_id = 1;
    send_ctx.endpoint = session::UdpEndpoint{"127.0.0.1", test_port};

    std::printf("[2/4] Launching Receiver thread on port %u...\n", test_port);
    std::fflush(stdout);

    std::thread receiver_thread([&]() {
        rdt::StopAndWaitOptions options;
        options.timeout = std::chrono::milliseconds(1000);
        auto receiver = rdt::create_stop_and_wait_transport(options);

        auto status = receiver->receive(recv_ctx, recv_data);
        if (status.error != common::Error::none) {
            std::printf("Receiver error: %s\n", status.message.c_str());
            std::fflush(stdout);
        }
        assert(status.error == common::Error::none);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::printf("[3/4] Sender transmitting data via UDP...\n");
    std::fflush(stdout);

    rdt::StopAndWaitOptions options;
    options.timeout = std::chrono::milliseconds(500);
    auto sender = rdt::create_stop_and_wait_transport(options);

    auto send_status = sender->send(send_ctx, send_data);
    if (send_status.error != common::Error::none) {
        std::printf("Sender error: %s\n", send_status.message.c_str());
        std::fflush(stdout);
    }
    assert(send_status.error == common::Error::none);

    std::printf("[4/4] Joining Receiver thread...\n");
    std::fflush(stdout);

    receiver_thread.join();

    assert(recv_data.size() == send_data.size());
    assert(recv_data == send_data);

    std::printf("[PASS] Loopback test transferred %zu bytes successfully!\n", recv_data.size());
    std::fflush(stdout);
}

int main() {
    std::printf("=== RDT ENGINE UNIT TEST STARTED ===\n");
    std::fflush(stdout);

    network::SocketRuntime runtime;

    test_stop_and_wait_loopback();

    std::printf("===> ALL RDT TESTS PASSED <===\n");
    std::fflush(stdout);
    return 0;
}