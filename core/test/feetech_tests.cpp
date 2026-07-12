#include <cmath>
#include <cstdio>
#include <limits>

#include "adapters/feetech.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool bytes_equal(const adapters::FeetechPacket &packet,
                 const unsigned char *expected, std::size_t size)
{
    if(packet.size != size) return false;
    for(std::size_t i = 0; i < size; ++i) {
        if(packet.bytes[i] != expected[i]) return false;
    }
    return true;
}

int check_golden_packets()
{
    using adapters::FeetechInstruction;
    adapters::FeetechPacket packet{};
    const unsigned char ping[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
    if(adapters::encode_instruction(1, FeetechInstruction::ping, nullptr, 0, packet) !=
           rt::ErrorCode::ok ||
       !bytes_equal(packet, ping, sizeof(ping))) {
        return fail("golden ping");
    }
    const unsigned char read_params[] = {0x38, 0x08};
    const unsigned char read[] = {0xFF, 0xFF, 0x02, 0x04, 0x02, 0x38, 0x08, 0xB7};
    adapters::encode_instruction(2, FeetechInstruction::read, read_params, 2, packet);
    if(!bytes_equal(packet, read, sizeof(read))) return fail("golden read");

    const unsigned char write_params[] = {0x21, 0x00};
    const unsigned char write[] = {0xFF, 0xFF, 0x03, 0x04, 0x03, 0x21, 0x00, 0xD4};
    adapters::encode_instruction(3, FeetechInstruction::write, write_params, 2, packet);
    if(!bytes_equal(packet, write, sizeof(write))) return fail("golden write");

    const unsigned char status_params[] = {0x00, 0x08, 0x00, 0x10};
    const unsigned char status[] = {0xFF, 0xFF, 0x03, 0x06, 0x00,
                                    0x00, 0x08, 0x00, 0x10, 0xDE};
    adapters::encode_status(3, 0, status_params, 4, packet);
    if(!bytes_equal(packet, status, sizeof(status))) return fail("golden status");

    adapters::FeetechParser parser;
    adapters::FeetechFrame frame{};
    for(std::size_t i = 0; i < sizeof(status); ++i) parser.push(status[i], frame);
    if(!frame.ready || frame.id != 3 || frame.error != 0 || frame.parameter_count != 4 ||
       frame.parameters[1] != 0x08) {
        return fail("golden status parse");
    }
    unsigned char bad[sizeof(status)] = {};
    for(std::size_t i = 0; i < sizeof(status); ++i) bad[i] = status[i];
    bad[sizeof(status) - 1] ^= 1u;
    frame = {};
    for(unsigned char byte : bad) parser.push(byte, frame);
    if(frame.ready) return fail("bad checksum dropped");

    const unsigned char noise[] = {0x11, 0xFF, 0x22, 0xFF, 0xFF};
    frame = {};
    for(unsigned char byte : noise) parser.push(byte, frame);
    for(std::size_t i = 2; i < sizeof(status); ++i) parser.push(status[i], frame);
    if(!frame.ready || frame.id != 3) return fail("parser resync");

    adapters::FeetechBus bus;
    const unsigned char ids[] = {1, 2};
    if(bus.bind(ids, 2) != rt::ErrorCode::ok) return fail("bus bind");
    adapters::FeetechPacket initialize{};
    const unsigned char initialize_expected[] = {0xFF, 0xFF, 0x01, 0x04,
                                                 0x03, 0x21, 0x00, 0xD6};
    if(bus.build_initialize(0, initialize) != rt::ErrorCode::ok ||
       !bytes_equal(initialize, initialize_expected, sizeof(initialize_expected))) {
        return fail("golden position-mode initialize");
    }
    adapters::ServoSetpoints setpoints[2] = {};
    setpoints[0].position = 0.0;
    setpoints[1].position = 3.14159265358979323846 / 2.0;
    adapters::FeetechPacket write_packet{};
    adapters::FeetechPacket read_packet{};
    if(bus.build_cycle(setpoints, 2, write_packet, read_packet) != rt::ErrorCode::ok ||
       write_packet.bytes[4] != static_cast<unsigned char>(FeetechInstruction::sync_write) ||
       read_packet.bytes[4] != static_cast<unsigned char>(FeetechInstruction::sync_read)) {
        return fail("golden sync packets");
    }
    adapters::FeetechFrame concatenated{};
    adapters::FeetechParser concatenated_parser;
    for(unsigned char byte : ping) concatenated_parser.push(byte, concatenated);
    if(!concatenated.ready) return fail("golden concatenated first");
    concatenated.ready = false;
    for(unsigned char byte : status) concatenated_parser.push(byte, concatenated);
    if(!concatenated.ready || concatenated.id != 3) {
        return fail("golden concatenated second");
    }
    adapters::FeetechPacket invalid{};
    if(adapters::encode_instruction(1, FeetechInstruction::write, nullptr, 1,
                                    invalid) != rt::ErrorCode::invalid_argument ||
       invalid.size != 0) {
        return fail("golden invalid parameters");
    }
    return 0;
}

int check_sign_magnitude()
{
    const int values[] = {0, 1, -1, 2047, -2047, 2048, -2048, 4095, -4095};
    const unsigned int masks[] = {0x8000u, 0x0400u, 0x0800u};
    for(unsigned int mask : masks) {
        for(int value : values) {
            const int maximum = static_cast<int>(mask - 1u);
            if(std::abs(value) > maximum) continue;
            const unsigned int encoded = adapters::encode_sign_magnitude(value, mask);
            if(adapters::decode_sign_magnitude(encoded, mask) != value) {
                return fail("sign magnitude roundtrip");
            }
        }
    }
    return 0;
}

int check_binding_rejections()
{
    adapters::FeetechBus bus;
    unsigned char ids[33] = {};
    for(std::size_t i = 0; i < 33; ++i) ids[i] = static_cast<unsigned char>(i + 1);
    if(bus.bind(ids, 33) != rt::ErrorCode::capacity_exceeded) {
        return fail("capacity rejection");
    }
    if(bus.bind(ids, 32) != rt::ErrorCode::capacity_exceeded) {
        return fail("protocol length rejection");
    }
    const unsigned char duplicate[] = {1, 2, 1};
    if(bus.bind(duplicate, 3) != rt::ErrorCode::invalid_argument) {
        return fail("duplicate rejection");
    }
    adapters::FeetechConfig invalid{};
    invalid.velocity_units_per_radian_second = 0.0;
    if(bus.configure(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("explicit scale rejection");
    }
    return 0;
}

int check_sim_roundtrip_and_timeout()
{
    const unsigned char ids[] = {1, 7};
    adapters::FeetechBus bus;
    adapters::FeetechSim sim;
    if(bus.bind(ids, 2) != rt::ErrorCode::ok || sim.bind(ids, 2) != rt::ErrorCode::ok) {
        return fail("sim bind");
    }
    adapters::FeetechConfig config{};
    config.velocity_units_per_radian_second = 10.0;
    config.acceleration_units_per_radian_second2 = 20.0;
    config.stale_limit_cycles = 3;
    bus.configure(config);
    sim.configure(config);

    adapters::ServoSetpoints setpoints[2] = {};
    setpoints[0] = {0.25, 1.2, 2.3, 99.0};
    setpoints[1] = {-0.5, -0.7, -1.1, -99.0};
    adapters::FeetechPacket tx{};
    adapters::FeetechPacket request{};
    adapters::FeetechPacket responses[2] = {};
    adapters::FeetechServo servo0(bus, 0);
    adapters::FeetechServo servo1(bus, 1);
    servo0.write_setpoints(setpoints[0]);
    servo1.write_setpoints(setpoints[1]);
    if(bus.build_cycle(tx, request) != rt::ErrorCode::ok ||
       sim.exchange(tx, request, responses, 2) != rt::ErrorCode::ok) {
        return fail("sim exchange");
    }
    adapters::FeetechPacket corrupt = tx;
    corrupt.bytes[corrupt.size - 1] ^= 1u;
    if(sim.exchange(corrupt, request, responses, 2) !=
       rt::ErrorCode::invalid_argument) {
        return fail("sim rejects corrupt write");
    }
    bus.begin_feedback_cycle();
    for(const auto &response : responses) {
        for(std::size_t byte = 0; byte < response.size; ++byte) {
            bus.push(response.bytes[byte]);
        }
    }
    bus.end_feedback_cycle();
    for(std::size_t i = 0; i < 2; ++i) {
        adapters::ServoFeedback feedback{};
        bus.read_feedback(i, feedback);
        if(std::fabs(feedback.position - setpoints[i].position) >
               adapters::FeetechConfig::RadiansPerCount ||
           !feedback.info.communication_ready || bus.stale(i)) {
            return fail("sim SI roundtrip");
        }
    }
    const unsigned char diagnostic_params[] = {0x00, 0x08, 0, 0, 0, 0, 120, 25};
    adapters::FeetechPacket diagnostic{};
    adapters::encode_status(1, 0xA5, diagnostic_params, 8, diagnostic);
    bus.begin_feedback_cycle();
    bus.consume(diagnostic);
    bus.end_feedback_cycle();
    if(bus.raw_status(0) != 0xA5) return fail("raw status preserved");

    adapters::ServoFeedback frozen{};
    bus.read_feedback(0, frozen);
    for(int cycle = 0; cycle < 2; ++cycle) {
        bus.begin_feedback_cycle();
        bus.end_feedback_cycle();
        adapters::ServoFeedback feedback{};
        bus.read_feedback(0, feedback);
        if(!feedback.info.communication_ready || !bus.stale(0) ||
           feedback.position != frozen.position) {
            return fail("M-1 timeout freeze");
        }
    }
    bus.begin_feedback_cycle();
    bus.end_feedback_cycle();
    adapters::ServoFeedback lost{};
    bus.read_feedback(0, lost);
    if(lost.info.communication_ready) return fail("M timeout flips readiness");
    return 0;
}

int check_budget()
{
    static_assert(adapters::FeetechBus::cycle_wire_bytes(16) == 384);
    static_assert(adapters::FeetechBus::cycle_wire_bytes(31) == 729);
    if(adapters::FeetechBus::utilization_percent(16, 50) != 19.2 ||
       adapters::FeetechBus::utilization_percent(16, 100) != 38.4 ||
       adapters::FeetechBus::utilization_percent(31, 50) != 36.45) {
        return fail("wire budget");
    }
    return 0;
}

int check_extreme_setpoints_are_bounded()
{
    const unsigned char id[] = {1};
    adapters::FeetechBus bus;
    bus.bind(id, 1);
    adapters::ServoSetpoints values[] = {
        {1.0e300, 1.0e300, 1.0e300, 0.0},
        {std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::quiet_NaN(),
         -std::numeric_limits<double>::infinity(), 0.0}};
    adapters::FeetechPacket write{};
    adapters::FeetechPacket read{};
    for(const auto &value : values) {
        if(bus.build_cycle(&value, 1, write, read) != rt::ErrorCode::ok ||
           write.size != 16 || read.size != 9) {
            return fail("extreme setpoint bounded encoding");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if(check_golden_packets() != 0 || check_sign_magnitude() != 0 ||
       check_binding_rejections() != 0 || check_sim_roundtrip_and_timeout() != 0 ||
       check_budget() != 0 || check_extreme_setpoints_are_bounded() != 0) {
        return 1;
    }
    std::printf("PASS Feetech STS tests\n");
    return 0;
}
