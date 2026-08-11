#include "hftp/rdt/packet.h"
#include "hftp/rdt/crc32.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace hftp::rdt;

void test_serialize_deserialize_happy_path() {
    PacketWire wire;
    Packet p1;
    p1.flags = PacketFlag::data;
    p1.transfer_id = 100;
    p1.sequence = 1;
    p1.acknowledgment = 0;
    
    std::string text = "Hello RDT!";
    for (char c : text) {
        p1.payload.push_back(static_cast<std::byte>(c));
    }

    std::vector<std::byte> serialized;
    auto status1 = wire.serialize(p1, serialized);
    assert(status1.error == hftp::common::Error::none);

    Packet p2;
    auto status2 = wire.deserialize(serialized, p2);
    assert(status2.error == hftp::common::Error::none);

    assert(p2.flags == p1.flags);
    assert(p2.transfer_id == p1.transfer_id);
    assert(p2.sequence == p1.sequence);
    assert(p2.payload_length == p1.payload.size());
    assert(p2.payload == p1.payload);

    std::cout << "[PASS] test_serialize_deserialize_happy_path\n";
}

void test_bad_crc() {
    PacketWire wire;
    Packet p1;
    p1.flags = PacketFlag::data;
    p1.transfer_id = 1;
    p1.sequence = 1;
    p1.payload.push_back(static_cast<std::byte>('A'));

    std::vector<std::byte> serialized;
    auto status1 = wire.serialize(p1, serialized);
    assert(status1.error == hftp::common::Error::none);

    // Cố tình làm hỏng 1 byte payload
    serialized.back() = static_cast<std::byte>('Z');

    Packet p2;
    auto status2 = wire.deserialize(serialized, p2);
    assert(status2.error != hftp::common::Error::none);

    std::cout << "[PASS] test_bad_crc\n";
}

void test_packet_too_small() {
    PacketWire wire;
    std::vector<std::byte> tiny_bytes(5, static_cast<std::byte>(0));

    Packet p;
    auto status = wire.deserialize(tiny_bytes, p);
    assert(status.error != hftp::common::Error::none);

    std::cout << "[PASS] test_packet_too_small\n";
}

void test_serialize_rejects_malformed_without_changing_output() {
    PacketWire wire;
    Packet packet;
    packet.transfer_id = 7;
    packet.flags = PacketFlag::ack;
    packet.payload.push_back(std::byte{'X'});

    const std::vector<std::byte> original{std::byte{0x11}, std::byte{0x22}};
    auto output = original;
    const auto status = wire.serialize(packet, output);
    assert(status.error == hftp::common::Error::invalid_argument);
    assert(output == original);

    packet.flags = PacketFlag::data;
    packet.payload.assign(65536, std::byte{0});
    output = original;
    const auto oversized = wire.serialize(packet, output);
    assert(oversized.error == hftp::common::Error::invalid_argument);
    assert(output == original);

    std::cout << "[PASS] test_serialize_rejects_malformed_without_changing_output\n";
}

void test_header_corruption_is_detected_and_output_is_unchanged() {
    PacketWire wire;
    Packet source;
    source.transfer_id = 9;
    source.sequence = 3;
    source.payload = {std::byte{'A'}, std::byte{'B'}};

    std::vector<std::byte> encoded;
    assert(wire.serialize(source, encoded));
    encoded[9] ^= std::byte{0x01};

    Packet destination;
    destination.transfer_id = 1234;
    const auto status = wire.deserialize(encoded, destination);
    assert(status.error == hftp::common::Error::integrity_error);
    assert(destination.transfer_id == 1234);

    std::cout << "[PASS] test_header_corruption_is_detected_and_output_is_unchanged\n";
}

int main() {
    test_serialize_deserialize_happy_path();
    test_bad_crc();
    test_packet_too_small();
    test_serialize_rejects_malformed_without_changing_output();
    test_header_corruption_is_detected_and_output_is_unchanged();
    std::cout << "===> ALL PACKET TESTS PASSED <===\n";
    return 0;
}
