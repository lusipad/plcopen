#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "axis/group.h"
#include "fb/group.h"
#include "kin/kinematics.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance = 1e-9)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

struct IdentityKinematics final : kin::Kinematics
{
    std::size_t joint_count() const override { return 3; }
    std::size_t cartesian_count() const override { return 3; }

    rt::ErrorCode forward(const double *joints,
                          std::size_t count,
                          geom::Vec3 &point) const override
    {
        if(count != 3) return rt::ErrorCode::invalid_argument;
        point = {joints[0], joints[1], joints[2]};
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3 point,
                          const double *,
                          std::size_t count,
                          double *joints) const override
    {
        if(count != 3) return rt::ErrorCode::invalid_argument;
        joints[0] = point.x;
        joints[1] = point.y;
        joints[2] = point.z;
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *, std::size_t) const override
    {
        return 1.0;
    }
};

struct IdentityPoseKinematics : kin::PoseKinematics
{
    std::size_t joint_count() const override { return 6; }

    void forward(const double *joints, kin::Pose6 &pose) const override
    {
        const geom::RigidTransform transform = geom::make_rpy_transform(
            joints[0], joints[1], joints[2], joints[3], joints[4], joints[5]);
        pose.position[0] = transform.translation.x;
        pose.position[1] = transform.translation.y;
        pose.position[2] = transform.translation.z;
        for(int row = 0; row < 3; ++row) {
            for(int column = 0; column < 3; ++column) {
                pose.rotation[row][column] = transform.rotation[row][column];
            }
        }
    }

    rt::ErrorCode inverse(const kin::Pose6 &pose,
                          const double *,
                          double,
                          double *joints) const override
    {
        joints[0] = pose.position[0];
        joints[1] = pose.position[1];
        joints[2] = pose.position[2];
        bool gimbal = geom::extract_rpy(pose.rotation, joints[3], joints[4], joints[5]);
        return gimbal ? rt::ErrorCode::precondition_failed : rt::ErrorCode::ok;
    }

    double singularity_margin(const double *) const override { return 1.0; }
};

struct ControlledKinematics final : kin::Kinematics
{
    rt::ErrorCode inverse_error = rt::ErrorCode::ok;
    double margin = 1.0;

    std::size_t joint_count() const override { return 3; }
    std::size_t cartesian_count() const override { return 3; }

    rt::ErrorCode forward(const double *joints,
                          std::size_t count,
                          geom::Vec3 &point) const override
    {
        if(count != 3) return rt::ErrorCode::invalid_argument;
        point = {joints[0], joints[1], joints[2]};
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3 point,
                          const double *,
                          std::size_t count,
                          double *joints) const override
    {
        if(inverse_error != rt::ErrorCode::ok) return inverse_error;
        if(count != 3) return rt::ErrorCode::invalid_argument;
        joints[0] = point.x;
        joints[1] = point.y;
        joints[2] = point.z;
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *, std::size_t) const override
    {
        return margin;
    }
};

struct ControlledPoseKinematics final : IdentityPoseKinematics
{
    rt::ErrorCode inverse_error = rt::ErrorCode::ok;
    double margin = 1.0;

    rt::ErrorCode inverse(const kin::Pose6 &pose,
                          const double *seed,
                          double max_joint_step,
                          double *joints) const override
    {
        if(inverse_error != rt::ErrorCode::ok) return inverse_error;
        return IdentityPoseKinematics::inverse(pose, seed, max_joint_step, joints);
    }

    double singularity_margin(const double *) const override { return margin; }
};

struct Rig
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    explicit Rig(std::size_t count = 3)
    {
        for(std::size_t i = 0; i < count; ++i) {
            group.add_axis(axes[i]);
            axes[i].set_power(true);
        }
        group.enable();
    }

    void cycle()
    {
        group.cycle();
        for(auto &axis : axes) axis.cycle();
    }
};

int check_public_facades_compile()
{
    static_assert(std::is_default_constructible<fb::FbGroupWriteToolData>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadToolData>::value);
    static_assert(std::is_default_constructible<fb::FbGroupSelectTool>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadTool>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWritePayloadData>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadPayloadData>::value);
    static_assert(std::is_default_constructible<fb::FbGroupSelectPayload>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadPayload>::value);
    static_assert(std::is_default_constructible<fb::FbGroupJog>::value);
    static_assert(std::is_default_constructible<fb::FbGroupJogVector>::value);
    std::printf("  PASS public_facades_compile\n");
    return 0;
}

int check_tool_store_and_tcp_consumption()
{
    static Rig rig;
    static IdentityKinematics identity;
    if(rig.group.set_kinematics(&identity, 0.0) != rt::ErrorCode::ok) {
        return fail("tool: kinematics setup");
    }

    fb::FbGroupWriteToolData write;
    write.group_ref = &rig.group;
    write.execute = true;
    write.tool_number = 1;
    write.tool_data = {{0.25, -0.5, 0.75, 0.0, 0.0, 0.0}};
    write.call();
    if(!write.outputs.done || write.outputs.error) return fail("tool: write slot 1");

    fb::FbGroupReadToolData read;
    read.group_ref = &rig.group;
    read.enable = true;
    read.tool_number = 1;
    read.call();
    if(!read.valid || read.error || !near(read.tool_data.value[2], 0.75)) {
        return fail("tool: read echoes");
    }

    fb::FbGroupSelectTool select;
    select.group_ref = &rig.group;
    select.execute = true;
    select.tool_number = 1;
    select.call();
    if(!select.outputs.done || select.outputs.error) return fail("tool: select");

    fb::FbMoveLinearAbsolute move;
    move.group_ref = &rig.group;
    move.position.size = 3;
    move.position.value[0] = 1.25;
    move.position.value[1] = 1.5;
    move.position.value[2] = 2.75;
    move.coord_system = axis::CoordSystem::mcs;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    move.execute = true;
    move.call();
    if(move.outputs.error) return fail("tool: MCS submit");
    for(int i = 0; i < 32 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.cycle();
    }
    if(!near(rig.axes[0].snapshot().command_position, 1.0) ||
       !near(rig.axes[1].snapshot().command_position, 2.0) ||
       !near(rig.axes[2].snapshot().command_position, 2.0)) {
        return fail("tool: selected transform affects TCP target");
    }

    fb::FbGroupReadTool current;
    current.group_ref = &rig.group;
    current.enable = true;
    current.source = axis::SelectionSource::selected;
    current.call();
    if(!current.valid || current.tool_number != 1) return fail("tool: selected read");

    write.execute = false;
    write.call();
    write.execute = true;
    write.tool_number = 0;
    write.call();
    if(!write.outputs.error || write.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("tool: slot zero immutable");
    }
    std::printf("  PASS tool_store_and_tcp_consumption\n");
    return 0;
}

int check_payload_store_and_selection()
{
    Rig rig(1);
    fb::FbGroupWritePayloadData write;
    write.group_ref = &rig.group;
    write.execute = true;
    write.payload_number = 15;
    write.payload_data.center = {{0.1, 0.2, 0.3, 0.4, 0.5, 0.6}};
    write.payload_data.mass = 2.5;
    write.payload_data.ix = 1.0;
    write.payload_data.iy = 2.0;
    write.payload_data.iz = 3.0;
    write.call();
    if(!write.outputs.done) return fail("payload: write upper slot");

    fb::FbGroupSelectPayload select;
    select.group_ref = &rig.group;
    select.execute = true;
    select.payload_number = 15;
    select.call();
    if(!select.outputs.done) return fail("payload: select");

    fb::FbGroupReadPayloadData read;
    read.group_ref = &rig.group;
    read.enable = true;
    read.payload_number = 15;
    read.call();
    if(!read.valid || !near(read.payload_data.mass, 2.5) ||
       !near(read.payload_data.center.value[5], 0.6)) {
        return fail("payload: read ten values");
    }

    fb::FbGroupReadPayload current;
    current.group_ref = &rig.group;
    current.enable = true;
    current.source = axis::SelectionSource::active;
    current.call();
    if(!current.valid || current.payload_number != 15) return fail("payload: active read");
    current.source = static_cast<axis::SelectionSource>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        9);
    current.call();
    if(!current.error || current.error_id != rt::ErrorCode::unsupported || current.valid) {
        return fail("payload: invalid source rejected");
    }

    write.execute = false;
    write.call();
    write.execute = true;
    write.payload_number = 1;
    write.payload_data.mass = -1.0;
    write.call();
    if(!write.outputs.error || write.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("payload: negative mass rejected");
    }
    std::printf("  PASS payload_store_and_selection\n");
    return 0;
}

int check_data_validation_matrix()
{
    Rig rig(1);
    axis::ToolData tool{};
    if(rig.group.write_tool_data(axis::AxisGroup::ToolCapacity, tool) !=
           rt::ErrorCode::out_of_range ||
       rig.group.read_tool_data(1).error() != rt::ErrorCode::out_of_range ||
       rig.group.select_tool(1) != rt::ErrorCode::out_of_range) {
        return fail("data validation: tool slots");
    }
    tool.value[5] = std::numeric_limits<double>::infinity();
    if(rig.group.write_tool_data(1, tool) != rt::ErrorCode::invalid_argument) {
        return fail("data validation: tool finite");
    }

    axis::PayloadData payload{};
    if(rig.group.write_payload_data(axis::AxisGroup::PayloadCapacity, payload) !=
           rt::ErrorCode::out_of_range ||
       rig.group.read_payload_data(1).error() != rt::ErrorCode::out_of_range ||
       rig.group.select_payload(1) != rt::ErrorCode::out_of_range) {
        return fail("data validation: payload slots");
    }
    payload.center.value[5] = std::numeric_limits<double>::infinity();
    if(rig.group.write_payload_data(1, payload) != rt::ErrorCode::invalid_argument) {
        return fail("data validation: payload center");
    }

    payload = {};
    double *fields[] = {&payload.mass, &payload.ix, &payload.iy, &payload.iz};
    for(double *field : fields) {
        *field = std::numeric_limits<double>::infinity();
        if(rig.group.write_payload_data(1, payload) != rt::ErrorCode::invalid_argument) {
            return fail("data validation: payload finite");
        }
        *field = 0.0;
    }
    for(double *field : fields) {
        *field = -1.0;
        if(rig.group.write_payload_data(1, payload) != rt::ErrorCode::invalid_argument) {
            return fail("data validation: payload nonnegative");
        }
        *field = 0.0;
    }
    std::printf("  PASS data_validation_matrix\n");
    return 0;
}

axis::JoggingDynamics jogging(std::size_t count)
{
    axis::JoggingDynamics result{};
    result.size = count;
    result.path = {0.4, 0.2, 0.2, 0.1};
    for(std::size_t i = 0; i < count; ++i) {
        result.axis_velocity[i] = 0.5;
        result.axis_acceleration[i] = 0.2;
        result.axis_deceleration[i] = 0.2;
        result.axis_jerk[i] = 0.1;
    }
    return result;
}

int check_acs_jog_lifecycle()
{
    Rig rig(2);
    if(rig.group.write_jogging_dynamics(jogging(2)) != rt::ErrorCode::ok) {
        return fail("ACS jog: dynamics");
    }
    fb::FbGroupJog jog;
    jog.group_ref = &rig.group;
    jog.enable = true;
    jog.jog_positive.count = 2;
    jog.jog_negative.count = 2;
    jog.jog_positive.value[0] = true;
    jog.jog_negative.value[1] = true;
    jog.call();
    if(!jog.enabled || !jog.active || jog.error) return fail("ACS jog: starts");
    for(int i = 0; i < 8; ++i) {
        rig.cycle();
        jog.call();
    }
    if(rig.axes[0].snapshot().command_velocity <= 0.0 ||
       rig.axes[1].snapshot().command_velocity >= 0.0 ||
       std::fabs(rig.axes[0].snapshot().command_acceleration) > 0.2 + 1e-12) {
        return fail("ACS jog: signed bounded motion");
    }

    jog.jog_negative.value[0] = true;
    jog.call();
    for(int i = 0; i < 12; ++i) {
        rig.cycle();
        jog.call();
    }
    if(std::fabs(rig.axes[0].snapshot().command_velocity) > 1e-12 ||
       rig.axes[1].snapshot().command_velocity >= 0.0) {
        return fail("ACS jog: conflict stops one axis");
    }

    jog.enable = false;
    jog.call();
    for(int i = 0; i < 32 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::standby || jog.enabled) {
        return fail("ACS jog: release controlled stop");
    }
    std::printf("  PASS acs_jog_lifecycle\n");
    return 0;
}

int check_cartesian_vector_jog()
{
    static Rig rig;
    static IdentityKinematics identity;
    if(rig.group.set_kinematics(&identity, 0.0) != rt::ErrorCode::ok ||
       rig.group.write_jogging_dynamics(jogging(3)) != rt::ErrorCode::ok) {
        return fail("vector jog: setup");
    }
    fb::FbGroupJogVector jog;
    jog.group_ref = &rig.group;
    jog.enable = true;
    jog.coord_system = axis::CoordSystem::mcs;
    jog.direction.size = 3;
    jog.direction.value[0] = 1.0;
    jog.call();
    for(int i = 0; i < 8; ++i) {
        rig.cycle();
        jog.call();
    }
    if(!jog.enabled || !jog.active || rig.axes[0].snapshot().command_position <= 0.0 ||
       !near(rig.axes[1].snapshot().command_position, 0.0)) {
        return fail("vector jog: MCS direction");
    }
    jog.direction.value[0] = 0.0;
    jog.direction.value[1] = 1.0;
    jog.call();
    for(int i = 0; i < 8; ++i) {
        rig.cycle();
        jog.call();
    }
    if(rig.axes[1].snapshot().command_position <= 0.0) {
        return fail("vector jog: continuous direction update");
    }
    std::printf("  PASS cartesian_vector_jog\n");
    return 0;
}

int check_jog_rejections_stop_and_takeover()
{
    Rig unconfigured(2);
    fb::FbGroupJog missing;
    missing.group_ref = &unconfigured.group;
    missing.enable = true;
    missing.jog_positive.count = 2;
    missing.jog_negative.count = 2;
    missing.jog_positive.value[0] = true;
    missing.call();
    if(!missing.error || missing.error_id != rt::ErrorCode::precondition_failed) {
        return fail("jog matrix: missing dynamics");
    }

    Rig rig(2);
    rig.group.write_jogging_dynamics(jogging(2));
    fb::FbGroupJog jog;
    jog.group_ref = &rig.group;
    jog.enable = true;
    jog.jog_positive.count = 2;
    jog.jog_negative.count = 2;
    jog.jog_positive.value[0] = true;
    jog.call();
    for(int i = 0; i < 5; ++i) {
        rig.cycle();
        jog.call();
    }
    if(rig.group.stop(0.1, 0.05) != rt::ErrorCode::ok ||
       rig.group.status() != axis::GroupStatus::stopping) {
        return fail("jog matrix: GroupStop takes control");
    }
    for(int i = 0; i < 32 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("jog matrix: GroupStop settles");
    }

    jog.enable = false;
    jog.call();
    jog.enable = true;
    jog.call();
    axis::GroupCommand buffered{};
    buffered.target.size = 2;
    buffered.target.value[0] = 0.5;
    buffered.target.value[1] = 0.5;
    buffered.velocity = 1.0;
    buffered.acceleration = 1.0;
    buffered.deceleration = 1.0;
    buffered.jerk = 1.0;
    buffered.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> queued = rig.group.submit_linear(buffered);
    if(queued || queued.error() != rt::ErrorCode::unsupported) {
        return fail("jog matrix: buffered rejected while active");
    }
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 1.0;
    command.target.value[1] = 1.0;
    command.velocity = 1.0;
    command.acceleration = 1.0;
    command.deceleration = 1.0;
    command.jerk = 1.0;
    if(!rig.group.submit_linear(command)) return fail("jog matrix: takeover submit");
    jog.call();
    if(!jog.command_aborted || jog.active || jog.error) {
        return fail("jog matrix: takeover reports aborted");
    }
    std::printf("  PASS jog_rejections_stop_and_takeover\n");
    return 0;
}

bool rejects_partial_axis_jog_dynamics(int field)
{
    Rig rig(1);
    axis::JoggingDynamics partial = jogging(1);
    double *axis_fields[] = {&partial.axis_velocity[0], &partial.axis_acceleration[0],
                             &partial.axis_deceleration[0], &partial.axis_jerk[0]};
    *axis_fields[field] = 0.0;
    axis::GroupPosition direction{};
    direction.size = 1;
    direction.value[0] = 1.0;
    return rig.group.write_jogging_dynamics(partial) == rt::ErrorCode::ok &&
           rig.group.begin_jog(axis::CoordSystem::acs, direction).error() ==
               rt::ErrorCode::precondition_failed;
}

int check_partial_jog_dynamics()
{
    for(int field = 0; field < 4; ++field) {
        if(!rejects_partial_axis_jog_dynamics(field)) {
            return fail("jog validation: partial axis dynamics");
        }
    }
    std::printf("  PASS partial_jog_dynamics\n");
    return 0;
}

bool cartesian_jog_reports(ControlledKinematics &kinematics,
                           double minimum_margin,
                           rt::ErrorCode expected)
{
    Rig rig(3);
    if(rig.group.set_kinematics(&kinematics, minimum_margin) != rt::ErrorCode::ok ||
       rig.group.write_jogging_dynamics(jogging(3)) != rt::ErrorCode::ok) {
        return false;
    }
    axis::GroupPosition direction{};
    direction.size = 3;
    direction.value[0] = 1.0;
    if(!rig.group.begin_jog(axis::CoordSystem::mcs, direction)) return false;
    rig.cycle();
    return rig.group.jog_error() == expected;
}

int check_cartesian_jog_errors()
{
    ControlledKinematics failed;
    failed.inverse_error = rt::ErrorCode::invalid_argument;
    if(!cartesian_jog_reports(failed, 0.0, rt::ErrorCode::invalid_argument)) {
        return fail("jog errors: inverse failure");
    }
    ControlledKinematics singular;
    singular.margin = 0.0;
    if(!cartesian_jog_reports(singular, 0.5, rt::ErrorCode::precondition_failed)) {
        return fail("jog errors: singularity margin");
    }

    static Rig pose_failed(6);
    static ControlledPoseKinematics failed_pose;
    failed_pose.inverse_error = rt::ErrorCode::invalid_argument;
    if(pose_failed.group.set_pose_kinematics(&failed_pose, 0.0, 10.0) !=
           rt::ErrorCode::ok ||
       pose_failed.group.write_jogging_dynamics(jogging(6)) != rt::ErrorCode::ok) {
        return fail("jog errors: pose failure setup");
    }
    axis::GroupPosition pose_direction{};
    pose_direction.size = 6;
    pose_direction.value[5] = 1.0;
    if(!pose_failed.group.begin_jog(axis::CoordSystem::mcs, pose_direction)) {
        return fail("jog errors: pose failure start");
    }
    pose_failed.cycle();
    if(pose_failed.group.jog_error() != rt::ErrorCode::invalid_argument) {
        return fail("jog errors: pose inverse failure");
    }

    static Rig pose_singular(6);
    static ControlledPoseKinematics singular_pose;
    singular_pose.margin = 0.0;
    if(pose_singular.group.set_pose_kinematics(&singular_pose, 0.5, 10.0) !=
           rt::ErrorCode::ok ||
       pose_singular.group.write_jogging_dynamics(jogging(6)) != rt::ErrorCode::ok ||
       !pose_singular.group.begin_jog(axis::CoordSystem::mcs, pose_direction)) {
        return fail("jog errors: pose margin setup");
    }
    pose_singular.cycle();
    if(pose_singular.group.jog_error() != rt::ErrorCode::precondition_failed) {
        return fail("jog errors: pose singularity margin");
    }
    std::printf("  PASS cartesian_jog_errors\n");
    return 0;
}

int check_jog_validation_matrix()
{
    axis::AxisGroup empty;
    axis::GroupPosition direction{};
    direction.size = 1;
    direction.value[0] = 1.0;
    if(empty.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("jog validation: empty group");
    }

    Rig rig(2);
    direction.size = 0;
    if(rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("jog validation: empty direction");
    }
    direction.size = 2;
    direction.value[0] = std::numeric_limits<double>::quiet_NaN();
    if(rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("jog validation: nonfinite direction");
    }
    direction.value[0] = 1.0;
    if(rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::precondition_failed) {
        return fail("jog validation: missing dynamics");
    }

    axis::JoggingDynamics dynamics = jogging(2);
    dynamics.size = 1;
    if(rig.group.write_jogging_dynamics(dynamics) != rt::ErrorCode::invalid_argument ||
       rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
           rt::ErrorCode::precondition_failed) {
        return fail("jog validation: dynamics size");
    }
    dynamics = jogging(2);
    dynamics.path.velocity = std::numeric_limits<double>::quiet_NaN();
    if(rig.group.write_jogging_dynamics(dynamics) != rt::ErrorCode::invalid_argument ||
       rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
           rt::ErrorCode::precondition_failed) {
        return fail("jog validation: path velocity");
    }
    dynamics = jogging(2);
    dynamics.axis_jerk[1] = std::numeric_limits<double>::quiet_NaN();
    if(rig.group.write_jogging_dynamics(dynamics) != rt::ErrorCode::invalid_argument ||
       rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
           rt::ErrorCode::precondition_failed) {
        return fail("jog validation: axis jerk");
    }
    dynamics = jogging(2);
    if(rig.group.write_jogging_dynamics(dynamics) != rt::ErrorCode::ok) {
        return fail("jog validation: restore dynamics");
    }
    direction.size = 1;
    if(rig.group.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("jog validation: ACS size");
    }
    direction.size = 2;
    if(rig.group
           .begin_jog(
               static_cast<axis::CoordSystem>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                   99),
               direction)
           .error() !=
       rt::ErrorCode::unsupported) {
        return fail("jog validation: unsupported coordinate system");
    }
    if(rig.group.begin_jog(axis::CoordSystem::mcs, direction).error() !=
       rt::ErrorCode::precondition_failed) {
        return fail("jog validation: missing cartesian kinematics");
    }
    Rig cart(3);
    if(cart.group.write_jogging_dynamics(jogging(3)) != rt::ErrorCode::ok) {
        return fail("jog validation: cartesian dynamics");
    }
    IdentityKinematics identity;
    if(cart.group.set_kinematics(&identity, 0.0) != rt::ErrorCode::ok) {
        return fail("jog validation: kinematics setup");
    }
    direction.size = 2;
    if(cart.group.begin_jog(axis::CoordSystem::mcs, direction).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("jog validation: cartesian size");
    }
    direction.size = 3;
    if(cart.group.begin_jog(axis::CoordSystem::mcs, direction).error() !=
       rt::ErrorCode::ok) {
        return fail("jog validation: cartesian start");
    }
    cart.group.stop(0.2, 0.1);
    for(int i = 0; i < 32 && cart.group.status() != axis::GroupStatus::standby; ++i) {
        cart.cycle();
    }
    std::printf("  PASS jog_validation_matrix\n");
    return 0;
}

int check_jog_command_contract()
{
    axis::AxisModel disabled_axis;
    axis::AxisGroup disabled;
    disabled_axis.set_power(true);
    disabled.add_axis(disabled_axis);
    disabled.write_jogging_dynamics(jogging(1));
    axis::GroupPosition direction{};
    direction.size = 1;
    direction.value[0] = 1.0;
    if(disabled.begin_jog(axis::CoordSystem::acs, direction).error() !=
       rt::ErrorCode::precondition_failed) {
        return fail("jog command: disabled group");
    }

    Rig rig(1);
    if(rig.group.write_jogging_dynamics(jogging(1)) != rt::ErrorCode::ok ||
       rig.group.update_jog(0, direction) != rt::ErrorCode::precondition_failed ||
       rig.group.update_jog(99, direction) != rt::ErrorCode::precondition_failed ||
       rig.group.release_jog(0) != rt::ErrorCode::precondition_failed ||
       rig.group.release_jog(99) != rt::ErrorCode::precondition_failed) {
        return fail("jog command: inactive IDs");
    }
    const rt::Result<std::uint32_t> first =
        rig.group.begin_jog(axis::CoordSystem::acs, direction);
    if(!first || !rig.group.jog_command_active(first.value()) ||
       rig.group.jog_command_active(0) || rig.group.jog_command_active(first.value() + 1) ||
       rig.group.update_jog(0, direction) != rt::ErrorCode::precondition_failed ||
       rig.group.update_jog(first.value() + 1, direction) !=
           rt::ErrorCode::precondition_failed ||
       rig.group.release_jog(0) != rt::ErrorCode::precondition_failed ||
       rig.group.release_jog(first.value() + 1) != rt::ErrorCode::precondition_failed) {
        return fail("jog command: active IDs");
    }
    const rt::Result<std::uint32_t> second =
        rig.group.begin_jog(axis::CoordSystem::acs, direction);
    if(!second || !rig.group.jog_command_aborted(first.value()) ||
       rig.group.jog_command_aborted(0) ||
       rig.group.jog_command_aborted(first.value() + second.value())) {
        return fail("jog command: repeated takeover");
    }
    if(rig.group.release_jog(second.value()) != rt::ErrorCode::ok ||
       rig.group.select_tool(0) != rt::ErrorCode::precondition_failed ||
       rig.group.select_payload(0) != rt::ErrorCode::precondition_failed) {
        return fail("jog command: stopping configuration lock");
    }
    for(int i = 0; i < 32 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.cycle();
    }

    direction.value[0] = 0.0;
    const rt::Result<std::uint32_t> idle =
        rig.group.begin_jog(axis::CoordSystem::acs, direction);
    if(!idle || rig.group.status() != axis::GroupStatus::standby ||
       rig.group.release_jog(idle.value()) != rt::ErrorCode::ok) {
        return fail("jog command: zero direction");
    }
    rig.cycle();
    std::printf("  PASS jog_command_contract\n");
    return 0;
}

int check_active_tool_readback_snapshot()
{
    static Rig rig;
    static IdentityKinematics identity;
    if(rig.group.set_kinematics(&identity, 0.0) != rt::ErrorCode::ok) {
        return fail("tool snapshot: kinematics");
    }
    axis::ToolData first{};
    first.value[0] = 0.1;
    axis::ToolData second{};
    second.value[0] = 0.4;
    if(rig.group.write_tool_data(1, first) != rt::ErrorCode::ok ||
       rig.group.write_tool_data(2, second) != rt::ErrorCode::ok ||
       rig.group.select_tool(1) != rt::ErrorCode::ok) {
        return fail("tool snapshot: stores");
    }
    axis::GroupCommand command{};
    command.target.size = 3;
    command.target.value[0] = 100.0;
    command.coord_system = axis::CoordSystem::mcs;
    command.velocity = 0.1;
    command.acceleration = 0.01;
    command.deceleration = 0.01;
    command.jerk = 0.01;
    if(!rig.group.submit_linear(command)) return fail("tool snapshot: submit");
    rig.cycle();
    axis::GroupPosition before{};
    if(rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command,
                                before) != rt::ErrorCode::ok ||
       rig.group.select_tool(2) != rt::ErrorCode::ok) {
        return fail("tool snapshot: active read/select");
    }
    axis::GroupPosition after{};
    if(rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command,
                                after) != rt::ErrorCode::ok ||
       !near(before.value[0], after.value[0], 1e-12)) {
        return fail("tool snapshot: active Cartesian read stable");
    }
    std::printf("  PASS active_tool_readback_snapshot\n");
    return 0;
}

int check_pose_rotation_and_tool_snapshot()
{
    static Rig rig(6);
    static IdentityPoseKinematics identity;
    if(rig.group.set_pose_kinematics(&identity, 0.0, 10.0) != rt::ErrorCode::ok ||
       rig.group.write_jogging_dynamics(jogging(6)) != rt::ErrorCode::ok) {
        return fail("pose jog: setup");
    }
    axis::ToolData first{};
    first.value[0] = 0.1;
    axis::ToolData second{};
    second.value[0] = 0.4;
    if(rig.group.write_tool_data(1, first) != rt::ErrorCode::ok ||
       rig.group.write_tool_data(2, second) != rt::ErrorCode::ok ||
       rig.group.select_tool(1) != rt::ErrorCode::ok) {
        return fail("pose jog: tools");
    }
    fb::FbGroupJogVector jog;
    jog.group_ref = &rig.group;
    jog.enable = true;
    jog.coord_system = axis::CoordSystem::mcs;
    jog.direction.size = 6;
    jog.direction.value[5] = 1.0;
    jog.call();
    for(int i = 0; i < 5; ++i) {
        rig.cycle();
        jog.call();
    }
    const double yaw_before = rig.axes[5].snapshot().command_position;
    const double x_before = rig.axes[0].snapshot().command_position;
    if(yaw_before <= 0.0) return fail("pose jog: angular direction consumed");
    if(rig.group.select_tool(2) != rt::ErrorCode::ok) {
        return fail("pose jog: select next tool while active");
    }
    if(!near(rig.axes[0].snapshot().command_position, x_before, 1e-12) ||
       rig.group.read_tool(axis::SelectionSource::active) != 1 ||
       rig.group.read_tool(axis::SelectionSource::selected) != 2) {
        return fail("pose jog: active and selected remain distinct");
    }
    for(int i = 0; i < 5; ++i) {
        rig.cycle();
        jog.call();
    }
    if(near(rig.axes[5].snapshot().command_position, yaw_before, 1e-9) ||
       rig.group.read_tool(axis::SelectionSource::active) != 1) {
        return fail("pose jog: active tool snapshot stable while rotating");
    }
    std::printf("  PASS pose_rotation_and_tool_snapshot\n");
    return 0;
}

} // namespace

int main()
{
    if(check_public_facades_compile() != 0) return 1;
    if(check_tool_store_and_tcp_consumption() != 0) return 1;
    if(check_payload_store_and_selection() != 0) return 1;
    if(check_data_validation_matrix() != 0) return 1;
    if(check_acs_jog_lifecycle() != 0) return 1;
    if(check_cartesian_vector_jog() != 0) return 1;
    if(check_jog_rejections_stop_and_takeover() != 0) return 1;
    if(check_partial_jog_dynamics() != 0) return 1;
    if(check_cartesian_jog_errors() != 0) return 1;
    if(check_jog_validation_matrix() != 0) return 1;
    if(check_jog_command_contract() != 0) return 1;
    if(check_pose_rotation_and_tool_snapshot() != 0) return 1;
    if(check_active_tool_readback_snapshot() != 0) return 1;
    std::printf("part4 P4-B2 tests passed\n");
    return 0;
}
