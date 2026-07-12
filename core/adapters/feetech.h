#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "adapters/servo.h"
#include "rt/error.h"

namespace plcopen::core::adapters
{

class FeetechBus;

enum class FeetechInstruction : std::uint8_t
{
    ping = 0x01,
    read = 0x02,
    write = 0x03,
    sync_read = 0x82,
    sync_write = 0x83,
};

struct FeetechPacket
{
    static constexpr std::size_t Capacity = 272;
    std::uint8_t bytes[Capacity] = {};
    std::size_t size = 0;
};

struct FeetechFrame
{
    std::uint8_t id = 0;
    std::uint8_t error = 0;
    std::uint8_t parameters[253] = {};
    std::size_t parameter_count = 0;
    bool ready = false;
};

inline std::uint8_t feetech_checksum(const std::uint8_t *bytes,
                                     std::size_t begin, std::size_t end)
{
    unsigned int sum = 0;
    for(std::size_t i = begin; i < end; ++i) sum += bytes[i];
    return static_cast<std::uint8_t>(~sum);
}

inline rt::ErrorCode encode_instruction(std::uint8_t id,
                                        FeetechInstruction instruction,
                                        const std::uint8_t *parameters,
                                        std::size_t parameter_count,
                                        FeetechPacket &packet)
{
    if(id == 0xFF || parameter_count > 253 ||
       (parameter_count != 0 && parameters == nullptr)) {
        packet = {};
        return rt::ErrorCode::invalid_argument;
    }
    packet = {};
    packet.size = parameter_count + 6;
    packet.bytes[0] = 0xFF;
    packet.bytes[1] = 0xFF;
    packet.bytes[2] = id;
    packet.bytes[3] = static_cast<std::uint8_t>(parameter_count + 2);
    packet.bytes[4] = static_cast<std::uint8_t>(instruction);
    for(std::size_t i = 0; i < parameter_count; ++i) {
        packet.bytes[5 + i] = parameters[i];
    }
    packet.bytes[packet.size - 1] = feetech_checksum(packet.bytes, 2, packet.size - 1);
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode encode_status(std::uint8_t id, std::uint8_t error,
                                   const std::uint8_t *parameters,
                                   std::size_t parameter_count,
                                   FeetechPacket &packet)
{
    if(id == 0xFF || parameter_count > 253 ||
       (parameter_count != 0 && parameters == nullptr)) {
        packet = {};
        return rt::ErrorCode::invalid_argument;
    }
    packet = {};
    packet.size = parameter_count + 6;
    packet.bytes[0] = 0xFF;
    packet.bytes[1] = 0xFF;
    packet.bytes[2] = id;
    packet.bytes[3] = static_cast<std::uint8_t>(parameter_count + 2);
    packet.bytes[4] = error;
    for(std::size_t i = 0; i < parameter_count; ++i) {
        packet.bytes[5 + i] = parameters[i];
    }
    packet.bytes[packet.size - 1] = feetech_checksum(packet.bytes, 2, packet.size - 1);
    return rt::ErrorCode::ok;
}

class FeetechParser
{
public:
    void push(std::uint8_t byte, FeetechFrame &frame)
    {
        frame.ready = false;
        if(size_ == 0) {
            if(byte == 0xFF) buffer_[size_++] = byte;
            return;
        }
        if(size_ == 1) {
            if(byte == 0xFF) buffer_[size_++] = byte;
            else size_ = 0;
            return;
        }
        if(size_ >= FeetechPacket::Capacity) {
            reset_with(byte);
            return;
        }
        buffer_[size_++] = byte;
        if(size_ == 4) {
            expected_ = static_cast<std::size_t>(buffer_[3]) + 4;
            if(buffer_[3] < 2 || expected_ > FeetechPacket::Capacity) {
                reset_with(byte);
            }
            return;
        }
        if(expected_ == 0 || size_ < expected_) return;
        if(feetech_checksum(buffer_, 2, expected_ - 1) == buffer_[expected_ - 1]) {
            frame.id = buffer_[2];
            frame.error = buffer_[4];
            frame.parameter_count = static_cast<std::size_t>(buffer_[3]) - 2;
            for(std::size_t i = 0; i < frame.parameter_count; ++i) {
                frame.parameters[i] = buffer_[5 + i];
            }
            frame.ready = true;
        }
        size_ = 0;
        expected_ = 0;
    }

private:
    void reset_with(std::uint8_t byte)
    {
        size_ = 0;
        expected_ = 0;
        if(byte == 0xFF) buffer_[size_++] = byte;
    }

    std::uint8_t buffer_[FeetechPacket::Capacity] = {};
    std::size_t size_ = 0;
    std::size_t expected_ = 0;
};

inline std::uint16_t encode_sign_magnitude(int value, std::uint16_t sign_mask)
{
    const std::int64_t wide = value;
    const unsigned int magnitude = static_cast<unsigned int>(wide < 0 ? -wide : wide);
    return static_cast<std::uint16_t>((value < 0 ? sign_mask : 0u) |
                                      (magnitude & (sign_mask - 1u)));
}

inline int decode_sign_magnitude(std::uint16_t value, std::uint16_t sign_mask)
{
    const int magnitude = static_cast<int>(value & (sign_mask - 1u));
    return (value & sign_mask) != 0 ? -magnitude : magnitude;
}

struct FeetechConfig
{
    static constexpr double Pi = 3.14159265358979323846;
    static constexpr double RadiansPerCount = 2.0 * Pi / 4096.0;

    double velocity_units_per_radian_second = 1.0;
    double acceleration_units_per_radian_second2 = 1.0;
    std::size_t stale_limit_cycles = 5;
};

class FeetechBus
{
public:
    static constexpr std::size_t StorageCapacity = 32;
    static constexpr std::size_t ProtocolCycleCapacity = 31;

    rt::ErrorCode configure(const FeetechConfig &config)
    {
        if(!std::isfinite(config.velocity_units_per_radian_second) ||
           config.velocity_units_per_radian_second <= 0.0 ||
           !std::isfinite(config.acceleration_units_per_radian_second2) ||
           config.acceleration_units_per_radian_second2 <= 0.0 ||
           config.stale_limit_cycles == 0) {
            return rt::ErrorCode::invalid_argument;
        }
        config_ = config;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode bind(const std::uint8_t *ids, std::size_t count)
    {
        if(count == 0 || ids == nullptr) return rt::ErrorCode::invalid_argument;
        if(count > StorageCapacity || count > ProtocolCycleCapacity) {
            return rt::ErrorCode::capacity_exceeded;
        }
        for(std::size_t i = 0; i < count; ++i) {
            if(ids[i] == 0 || ids[i] == 0xFE || ids[i] == 0xFF) {
                return rt::ErrorCode::invalid_argument;
            }
            for(std::size_t j = 0; j < i; ++j) {
                if(ids[i] == ids[j]) return rt::ErrorCode::invalid_argument;
            }
        }
        count_ = count;
        for(std::size_t i = 0; i < count_; ++i) {
            ids_[i] = ids[i];
            feedback_[i] = {};
            feedback_[i].info.communication_ready = false;
            stale_cycles_[i] = 0;
            received_[i] = false;
            raw_status_[i] = 0;
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode build_cycle(const ServoSetpoints *setpoints, std::size_t count,
                              FeetechPacket &sync_write,
                              FeetechPacket &sync_read)
    {
        if(count != count_ || setpoints == nullptr) return rt::ErrorCode::invalid_argument;
        std::uint8_t write_parameters[2 + ProtocolCycleCapacity * 8] = {};
        write_parameters[0] = 41;
        write_parameters[1] = 7;
        std::size_t cursor = 2;
        for(std::size_t i = 0; i < count_; ++i) {
            const std::uint16_t position_counts = wrap_units(
                setpoints[i].position / FeetechConfig::RadiansPerCount + 2048.0,
                65536u);
            const int velocity_units = static_cast<int>(wrap_units(
                std::fabs(setpoints[i].velocity) *
                    config_.velocity_units_per_radian_second,
                32768u));
            const std::uint8_t acceleration_units = static_cast<std::uint8_t>(
                wrap_units(std::fabs(setpoints[i].acceleration) *
                               config_.acceleration_units_per_radian_second2,
                           256u));
            write_parameters[cursor++] = ids_[i];
            write_parameters[cursor++] = static_cast<std::uint8_t>(acceleration_units);
            write_parameters[cursor++] = static_cast<std::uint8_t>(position_counts);
            write_parameters[cursor++] = static_cast<std::uint8_t>(position_counts >> 8);
            write_parameters[cursor++] = 0;
            write_parameters[cursor++] = 0;
            const std::uint16_t velocity = encode_sign_magnitude(
                setpoints[i].velocity < 0.0 ? -velocity_units : velocity_units, 0x8000u);
            write_parameters[cursor++] = static_cast<std::uint8_t>(velocity);
            write_parameters[cursor++] = static_cast<std::uint8_t>(velocity >> 8);
        }
        rt::ErrorCode result = encode_instruction(
            0xFE, FeetechInstruction::sync_write, write_parameters, cursor, sync_write);
        if(result != rt::ErrorCode::ok) return result;

        std::uint8_t read_parameters[2 + ProtocolCycleCapacity] = {};
        read_parameters[0] = 56;
        read_parameters[1] = 8;
        for(std::size_t i = 0; i < count_; ++i) read_parameters[2 + i] = ids_[i];
        return encode_instruction(0xFE, FeetechInstruction::sync_read,
                                  read_parameters, count_ + 2, sync_read);
    }

    rt::ErrorCode build_cycle(FeetechPacket &sync_write,
                              FeetechPacket &sync_read)
    {
        return build_cycle(setpoints_, count_, sync_write, sync_read);
    }

    rt::ErrorCode build_initialize(std::size_t index, FeetechPacket &packet) const
    {
        if(index >= count_) return rt::ErrorCode::out_of_range;
        const std::uint8_t parameters[] = {33, 0};
        return encode_instruction(ids_[index], FeetechInstruction::write,
                                  parameters, 2, packet);
    }

    void latch(std::size_t index, const ServoSetpoints &setpoints)
    {
        if(index >= count_) return;
        setpoints_[index] = setpoints;
        setpoints_[index].torque = 0.0;
    }

    void begin_feedback_cycle()
    {
        for(std::size_t i = 0; i < count_; ++i) received_[i] = false;
    }

    void consume(const FeetechPacket &packet)
    {
        FeetechParser parser;
        FeetechFrame frame{};
        for(std::size_t i = 0; i < packet.size; ++i) parser.push(packet.bytes[i], frame);
        if(!frame.ready || frame.parameter_count != 8) return;
        consume(frame);
    }

    void push(std::uint8_t byte)
    {
        FeetechFrame frame{};
        parser_.push(byte, frame);
        if(frame.ready) consume(frame);
    }

    std::size_t count() const { return count_; }

private:
    void consume(const FeetechFrame &frame)
    {
        if(frame.parameter_count != 8) return;
        const std::size_t index = find(frame.id);
        if(index >= count_) return;
        const std::uint16_t position =
            static_cast<std::uint16_t>(frame.parameters[0] |
                                       (static_cast<std::uint16_t>(frame.parameters[1]) << 8));
        const std::uint16_t velocity =
            static_cast<std::uint16_t>(frame.parameters[2] |
                                       (static_cast<std::uint16_t>(frame.parameters[3]) << 8));
        const std::uint16_t load =
            static_cast<std::uint16_t>(frame.parameters[4] |
                                       (static_cast<std::uint16_t>(frame.parameters[5]) << 8));
        feedback_[index].position =
            (static_cast<int>(position) - 2048) * FeetechConfig::RadiansPerCount;
        feedback_[index].velocity =
            decode_sign_magnitude(velocity, 0x8000u) /
            config_.velocity_units_per_radian_second;
        feedback_[index].acceleration = 0.0;
        feedback_[index].torque = decode_sign_magnitude(load, 0x0400u) / 1023.0;
        feedback_[index].info.communication_ready = true;
        raw_status_[index] = frame.error;
        received_[index] = true;
        stale_cycles_[index] = 0;
    }

public:

    void end_feedback_cycle()
    {
        for(std::size_t i = 0; i < count_; ++i) {
            if(received_[i]) continue;
            if(stale_cycles_[i] < config_.stale_limit_cycles) ++stale_cycles_[i];
            if(stale_cycles_[i] >= config_.stale_limit_cycles) {
                feedback_[i].info.communication_ready = false;
            }
        }
    }

    void read_feedback(std::size_t index, ServoFeedback &feedback) const
    {
        feedback = index < count_ ? feedback_[index] : ServoFeedback{};
        if(index >= count_) feedback.info.communication_ready = false;
    }

    bool stale(std::size_t index) const
    {
        return index < count_ && stale_cycles_[index] != 0;
    }

    std::uint8_t raw_status(std::size_t index) const
    {
        return index < count_ ? raw_status_[index] : 0;
    }

    static constexpr std::size_t cycle_wire_bytes(std::size_t count)
    {
        return 16 + 23 * count;
    }

    static constexpr double utilization_percent(std::size_t count,
                                                 std::size_t frequency_hz)
    {
        return static_cast<double>(cycle_wire_bytes(count) * 10 * frequency_hz) /
               10000.0;
    }

private:
    static std::uint16_t wrap_units(double value, unsigned int modulus)
    {
        if(!std::isfinite(value)) return 0;
        double wrapped = std::fmod(std::round(value), static_cast<double>(modulus));
        if(wrapped < 0.0) wrapped += static_cast<double>(modulus);
        return static_cast<std::uint16_t>(wrapped);
    }

    std::size_t find(std::uint8_t id) const
    {
        for(std::size_t i = 0; i < count_; ++i) {
            if(ids_[i] == id) return i;
        }
        return count_;
    }

    FeetechConfig config_{};
    std::uint8_t ids_[StorageCapacity] = {};
    ServoFeedback feedback_[StorageCapacity] = {};
    std::size_t stale_cycles_[StorageCapacity] = {};
    bool received_[StorageCapacity] = {};
    std::uint8_t raw_status_[StorageCapacity] = {};
    ServoSetpoints setpoints_[StorageCapacity] = {};
    FeetechParser parser_{};
    std::size_t count_ = 0;
};

class FeetechServo final : public Servo
{
public:
    FeetechServo(FeetechBus &bus, std::size_t index)
        : bus_(&bus)
        , index_(index)
    {
    }

    void write_setpoints(const ServoSetpoints &setpoints) override
    {
        setpoints_ = setpoints;
        setpoints_.torque = 0.0;
        bus_->latch(index_, setpoints_);
    }

    void read_feedback(ServoFeedback &feedback) override
    {
        bus_->read_feedback(index_, feedback);
    }

    const ServoSetpoints &setpoints() const { return setpoints_; }

private:
    FeetechBus *bus_ = nullptr;
    std::size_t index_ = 0;
    ServoSetpoints setpoints_{};
};

class FeetechSim
{
public:
    rt::ErrorCode configure(const FeetechConfig &config)
    {
        if(!std::isfinite(config.velocity_units_per_radian_second) ||
           config.velocity_units_per_radian_second <= 0.0 ||
           !std::isfinite(config.acceleration_units_per_radian_second2) ||
           config.acceleration_units_per_radian_second2 <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        config_ = config;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode bind(const std::uint8_t *ids, std::size_t count)
    {
        FeetechBus validator;
        const rt::ErrorCode result = validator.bind(ids, count);
        if(result != rt::ErrorCode::ok) return result;
        count_ = count;
        for(std::size_t i = 0; i < count_; ++i) ids_[i] = ids[i];
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode exchange(const FeetechPacket &sync_write,
                           const FeetechPacket &sync_read,
                           FeetechPacket *responses,
                           std::size_t response_capacity)
    {
        FeetechFrame write_frame{};
        FeetechFrame read_frame{};
        FeetechParser write_parser;
        FeetechParser read_parser;
        for(std::size_t i = 0; i < sync_write.size; ++i) {
            write_parser.push(sync_write.bytes[i], write_frame);
        }
        for(std::size_t i = 0; i < sync_read.size; ++i) {
            read_parser.push(sync_read.bytes[i], read_frame);
        }
        if(responses == nullptr || response_capacity < count_ || !write_frame.ready ||
           !read_frame.ready || write_frame.id != 0xFE || read_frame.id != 0xFE ||
           write_frame.error != static_cast<std::uint8_t>(FeetechInstruction::sync_write) ||
           read_frame.error != static_cast<std::uint8_t>(FeetechInstruction::sync_read) ||
           write_frame.parameter_count < 2 || read_frame.parameter_count != count_ + 2 ||
           write_frame.parameters[0] != 41 || write_frame.parameters[1] != 7 ||
           read_frame.parameters[0] != 56 || read_frame.parameters[1] != 8) {
            return rt::ErrorCode::invalid_argument;
        }
        const std::size_t entries = (write_frame.parameter_count - 2) / 8;
        if(entries != count_ || write_frame.parameter_count != 2 + entries * 8) {
            return rt::ErrorCode::invalid_argument;
        }
        std::size_t cursor = 2;
        for(std::size_t entry = 0; entry < entries; ++entry) {
            const std::uint8_t id = write_frame.parameters[cursor++];
            const std::size_t index = find(id);
            if(index >= count_) return rt::ErrorCode::invalid_argument;
            acceleration_[index] = write_frame.parameters[cursor++];
            position_[index] = static_cast<std::uint16_t>(
                write_frame.parameters[cursor] |
                (static_cast<std::uint16_t>(write_frame.parameters[cursor + 1]) << 8));
            cursor += 4;
            velocity_[index] = static_cast<std::uint16_t>(
                write_frame.parameters[cursor] |
                (static_cast<std::uint16_t>(write_frame.parameters[cursor + 1]) << 8));
            cursor += 2;
        }
        for(std::size_t i = 0; i < count_; ++i) {
            std::uint8_t present[8] = {
                static_cast<std::uint8_t>(position_[i]),
                static_cast<std::uint8_t>(position_[i] >> 8),
                static_cast<std::uint8_t>(velocity_[i]),
                static_cast<std::uint8_t>(velocity_[i] >> 8),
                0, 0, 120, 25};
            encode_status(ids_[i], 0, present, 8, responses[i]);
        }
        return rt::ErrorCode::ok;
    }

private:
    std::size_t find(std::uint8_t id) const
    {
        for(std::size_t i = 0; i < count_; ++i) {
            if(ids_[i] == id) return i;
        }
        return count_;
    }

    FeetechConfig config_{};
    std::uint8_t ids_[FeetechBus::StorageCapacity] = {};
    std::uint16_t position_[FeetechBus::StorageCapacity] = {};
    std::uint16_t velocity_[FeetechBus::StorageCapacity] = {};
    std::uint8_t acceleration_[FeetechBus::StorageCapacity] = {};
    std::size_t count_ = 0;
};

} // namespace plcopen::core::adapters
