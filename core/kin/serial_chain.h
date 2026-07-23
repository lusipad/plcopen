#pragma once

// H2 numerical inverse kinematics (approved matrix:
// doc/compliance/kinematics-plugin-semantics.md v2/v2.1). SerialChain is a
// fixed-capacity PoseKinematics plugin for revolute standard-DH and
// modified-DH chains. It owns only the base-to-flange model; L5 keeps the
// workpiece and tool transforms.
//
// RT-SAFE: value math only, fixed storage, bounded iterations, no exceptions.

#include <array>
#include <cmath>
#include <cstddef>

#include "geom/frame.h"
#include "kin/pose.h"
#include "rt/error.h"

namespace plcopen::core::kin
{

inline constexpr std::size_t SerialChainMaxJoints = 8;

enum class DhConvention
{
    standard,
    modified,
};

struct DhLink
{
    double a = 0.0;
    double alpha = 0.0;
    double d = 0.0;
    double theta_offset = 0.0;
    double min_position = 0.0;
    double max_position = 0.0;
};

struct SerialChainSpec
{
    std::size_t joint_count = 0;
    DhConvention convention = DhConvention::standard;
    std::array<DhLink, SerialChainMaxJoints> links{};
};

struct SerialChainSolveOptions
{
    double position_tolerance = 1e-9;
    double orientation_tolerance = 1e-9;
    const double *preferred_joints = nullptr;
    double preference_weight = 0.0;
};

struct SerialChainSolveResult
{
    rt::ErrorCode code = rt::ErrorCode::invalid_argument;
    std::array<double, SerialChainMaxJoints> joints{};
    double position_residual = 0.0;
    double orientation_residual = 0.0;
    std::size_t iterations = 0;
};

class SerialChain final : public PoseKinematics
{
  public:
    static constexpr std::size_t MaxJoints = SerialChainMaxJoints;
    static constexpr std::size_t MaxIterations = 32;

    explicit SerialChain(const SerialChainSpec &spec) : spec_(spec), valid_(validate_spec(spec)) {}

    std::size_t joint_count() const override { return valid_ ? spec_.joint_count : 0; }

    void forward(const double *joints, Pose6 &pose) const override
    {
        geom::RigidTransform transform{};
        if (!valid_ || joints == nullptr)
        {
            copy_pose(transform, pose);
            return;
        }
        for (std::size_t i = 0; i < spec_.joint_count; ++i)
        {
            transform = geom::compose(transform, link_transform(spec_.links[i], joints[i]));
        }
        copy_pose(transform, pose);
    }

    rt::ErrorCode inverse(const Pose6 &pose, const double *seed, double max_joint_step,
                          double *joints_out) const override
    {
        const SerialChainSolveOptions options{};
        return solve(pose, seed, max_joint_step, options, joints_out);
    }

    rt::ErrorCode solve(const Pose6 &pose, const double *seed, double max_joint_step,
                        const SerialChainSolveOptions &options, double *joints_out) const
    {
        if (joints_out == nullptr)
        {
            return rt::ErrorCode::invalid_argument;
        }
        const SerialChainSolveResult result = solve_internal(pose, seed, max_joint_step, options);
        if (result.code == rt::ErrorCode::ok)
        {
            for (std::size_t i = 0; i < spec_.joint_count; ++i)
            {
                joints_out[i] = result.joints[i];
            }
        }
        return result.code;
    }

    SerialChainSolveResult
    solve_best_effort(const Pose6 &pose, const double *seed, double max_joint_step,
                      const SerialChainSolveOptions &options = SerialChainSolveOptions{}) const
    {
        return solve_internal(pose, seed, max_joint_step, options);
    }

    double singularity_margin(const double *) const override
    {
        // Numerical chains use adaptive damping and the convergence gates in
        // place of the analytic plugin's entry-ban margin.
        return valid_ ? 1e300 : 0.0;
    }

  private:
    static constexpr double DifferenceStep = 1e-7;
    static constexpr double MinimumDamping = 1e-6;
    static constexpr double MaximumDamping = 1e6;
    static constexpr std::size_t DampingAttempts = 13;
    static constexpr std::size_t LineSearchSteps = 10;

    static bool validate_spec(const SerialChainSpec &spec)
    {
        if (spec.joint_count == 0 || spec.joint_count > MaxJoints)
        {
            return false;
        }
        if (spec.convention != DhConvention::standard && spec.convention != DhConvention::modified)
        {
            return false;
        }
        for (std::size_t i = 0; i < spec.joint_count; ++i)
        {
            const DhLink &link = spec.links[i];
            if (!std::isfinite(link.a) || !std::isfinite(link.alpha) || !std::isfinite(link.d) ||
                !std::isfinite(link.theta_offset) || !std::isfinite(link.min_position) ||
                !std::isfinite(link.max_position) || link.min_position > link.max_position)
            {
                return false;
            }
        }
        return true;
    }

    static bool valid_solve_options(double max_joint_step, const SerialChainSolveOptions &options)
    {
        constexpr double ToleranceFloor = 1e-12;
        return std::isfinite(max_joint_step) && max_joint_step > 0.0 &&
               std::isfinite(options.position_tolerance) &&
               options.position_tolerance >= ToleranceFloor &&
               std::isfinite(options.orientation_tolerance) &&
               options.orientation_tolerance >= ToleranceFloor &&
               std::isfinite(options.preference_weight) && options.preference_weight >= 0.0 &&
               (options.preference_weight == 0.0 || options.preferred_joints != nullptr);
    }

    bool valid_solve_inputs(const Pose6 &pose, const double *seed, double max_joint_step,
                            const SerialChainSolveOptions &options) const
    {
        if (!valid_ || seed == nullptr || !valid_solve_options(max_joint_step, options))
        {
            return false;
        }
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(pose.position[i]))
            {
                return false;
            }
            for (int j = 0; j < 3; ++j)
            {
                if (!std::isfinite(pose.rotation[i][j]))
                {
                    return false;
                }
            }
        }
        for (std::size_t i = 0; i < spec_.joint_count; ++i)
        {
            if (!std::isfinite(seed[i]) ||
                (options.preference_weight > 0.0 && !std::isfinite(options.preferred_joints[i])))
            {
                return false;
            }
        }
        return true;
    }

    static double vector_norm(const double *values, std::size_t count)
    {
        double squared = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            squared += values[i] * values[i];
        }
        return std::sqrt(squared);
    }

    static void pose_error(const Pose6 &current, const Pose6 &target, double error[6],
                           double &position_residual, double &orientation_residual)
    {
        for (int i = 0; i < 3; ++i)
        {
            error[i] = target.position[i] - current.position[i];
        }
        position_residual = vector_norm(error, 3);

        relative_rotation_log(current.rotation, target.rotation, error + 3);
        orientation_residual = vector_norm(error + 3, 3);
    }

    static void relative_rotation_log(const double from[3][3], const double to[3][3],
                                      double base_log[3])
    {
        double relative[3][3];
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                relative[row][column] = from[0][row] * to[0][column] +
                                        from[1][row] * to[1][column] + from[2][row] * to[2][column];
            }
        }
        double local_log[3] = {0.5 * (relative[2][1] - relative[1][2]),
                               0.5 * (relative[0][2] - relative[2][0]),
                               0.5 * (relative[1][0] - relative[0][1])};
        const double sine = vector_norm(local_log, 3);
        double cosine = 0.5 * (relative[0][0] + relative[1][1] + relative[2][2] - 1.0);
        cosine = cosine > 1.0 ? 1.0 : (cosine < -1.0 ? -1.0 : cosine);
        if (sine > 1e-12)
        {
            const double scale = std::atan2(sine, cosine) / sine;
            for (double &value : local_log)
            {
                value *= scale;
            }
        }
        else if (cosine < 0.0)
        {
            double axis[3];
            double angle = 0.0;
            geom::relative_axis_angle(from, to, axis, angle);
            for (int row = 0; row < 3; ++row)
            {
                local_log[row] = axis[row] * angle;
            }
        }
        for (int row = 0; row < 3; ++row)
        {
            base_log[row] = from[row][0] * local_log[0] + from[row][1] * local_log[1] +
                            from[row][2] * local_log[2];
        }
    }

    void forward_from(const double *joints, Pose6 &pose) const
    {
        geom::RigidTransform transform{};
        for (std::size_t i = 0; i < spec_.joint_count; ++i)
        {
            transform = geom::compose(transform, link_transform(spec_.links[i], joints[i]));
        }
        copy_pose(transform, pose);
    }

    void numerical_jacobian(const double *joints, const Pose6 &current,
                            double jacobian[6][MaxJoints]) const
    {
        double shifted[MaxJoints] = {};
        for (std::size_t i = 0; i < spec_.joint_count; ++i)
        {
            shifted[i] = joints[i];
        }
        for (std::size_t column = 0; column < spec_.joint_count; ++column)
        {
            shifted[column] += DifferenceStep;
            Pose6 perturbed{};
            forward_from(shifted, perturbed);
            shifted[column] = joints[column];
            for (int row = 0; row < 3; ++row)
            {
                jacobian[row][column] =
                    (perturbed.position[row] - current.position[row]) / DifferenceStep;
            }
            double rotation_delta[3];
            relative_rotation_log(current.rotation, perturbed.rotation, rotation_delta);
            for (int row = 0; row < 3; ++row)
            {
                jacobian[row + 3][column] = rotation_delta[row] / DifferenceStep;
            }
        }
        for (std::size_t column = spec_.joint_count; column < MaxJoints; ++column)
        {
            for (int row = 0; row < 6; ++row)
            {
                jacobian[row][column] = 0.0;
            }
        }
    }

    static bool solve_linear(double matrix[6][6], const double right_hand_side[6],
                             double solution[6])
    {
        double augmented[6][7] = {};
        for (int row = 0; row < 6; ++row)
        {
            for (int column = 0; column < 6; ++column)
            {
                augmented[row][column] = matrix[row][column];
            }
            augmented[row][6] = right_hand_side[row];
        }

        for (int column = 0; column < 6; ++column)
        {
            int pivot_row = column;
            double pivot_magnitude = std::fabs(augmented[column][column]);
            for (int row = column + 1; row < 6; ++row)
            {
                const double candidate = std::fabs(augmented[row][column]);
                if (candidate > pivot_magnitude)
                {
                    pivot_magnitude = candidate;
                    pivot_row = row;
                }
            }
            if (!std::isfinite(pivot_magnitude) || pivot_magnitude < 1e-24)
            {
                return false;
            }
            if (pivot_row != column)
            {
                for (int item = column; item < 7; ++item)
                {
                    const double saved = augmented[column][item];
                    augmented[column][item] = augmented[pivot_row][item];
                    augmented[pivot_row][item] = saved;
                }
            }
            for (int row = column + 1; row < 6; ++row)
            {
                const double factor = augmented[row][column] / augmented[column][column];
                for (int item = column; item < 7; ++item)
                {
                    augmented[row][item] -= factor * augmented[column][item];
                }
            }
        }

        for (int row = 5; row >= 0; --row)
        {
            double value = augmented[row][6];
            for (int column = row + 1; column < 6; ++column)
            {
                value -= augmented[row][column] * solution[column];
            }
            const double pivot = augmented[row][row];
            if (!std::isfinite(pivot) || std::fabs(pivot) < 1e-24)
            {
                return false;
            }
            solution[row] = value / pivot;
            if (!std::isfinite(solution[row]))
            {
                return false;
            }
        }
        return true;
    }

    void damped_matrix(const double jacobian[6][MaxJoints], double damping,
                       double matrix[6][6]) const
    {
        for (int row = 0; row < 6; ++row)
        {
            for (int column = 0; column < 6; ++column)
            {
                double value = 0.0;
                for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                {
                    value += jacobian[row][joint] * jacobian[column][joint];
                }
                if (row == column)
                {
                    value += damping * damping;
                }
                matrix[row][column] = value;
            }
        }
    }

    bool damped_task_step(const double jacobian[6][MaxJoints], const double error[6],
                          double damping, double step[MaxJoints]) const
    {
        double matrix[6][6];
        damped_matrix(jacobian, damping, matrix);
        double task_solution[6] = {};
        if (!solve_linear(matrix, error, task_solution))
        {
            return false;
        }
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            step[joint] = 0.0;
            for (int row = 0; row < 6; ++row)
            {
                step[joint] += jacobian[row][joint] * task_solution[row];
            }
        }
        return true;
    }

    bool nullspace_step(const double jacobian[6][MaxJoints], const double *joints,
                        const SerialChainSolveOptions &options, double damping,
                        double step[MaxJoints]) const
    {
        double gradient[MaxJoints] = {};
        double projected[6] = {};
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            gradient[joint] =
                options.preference_weight * (options.preferred_joints[joint] - joints[joint]);
            for (int row = 0; row < 6; ++row)
            {
                projected[row] += jacobian[row][joint] * gradient[joint];
            }
        }
        double matrix[6][6];
        damped_matrix(jacobian, damping, matrix);
        double task_component[6] = {};
        if (!solve_linear(matrix, projected, task_component))
        {
            return false;
        }
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            step[joint] = gradient[joint];
            for (int row = 0; row < 6; ++row)
            {
                step[joint] -= jacobian[row][joint] * task_component[row];
            }
        }
        return true;
    }

    void project_candidate(const double *seed, double max_joint_step, double candidate[MaxJoints],
                           bool &limit_clipped, bool &step_clipped) const
    {
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            const DhLink &link = spec_.links[joint];
            if (candidate[joint] < link.min_position)
            {
                candidate[joint] = link.min_position;
                limit_clipped = true;
            }
            else if (candidate[joint] > link.max_position)
            {
                candidate[joint] = link.max_position;
                limit_clipped = true;
            }
            const double step_lower = seed[joint] - max_joint_step;
            const double step_upper = seed[joint] + max_joint_step;
            if (candidate[joint] < step_lower)
            {
                candidate[joint] = step_lower;
                step_clipped = true;
            }
            else if (candidate[joint] > step_upper)
            {
                candidate[joint] = step_upper;
                step_clipped = true;
            }
            if (candidate[joint] < link.min_position)
            {
                candidate[joint] = link.min_position;
                limit_clipped = true;
            }
            else if (candidate[joint] > link.max_position)
            {
                candidate[joint] = link.max_position;
                limit_clipped = true;
            }
        }
    }

    double preference_cost(const double *joints, const SerialChainSolveOptions &options) const
    {
        double cost = 0.0;
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            const double difference = options.preferred_joints[joint] - joints[joint];
            cost += difference * difference;
        }
        return cost;
    }

    // Called only from the bounded outer loop. An accepted secondary step
    // returns through `continue`, so it consumes one of MaxIterations.
    bool apply_in_loop_preference_step(const Pose6 &target, const double *seed,
                                       double max_joint_step,
                                       const SerialChainSolveOptions &options,
                                       double joints[MaxJoints], const Pose6 &current) const
    {
        if (spec_.joint_count <= 6 || options.preference_weight == 0.0)
        {
            return false;
        }
        double jacobian[6][MaxJoints] = {};
        numerical_jacobian(joints, current, jacobian);
        double secondary_step[MaxJoints] = {};
        if (!nullspace_step(jacobian, joints, options, MinimumDamping, secondary_step))
        {
            return false;
        }
        const double current_cost = preference_cost(joints, options);
        double scale = 1.0;
        for (std::size_t line = 0; line < LineSearchSteps; ++line)
        {
            double candidate[MaxJoints] = {};
            for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
            {
                candidate[joint] = joints[joint] + scale * secondary_step[joint];
            }
            bool limit_clipped = false;
            bool step_clipped = false;
            project_candidate(seed, max_joint_step, candidate, limit_clipped, step_clipped);
            Pose6 candidate_pose{};
            forward_from(candidate, candidate_pose);
            double error[6];
            double candidate_position = 0.0;
            double candidate_orientation = 0.0;
            pose_error(candidate_pose, target, error, candidate_position, candidate_orientation);
            if (candidate_position <= options.position_tolerance &&
                candidate_orientation <= options.orientation_tolerance &&
                preference_cost(candidate, options) < current_cost)
            {
                for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                {
                    joints[joint] = candidate[joint];
                }
                return true;
            }
            scale *= 0.5;
        }
        return false;
    }

    SerialChainSolveResult solve_internal(const Pose6 &target, const double *seed,
                                          double max_joint_step,
                                          const SerialChainSolveOptions &options) const
    {
        SerialChainSolveResult result{};
        if (!valid_solve_inputs(target, seed, max_joint_step, options))
        {
            result.code = rt::ErrorCode::invalid_argument;
            return result;
        }

        double joints[MaxJoints] = {};
        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
        {
            if (seed[joint] < spec_.links[joint].min_position ||
                seed[joint] > spec_.links[joint].max_position)
            {
                result.code = rt::ErrorCode::limit_infeasible;
                return result;
            }
            joints[joint] = seed[joint];
            result.joints[joint] = seed[joint];
        }

        double best_score = 1e300;
        double damping = MinimumDamping;
        bool preference_applied = false;
        for (std::size_t iteration = 0; iteration < MaxIterations; ++iteration)
        {
            Pose6 current{};
            forward_from(joints, current);
            double error[6];
            double position_residual = 0.0;
            double orientation_residual = 0.0;
            pose_error(current, target, error, position_residual, orientation_residual);
            const double current_score = std::sqrt(position_residual * position_residual +
                                                   orientation_residual * orientation_residual);
            if (current_score < best_score)
            {
                best_score = current_score;
                result.position_residual = position_residual;
                result.orientation_residual = orientation_residual;
                result.iterations = iteration;
                for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                {
                    result.joints[joint] = joints[joint];
                }
            }

            if (position_residual <= options.position_tolerance &&
                orientation_residual <= options.orientation_tolerance)
            {
                if (!preference_applied &&
                    apply_in_loop_preference_step(target, seed, max_joint_step, options, joints,
                                                  current))
                {
                    preference_applied = true;
                    continue;
                }
                result.code = rt::ErrorCode::ok;
                result.position_residual = position_residual;
                result.orientation_residual = orientation_residual;
                result.iterations = iteration;
                for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                {
                    result.joints[joint] = joints[joint];
                }
                return result;
            }

            double jacobian[6][MaxJoints] = {};
            numerical_jacobian(joints, current, jacobian);
            bool accepted = false;
            bool any_limit_clipped = false;
            bool any_step_clipped = false;
            double attempt_damping = damping;
            for (std::size_t damping_attempt = 0; damping_attempt < DampingAttempts;
                 ++damping_attempt)
            {
                double step[MaxJoints] = {};
                if (!damped_task_step(jacobian, error, attempt_damping, step))
                {
                    attempt_damping *= 10.0;
                    if (attempt_damping > MaximumDamping)
                    {
                        attempt_damping = MaximumDamping;
                    }
                    continue;
                }
                double scale = 1.0;
                for (std::size_t line = 0; line < LineSearchSteps; ++line)
                {
                    double candidate[MaxJoints] = {};
                    for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                    {
                        candidate[joint] = joints[joint] + scale * step[joint];
                    }
                    bool limit_clipped = false;
                    bool step_clipped = false;
                    project_candidate(seed, max_joint_step, candidate, limit_clipped, step_clipped);
                    any_limit_clipped = any_limit_clipped || limit_clipped;
                    any_step_clipped = any_step_clipped || step_clipped;

                    Pose6 candidate_pose{};
                    forward_from(candidate, candidate_pose);
                    double candidate_error[6];
                    double candidate_position = 0.0;
                    double candidate_orientation = 0.0;
                    pose_error(candidate_pose, target, candidate_error, candidate_position,
                               candidate_orientation);
                    const double candidate_score =
                        std::sqrt(candidate_position * candidate_position +
                                  candidate_orientation * candidate_orientation);
                    if (candidate_score < current_score)
                    {
                        for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
                        {
                            joints[joint] = candidate[joint];
                        }
                        damping = attempt_damping * 0.2;
                        if (damping < MinimumDamping)
                        {
                            damping = MinimumDamping;
                        }
                        accepted = true;
                        break;
                    }
                    scale *= 0.5;
                }
                if (accepted)
                {
                    break;
                }
                attempt_damping *= 10.0;
                if (attempt_damping > MaximumDamping)
                {
                    attempt_damping = MaximumDamping;
                }
            }

            if (!accepted)
            {
                result.code = any_limit_clipped
                                  ? rt::ErrorCode::limit_infeasible
                                  : (any_step_clipped ? rt::ErrorCode::infeasible
                                                      : rt::ErrorCode::singular_region);
                return result;
            }
        }

        Pose6 final_pose{};
        forward_from(joints, final_pose);
        double final_error[6];
        double final_position = 0.0;
        double final_orientation = 0.0;
        pose_error(final_pose, target, final_error, final_position, final_orientation);
        const double final_score =
            std::sqrt(final_position * final_position + final_orientation * final_orientation);
        if (final_score < best_score)
        {
            result.position_residual = final_position;
            result.orientation_residual = final_orientation;
            for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
            {
                result.joints[joint] = joints[joint];
            }
        }
        if (final_position <= options.position_tolerance &&
            final_orientation <= options.orientation_tolerance)
        {
            result.code = rt::ErrorCode::ok;
            result.position_residual = final_position;
            result.orientation_residual = final_orientation;
            result.iterations = MaxIterations;
            for (std::size_t joint = 0; joint < spec_.joint_count; ++joint)
            {
                result.joints[joint] = joints[joint];
            }
            return result;
        }
        result.code = rt::ErrorCode::not_converged;
        result.iterations = MaxIterations;
        return result;
    }

    geom::RigidTransform link_transform(const DhLink &link, double joint) const
    {
        const double theta = joint + link.theta_offset;
        const double ct = std::cos(theta);
        const double st = std::sin(theta);
        const double ca = std::cos(link.alpha);
        const double sa = std::sin(link.alpha);
        geom::RigidTransform transform{};
        if (spec_.convention == DhConvention::standard)
        {
            transform.rotation[0][0] = ct;
            transform.rotation[0][1] = -st * ca;
            transform.rotation[0][2] = st * sa;
            transform.rotation[1][0] = st;
            transform.rotation[1][1] = ct * ca;
            transform.rotation[1][2] = -ct * sa;
            transform.rotation[2][0] = 0.0;
            transform.rotation[2][1] = sa;
            transform.rotation[2][2] = ca;
            transform.translation = geom::Vec3{link.a * ct, link.a * st, link.d};
            return transform;
        }

        transform.rotation[0][0] = ct;
        transform.rotation[0][1] = -st;
        transform.rotation[0][2] = 0.0;
        transform.rotation[1][0] = st * ca;
        transform.rotation[1][1] = ct * ca;
        transform.rotation[1][2] = -sa;
        transform.rotation[2][0] = st * sa;
        transform.rotation[2][1] = ct * sa;
        transform.rotation[2][2] = ca;
        transform.translation = geom::Vec3{link.a, -link.d * sa, link.d * ca};
        return transform;
    }

    static void copy_pose(const geom::RigidTransform &transform, Pose6 &pose)
    {
        pose.position[0] = transform.translation.x;
        pose.position[1] = transform.translation.y;
        pose.position[2] = transform.translation.z;
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                pose.rotation[row][column] = transform.rotation[row][column];
            }
        }
    }

    SerialChainSpec spec_{};
    bool valid_ = false;
};

} // namespace plcopen::core::kin
