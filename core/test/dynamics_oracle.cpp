// Independent H3 oracle. This test intentionally duplicates the small amount
// of spatial algebra needed by ABA instead of sharing RNEA implementation
// helpers. It also derives gravity torque from world-frame potential energy.

#include <array>
#include <cmath>
#include <cstdio>

#include "dyn/fixed_base_chain.h"
#include "test_support/dynamics_fixture.h"

namespace
{

using namespace plcopen::core;

using Vector6 = std::array<double, 6>;
using Matrix6 = std::array<Vector6, 6>;

constexpr std::size_t MaxJoints = dyn::FixedBaseChain::MaxJoints;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

void cross3(const double lhs[3], const double rhs[3], double out[3])
{
    out[0] = lhs[1] * rhs[2] - lhs[2] * rhs[1];
    out[1] = lhs[2] * rhs[0] - lhs[0] * rhs[2];
    out[2] = lhs[0] * rhs[1] - lhs[1] * rhs[0];
}

Vector6 add(Vector6 lhs, const Vector6 &rhs)
{
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        lhs[index] += rhs[index];
    }
    return lhs;
}

Matrix6 add(Matrix6 lhs, const Matrix6 &rhs)
{
    for (std::size_t row = 0; row < lhs.size(); ++row)
    {
        for (std::size_t column = 0; column < lhs.size(); ++column)
        {
            lhs[row][column] += rhs[row][column];
        }
    }
    return lhs;
}

Vector6 multiply(const Matrix6 &matrix, const Vector6 &vector)
{
    Vector6 out{};
    for (std::size_t row = 0; row < out.size(); ++row)
    {
        for (std::size_t column = 0; column < out.size(); ++column)
        {
            out[row] += matrix[row][column] * vector[column];
        }
    }
    return out;
}

Matrix6 multiply(const Matrix6 &lhs, const Matrix6 &rhs)
{
    Matrix6 out{};
    for (std::size_t row = 0; row < out.size(); ++row)
    {
        for (std::size_t column = 0; column < out.size(); ++column)
        {
            for (std::size_t inner = 0; inner < out.size(); ++inner)
            {
                out[row][column] += lhs[row][inner] * rhs[inner][column];
            }
        }
    }
    return out;
}

Matrix6 transpose(const Matrix6 &matrix)
{
    Matrix6 out{};
    for (std::size_t row = 0; row < out.size(); ++row)
    {
        for (std::size_t column = 0; column < out.size(); ++column)
        {
            out[row][column] = matrix[column][row];
        }
    }
    return out;
}

double dot(const Vector6 &lhs, const Vector6 &rhs)
{
    double out = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        out += lhs[index] * rhs[index];
    }
    return out;
}

Vector6 motion_cross(const Vector6 &lhs, const Vector6 &rhs)
{
    Vector6 out{};
    cross3(lhs.data(), rhs.data(), out.data());
    double first[3];
    double second[3];
    cross3(lhs.data() + 3, rhs.data(), first);
    cross3(lhs.data(), rhs.data() + 3, second);
    for (int index = 0; index < 3; ++index)
    {
        out[index + 3] = first[index] + second[index];
    }
    return out;
}

Vector6 force_cross(const Vector6 &velocity, const Vector6 &force)
{
    Vector6 out{};
    double first[3];
    double second[3];
    cross3(velocity.data(), force.data(), first);
    cross3(velocity.data() + 3, force.data() + 3, second);
    cross3(velocity.data(), force.data() + 3, out.data() + 3);
    for (int index = 0; index < 3; ++index)
    {
        out[index] = first[index] + second[index];
    }
    return out;
}

Matrix6 spatial_inertia(const dyn::RevoluteBody &body)
{
    Matrix6 out{};
    const double c[3] = {body.center_of_mass.x, body.center_of_mass.y,
                         body.center_of_mass.z};
    const double c2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    const double cross[3][3] = {
        {0.0, -c[2], c[1]}, {c[2], 0.0, -c[0]}, {-c[1], c[0], 0.0}};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            out[row][column] =
                body.inertia_com[row][column] +
                body.mass * ((row == column ? c2 : 0.0) - c[row] * c[column]);
            out[row][column + 3] = body.mass * cross[row][column];
            out[row + 3][column] = -body.mass * cross[row][column];
            out[row + 3][column + 3] = row == column ? body.mass : 0.0;
        }
    }
    return out;
}

Matrix6 parent_motion_to_child(const dyn::RevoluteBody &body, double q)
{
    const double axis[3] = {body.joint_axis.x, body.joint_axis.y,
                            body.joint_axis.z};
    double rotation_q[3][3];
    geom::rodrigues(axis, q, rotation_q);
    double child_to_parent[3][3];
    geom::rotation_multiply(body.parent_from_joint_zero.rotation, rotation_q,
                            child_to_parent);
    const geom::Vec3 p = body.parent_from_joint_zero.translation;
    const double p_cross[3][3] = {
        {0.0, -p.z, p.y}, {p.z, 0.0, -p.x}, {-p.y, p.x, 0.0}};
    Matrix6 out{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            out[row][column] = child_to_parent[column][row];
            out[row + 3][column + 3] = child_to_parent[column][row];
            for (int inner = 0; inner < 3; ++inner)
            {
                out[row + 3][column] -=
                    child_to_parent[inner][row] * p_cross[inner][column];
            }
        }
    }
    return out;
}

Vector6 joint_subspace(const dyn::RevoluteBody &body)
{
    return {body.joint_axis.x, body.joint_axis.y, body.joint_axis.z, 0.0, 0.0, 0.0};
}

bool aba_forward_dynamics(const dyn::FixedBaseChainSpec &spec, const double *q,
                          const double *dq, const double *tau, const double gravity[3],
                          double *ddq_out)
{
    std::array<Matrix6, MaxJoints> transforms{};
    std::array<Matrix6, MaxJoints> articulated_inertia{};
    std::array<Vector6, MaxJoints> bias_force{};
    std::array<Vector6, MaxJoints> velocities{};
    std::array<Vector6, MaxJoints> bias_acceleration{};
    std::array<Vector6, MaxJoints> subspaces{};
    std::array<Vector6, MaxJoints> projected_inertia{};
    std::array<double, MaxJoints> denominator{};
    std::array<double, MaxJoints> generalized_force{};

    Vector6 parent_velocity{};
    for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
    {
        const dyn::RevoluteBody &body = spec.bodies[joint];
        transforms[joint] = parent_motion_to_child(body, q[joint]);
        subspaces[joint] = joint_subspace(body);
        Vector6 joint_velocity = subspaces[joint];
        for (double &value : joint_velocity)
        {
            value *= dq[joint];
        }
        velocities[joint] =
            add(multiply(transforms[joint], parent_velocity), joint_velocity);
        bias_acceleration[joint] = motion_cross(velocities[joint], joint_velocity);
        articulated_inertia[joint] = spatial_inertia(body);
        bias_force[joint] =
            force_cross(velocities[joint],
                        multiply(articulated_inertia[joint], velocities[joint]));
        parent_velocity = velocities[joint];
    }

    for (std::size_t joint = spec.joint_count; joint-- > 0;)
    {
        projected_inertia[joint] =
            multiply(articulated_inertia[joint], subspaces[joint]);
        denominator[joint] = dot(subspaces[joint], projected_inertia[joint]);
        generalized_force[joint] =
            tau[joint] - dot(subspaces[joint], bias_force[joint]);
        if (!(denominator[joint] > 1e-14))
        {
            return false;
        }
        if (joint != 0)
        {
            Matrix6 reduced = articulated_inertia[joint];
            for (std::size_t row = 0; row < 6; ++row)
            {
                for (std::size_t column = 0; column < 6; ++column)
                {
                    reduced[row][column] -=
                        projected_inertia[joint][row] *
                        projected_inertia[joint][column] / denominator[joint];
                }
            }
            Vector6 propagated_bias =
                add(bias_force[joint],
                    multiply(reduced, bias_acceleration[joint]));
            for (std::size_t index = 0; index < 6; ++index)
            {
                propagated_bias[index] +=
                    projected_inertia[joint][index] *
                    generalized_force[joint] / denominator[joint];
            }
            const Matrix6 transpose_transform = transpose(transforms[joint]);
            articulated_inertia[joint - 1] =
                add(articulated_inertia[joint - 1],
                    multiply(multiply(transpose_transform, reduced),
                             transforms[joint]));
            bias_force[joint - 1] =
                add(bias_force[joint - 1],
                    multiply(transpose_transform, propagated_bias));
        }
    }

    Vector6 parent_acceleration{};
    parent_acceleration[3] = -gravity[0];
    parent_acceleration[4] = -gravity[1];
    parent_acceleration[5] = -gravity[2];
    for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
    {
        Vector6 acceleration =
            add(multiply(transforms[joint], parent_acceleration),
                bias_acceleration[joint]);
        ddq_out[joint] =
            (generalized_force[joint] - dot(projected_inertia[joint], acceleration)) /
            denominator[joint];
        for (std::size_t index = 0; index < 6; ++index)
        {
            acceleration[index] += subspaces[joint][index] * ddq_out[joint];
        }
        parent_acceleration = acceleration;
    }
    return true;
}

double potential_energy(const dyn::FixedBaseChainSpec &spec, const double *q,
                        const double gravity[3])
{
    double parent_rotation[3][3] = {
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    double parent_position[3] = {};
    double energy = 0.0;
    for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
    {
        const dyn::RevoluteBody &body = spec.bodies[joint];
        const double axis[3] = {body.joint_axis.x, body.joint_axis.y,
                                body.joint_axis.z};
        double rotation_q[3][3];
        geom::rodrigues(axis, q[joint], rotation_q);
        double child_to_parent[3][3];
        geom::rotation_multiply(body.parent_from_joint_zero.rotation, rotation_q,
                                child_to_parent);
        double child_rotation[3][3];
        geom::rotation_multiply(parent_rotation, child_to_parent, child_rotation);

        const geom::Vec3 offset = body.parent_from_joint_zero.translation;
        const double offset_values[3] = {offset.x, offset.y, offset.z};
        double child_position[3] = {};
        for (int row = 0; row < 3; ++row)
        {
            child_position[row] =
                parent_position[row] + parent_rotation[row][0] * offset_values[0] +
                parent_rotation[row][1] * offset_values[1] +
                parent_rotation[row][2] * offset_values[2];
        }
        const double com[3] = {body.center_of_mass.x, body.center_of_mass.y,
                               body.center_of_mass.z};
        double world_com[3] = {};
        for (int row = 0; row < 3; ++row)
        {
            world_com[row] = child_position[row] +
                             child_rotation[row][0] * com[0] +
                             child_rotation[row][1] * com[1] +
                             child_rotation[row][2] * com[2];
        }
        energy -= body.mass * (gravity[0] * world_com[0] +
                               gravity[1] * world_com[1] +
                               gravity[2] * world_com[2]);
        for (int row = 0; row < 3; ++row)
        {
            parent_position[row] = child_position[row];
            for (int column = 0; column < 3; ++column)
            {
                parent_rotation[row][column] = child_rotation[row][column];
            }
        }
    }
    return energy;
}

struct Lcg
{
    unsigned state = 0xC001D00Du;

    double range(double minimum, double maximum)
    {
        state = state * 1664525u + 1013904223u;
        return minimum +
               static_cast<double>(state >> 8) / 16777216.0 * (maximum - minimum);
    }
};

int check_rnea_aba_round_trip()
{
    const dyn::FixedBaseChainSpec spec = test_support::eight_joint_dynamics_spec();
    const dyn::FixedBaseChain chain(spec);
    const double gravity[3] = {0.4, -0.3, -9.7};
    Lcg random{};
    for (int sample = 0; sample < 48; ++sample)
    {
        double q[MaxJoints] = {};
        double dq[MaxJoints] = {};
        double expected_ddq[MaxJoints] = {};
        double tau[MaxJoints] = {};
        double recovered_ddq[MaxJoints] = {};
        for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
        {
            q[joint] = random.range(-1.2, 1.2);
            dq[joint] = random.range(-1.5, 1.5);
            expected_ddq[joint] = random.range(-2.0, 2.0);
        }
        if (chain.inverse_dynamics(q, dq, expected_ddq, gravity, tau) !=
                rt::ErrorCode::ok ||
            !aba_forward_dynamics(spec, q, dq, tau, gravity, recovered_ddq))
        {
            return fail("RNEA to ABA evaluation");
        }
        for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
        {
            if (std::fabs(recovered_ddq[joint] - expected_ddq[joint]) > 2e-10)
            {
                std::printf("round-trip sample=%d joint=%zu actual=%.17g expected=%.17g\n",
                            sample, joint, recovered_ddq[joint],
                            expected_ddq[joint]);
                return fail("independent ABA round trip");
            }
        }
    }
    return 0;
}

int check_potential_energy_gradient()
{
    const dyn::FixedBaseChainSpec spec = test_support::eight_joint_dynamics_spec();
    const dyn::FixedBaseChain chain(spec);
    double q[MaxJoints] = {0.21, -0.32, 0.43, -0.54, 0.65, -0.76, 0.87, -0.98};
    const double zero[MaxJoints] = {};
    const double gravity[3] = {0.3, -0.4, -9.6};
    double static_tau[MaxJoints] = {};
    if (chain.inverse_dynamics(q, zero, zero, gravity, static_tau) !=
        rt::ErrorCode::ok)
    {
        return fail("static RNEA evaluation");
    }
    constexpr double Step = 1e-6;
    for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
    {
        q[joint] += Step;
        const double upper = potential_energy(spec, q, gravity);
        q[joint] -= 2.0 * Step;
        const double lower = potential_energy(spec, q, gravity);
        q[joint] += Step;
        const double gradient = (upper - lower) / (2.0 * Step);
        if (std::fabs(static_tau[joint] - gradient) > 2e-8)
        {
            std::printf("gradient joint=%zu rnea=%.17g potential=%.17g\n", joint,
                        static_tau[joint], gradient);
            return fail("potential-energy gravity gradient");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if (check_rnea_aba_round_trip() != 0 || check_potential_energy_gradient() != 0)
    {
        return 1;
    }
    std::puts("PASS H3 independent ABA and potential-energy oracles");
    return 0;
}
