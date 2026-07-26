#pragma once

// H3 fixed-base inverse dynamics (approved matrix:
// doc/compliance/dynamics-feedforward-semantics.md v1).
//
// RT-SAFE: fixed storage and bounded O(n) loops; no allocation, locks,
// exceptions, operating-system calls, or wall-clock access.

#include <array>
#include <cmath>
#include <cstddef>

#include "geom/frame.h"
#include "rt/error.h"

namespace plcopen::core::dyn
{

inline constexpr std::size_t FixedBaseChainMaxJoints = 8;

struct RevoluteBody
{
    // Maps joint-local coordinates at q=0 into the parent coordinates.
    geom::RigidTransform parent_from_joint_zero{};
    // Unit rotation axis expressed in the joint-local coordinates.
    geom::Vec3 joint_axis{};
    double mass = 0.0;
    // Center of mass and inertia at the center of mass, both expressed in
    // joint-local coordinates.
    geom::Vec3 center_of_mass{};
    double inertia_com[3][3] = {};
};

struct FixedBaseChainSpec
{
    std::size_t joint_count = 0;
    std::array<RevoluteBody, FixedBaseChainMaxJoints> bodies{};
};

class FixedBaseChain
{
  public:
    static constexpr std::size_t MaxJoints = FixedBaseChainMaxJoints;

    explicit FixedBaseChain(const FixedBaseChainSpec &spec)
        : spec_(spec), valid_(validate_spec(spec))
    {
        if (valid_)
        {
            for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
            {
                spatial_inertias_[joint] = make_spatial_inertia(spec_.bodies[joint]);
            }
        }
    }

    bool valid() const { return valid_; }

    std::size_t joint_count() const { return valid_ ? spec_.joint_count : 0; }

    rt::ErrorCode inverse_dynamics(const double *q, const double *dq, const double *ddq,
                                   const double gravity[3], double *tau_out) const
    {
        if (!valid_inputs(q, dq, ddq, gravity, tau_out))
        {
            return rt::ErrorCode::invalid_argument;
        }

        std::array<SpatialMatrix, MaxJoints> parent_motion_to_body{};
        std::array<SpatialVector, MaxJoints> velocities{};
        std::array<SpatialVector, MaxJoints> accelerations{};
        std::array<SpatialVector, MaxJoints> forces{};
        std::array<double, MaxJoints> torques{};

        SpatialVector parent_velocity{};
        SpatialVector parent_acceleration{};
        parent_acceleration[3] = -gravity[0];
        parent_acceleration[4] = -gravity[1];
        parent_acceleration[5] = -gravity[2];

        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            const RevoluteBody &body = spec_.bodies[joint];
            parent_motion_to_body[joint] = motion_transform(body, q[joint]);

            const SpatialVector transformed_velocity =
                multiply(parent_motion_to_body[joint], parent_velocity);
            SpatialVector joint_velocity{};
            joint_velocity[0] = body.joint_axis.x * dq[joint];
            joint_velocity[1] = body.joint_axis.y * dq[joint];
            joint_velocity[2] = body.joint_axis.z * dq[joint];
            velocities[joint] = add(transformed_velocity, joint_velocity);

            const SpatialVector transformed_acceleration =
                multiply(parent_motion_to_body[joint], parent_acceleration);
            SpatialVector joint_acceleration{};
            joint_acceleration[0] = body.joint_axis.x * ddq[joint];
            joint_acceleration[1] = body.joint_axis.y * ddq[joint];
            joint_acceleration[2] = body.joint_axis.z * ddq[joint];
            accelerations[joint] =
                add(add(transformed_acceleration, joint_acceleration),
                    motion_cross(velocities[joint], joint_velocity));

            const SpatialVector momentum =
                multiply(spatial_inertias_[joint], velocities[joint]);
            forces[joint] =
                add(multiply(spatial_inertias_[joint], accelerations[joint]),
                    force_cross(velocities[joint], momentum));

            parent_velocity = velocities[joint];
            parent_acceleration = accelerations[joint];
        }

        for (std::size_t joint = spec_.joint_count; joint-- > 0;)
        {
            const geom::Vec3 axis = spec_.bodies[joint].joint_axis;
            torques[joint] =
                axis.x * forces[joint][0] + axis.y * forces[joint][1] +
                axis.z * forces[joint][2];
            if (!std::isfinite(torques[joint]))
            {
                return rt::ErrorCode::invalid_argument;
            }
            if (joint != 0)
            {
                forces[joint - 1] =
                    add(forces[joint - 1],
                        transpose_multiply(parent_motion_to_body[joint], forces[joint]));
            }
        }

        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            tau_out[joint] = torques[joint];
        }
        return rt::ErrorCode::ok;
    }

  private:
    using SpatialVector = std::array<double, 6>;
    using SpatialMatrix = std::array<SpatialVector, 6>;

    static constexpr double RotationTolerance = 1e-9;
    static constexpr double AxisTolerance = 1e-9;

    static bool finite(geom::Vec3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    static double determinant3(const double matrix[3][3])
    {
        return matrix[0][0] *
                   (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
               matrix[0][1] *
                   (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
               matrix[0][2] *
                   (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
    }

    static bool valid_rotation(const double rotation[3][3])
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                if (!std::isfinite(rotation[row][column]))
                {
                    return false;
                }
            }
        }
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const double dot = rotation[0][row] * rotation[0][column] +
                                   rotation[1][row] * rotation[1][column] +
                                   rotation[2][row] * rotation[2][column];
                const double expected = row == column ? 1.0 : 0.0;
                if (std::fabs(dot - expected) > RotationTolerance)
                {
                    return false;
                }
            }
        }
        return std::fabs(determinant3(rotation) - 1.0) <= RotationTolerance;
    }

    static bool valid_inertia(const double inertia[3][3])
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                if (!std::isfinite(inertia[row][column]))
                {
                    return false;
                }
                if (inertia[row][column] != inertia[column][row])
                {
                    return false;
                }
            }
        }

        const double leading2 =
            inertia[0][0] * inertia[1][1] - inertia[0][1] * inertia[1][0];
        const double determinant = determinant3(inertia);
        if (!(inertia[0][0] > 0.0) || !(leading2 > 0.0) ||
            !(determinant > 0.0))
        {
            return false;
        }

        // For principal moments J, triangle feasibility is J_i <= J_j + J_k.
        // The coordinate-invariant equivalent is trace(I)E - 2I >= 0.
        double triangle[3][3] = {};
        const double trace = inertia[0][0] + inertia[1][1] + inertia[2][2];
        double scale = 0.0;
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const double magnitude = std::fabs(inertia[row][column]);
                scale = magnitude > scale ? magnitude : scale;
                triangle[row][column] = -2.0 * inertia[row][column];
                if (row == column)
                {
                    triangle[row][column] += trace;
                }
            }
        }
        const double tolerance1 = 1e-12 * scale;
        const double tolerance2 = tolerance1 * scale;
        const double tolerance3 = tolerance2 * scale;
        for (int index = 0; index < 3; ++index)
        {
            if (triangle[index][index] < -tolerance1)
            {
                return false;
            }
        }
        const double minor01 =
            triangle[0][0] * triangle[1][1] - triangle[0][1] * triangle[1][0];
        const double minor02 =
            triangle[0][0] * triangle[2][2] - triangle[0][2] * triangle[2][0];
        const double minor12 =
            triangle[1][1] * triangle[2][2] - triangle[1][2] * triangle[2][1];
        return minor01 >= -tolerance2 && minor02 >= -tolerance2 &&
               minor12 >= -tolerance2 && determinant3(triangle) >= -tolerance3;
    }

    static bool validate_spec(const FixedBaseChainSpec &spec)
    {
        if (spec.joint_count == 0 || spec.joint_count > MaxJoints)
        {
            return false;
        }
        for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
        {
            const RevoluteBody &body = spec.bodies[joint];
            if (!valid_rotation(body.parent_from_joint_zero.rotation) ||
                !finite(body.parent_from_joint_zero.translation) || !finite(body.joint_axis) ||
                !std::isfinite(body.mass) || body.mass <= 0.0 ||
                !finite(body.center_of_mass) || !valid_inertia(body.inertia_com))
            {
                return false;
            }
            const double axis_squared = geom::dot(body.joint_axis, body.joint_axis);
            if (std::fabs(axis_squared - 1.0) > AxisTolerance)
            {
                return false;
            }
        }
        return true;
    }

    bool valid_inputs(const double *q, const double *dq, const double *ddq,
                      const double gravity[3], const double *tau_out) const
    {
        if (!valid_ || q == nullptr || dq == nullptr || ddq == nullptr || gravity == nullptr ||
            tau_out == nullptr)
        {
            return false;
        }
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            if (!std::isfinite(q[joint]) || !std::isfinite(dq[joint]) ||
                !std::isfinite(ddq[joint]))
            {
                return false;
            }
        }
        return std::isfinite(gravity[0]) && std::isfinite(gravity[1]) &&
               std::isfinite(gravity[2]);
    }

    static SpatialMatrix make_spatial_inertia(const RevoluteBody &body)
    {
        SpatialMatrix inertia{};
        const double c[3] = {body.center_of_mass.x, body.center_of_mass.y,
                             body.center_of_mass.z};
        const double squared = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                inertia[row][column] =
                    body.inertia_com[row][column] +
                    body.mass * ((row == column ? squared : 0.0) - c[row] * c[column]);
            }
        }
        const double cross[3][3] = {
            {0.0, -c[2], c[1]}, {c[2], 0.0, -c[0]}, {-c[1], c[0], 0.0}};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                inertia[row][column + 3] = body.mass * cross[row][column];
                inertia[row + 3][column] = -body.mass * cross[row][column];
                inertia[row + 3][column + 3] =
                    row == column ? body.mass : 0.0;
            }
        }
        return inertia;
    }

    static SpatialMatrix motion_transform(const RevoluteBody &body, double q)
    {
        const double axis[3] = {body.joint_axis.x, body.joint_axis.y,
                                body.joint_axis.z};
        double joint_rotation[3][3];
        geom::rodrigues(axis, q, joint_rotation);
        double child_to_parent[3][3];
        geom::rotation_multiply(body.parent_from_joint_zero.rotation, joint_rotation,
                                child_to_parent);

        const geom::Vec3 p = body.parent_from_joint_zero.translation;
        const double p_cross[3][3] = {
            {0.0, -p.z, p.y}, {p.z, 0.0, -p.x}, {-p.y, p.x, 0.0}};
        SpatialMatrix transform{};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const double transpose_rotation = child_to_parent[column][row];
                transform[row][column] = transpose_rotation;
                transform[row + 3][column + 3] = transpose_rotation;
                transform[row + 3][column] =
                    -(child_to_parent[0][row] * p_cross[0][column] +
                      child_to_parent[1][row] * p_cross[1][column] +
                      child_to_parent[2][row] * p_cross[2][column]);
            }
        }
        return transform;
    }

    static SpatialVector add(const SpatialVector &lhs, const SpatialVector &rhs)
    {
        SpatialVector result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = lhs[index] + rhs[index];
        }
        return result;
    }

    static SpatialVector multiply(const SpatialMatrix &matrix, const SpatialVector &vector)
    {
        SpatialVector result{};
        for (std::size_t row = 0; row < result.size(); ++row)
        {
            for (std::size_t column = 0; column < result.size(); ++column)
            {
                result[row] += matrix[row][column] * vector[column];
            }
        }
        return result;
    }

    static SpatialVector transpose_multiply(const SpatialMatrix &matrix,
                                            const SpatialVector &vector)
    {
        SpatialVector result{};
        for (std::size_t row = 0; row < result.size(); ++row)
        {
            for (std::size_t column = 0; column < result.size(); ++column)
            {
                result[row] += matrix[column][row] * vector[column];
            }
        }
        return result;
    }

    static void cross3(const double lhs[3], const double rhs[3], double result[3])
    {
        result[0] = lhs[1] * rhs[2] - lhs[2] * rhs[1];
        result[1] = lhs[2] * rhs[0] - lhs[0] * rhs[2];
        result[2] = lhs[0] * rhs[1] - lhs[1] * rhs[0];
    }

    static SpatialVector motion_cross(const SpatialVector &lhs, const SpatialVector &rhs)
    {
        SpatialVector result{};
        cross3(lhs.data(), rhs.data(), result.data());
        double first[3];
        double second[3];
        cross3(lhs.data() + 3, rhs.data(), first);
        cross3(lhs.data(), rhs.data() + 3, second);
        for (int index = 0; index < 3; ++index)
        {
            result[index + 3] = first[index] + second[index];
        }
        return result;
    }

    static SpatialVector force_cross(const SpatialVector &motion,
                                     const SpatialVector &force)
    {
        SpatialVector result{};
        double angular[3];
        double offset[3];
        cross3(motion.data(), force.data(), angular);
        cross3(motion.data() + 3, force.data() + 3, offset);
        cross3(motion.data(), force.data() + 3, result.data() + 3);
        for (int index = 0; index < 3; ++index)
        {
            result[index] = angular[index] + offset[index];
        }
        return result;
    }

    FixedBaseChainSpec spec_{};
    std::array<SpatialMatrix, MaxJoints> spatial_inertias_{};
    bool valid_ = false;
};

} // namespace plcopen::core::dyn
