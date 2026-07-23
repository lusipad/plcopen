// H2 acceptance tests (approved kinematics matrix v2/v2.1): fixed-capacity
// DH/modified-DH serial-chain forward kinematics and deterministic numerical
// inverse kinematics through the public PoseKinematics seam.

#include <cmath>
#include <cstdio>

#include "kin/serial_chain.h"
#include "test_support/serial_chain_fixture.h"

namespace
{

using namespace plcopen::core;

constexpr double Pi = 3.14159265358979323846;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool close(double actual, double expected, double tolerance = 1e-10)
{
    return std::fabs(actual - expected) <= tolerance;
}

struct Lcg
{
    unsigned state = 0x243F6A88u;

    double range(double minimum, double maximum)
    {
        state = state * 1664525u + 1013904223u;
        return minimum + (static_cast<double>(state >> 8) / 16777216.0) * (maximum - minimum);
    }
};

kin::SerialChainSpec ur_like_spec()
{
    kin::SerialChainSpec spec{};
    spec.joint_count = 6;
    spec.convention = kin::DhConvention::standard;
    constexpr double Lower = -2.0 * Pi;
    constexpr double Upper = 2.0 * Pi;
    spec.links[0] = {0.0, Pi * 0.5, 0.089159, 0.0, Lower, Upper};
    spec.links[1] = {-0.425, 0.0, 0.0, 0.0, Lower, Upper};
    spec.links[2] = {-0.39225, 0.0, 0.0, 0.0, Lower, Upper};
    spec.links[3] = {0.0, Pi * 0.5, 0.10915, 0.0, Lower, Upper};
    spec.links[4] = {0.0, -Pi * 0.5, 0.09465, 0.0, Lower, Upper};
    spec.links[5] = {0.0, 0.0, 0.0823, 0.0, Lower, Upper};
    return spec;
}

void pose_residual(const kin::Pose6 &actual, const kin::Pose6 &expected, double &position,
                   double &orientation)
{
    const double dx = expected.position[0] - actual.position[0];
    const double dy = expected.position[1] - actual.position[1];
    const double dz = expected.position[2] - actual.position[2];
    position = std::sqrt(dx * dx + dy * dy + dz * dz);
    double relative[3][3];
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            relative[row][column] = actual.rotation[0][row] * expected.rotation[0][column] +
                                    actual.rotation[1][row] * expected.rotation[1][column] +
                                    actual.rotation[2][row] * expected.rotation[2][column];
        }
    }
    const double skew[3] = {0.5 * (relative[2][1] - relative[1][2]),
                            0.5 * (relative[0][2] - relative[2][0]),
                            0.5 * (relative[1][0] - relative[0][1])};
    const double sine = std::sqrt(skew[0] * skew[0] + skew[1] * skew[1] + skew[2] * skew[2]);
    double cosine = 0.5 * (relative[0][0] + relative[1][1] + relative[2][2] - 1.0);
    cosine = cosine > 1.0 ? 1.0 : (cosine < -1.0 ? -1.0 : cosine);
    orientation = std::atan2(sine, cosine);
}

int check_dh_forward_examples()
{
    kin::SerialChainSpec standard{};
    standard.joint_count = 2;
    standard.convention = kin::DhConvention::standard;
    standard.links[0] = {1.0, 0.0, 0.0, 0.0, -Pi, Pi};
    standard.links[1] = {0.5, 0.0, 0.0, 0.0, -Pi, Pi};
    const kin::SerialChain standard_chain(standard);
    const double q_standard[2] = {Pi * 0.5, -Pi * 0.5};
    kin::Pose6 standard_pose{};
    standard_chain.forward(q_standard, standard_pose);
    if (!close(standard_pose.position[0], 0.5) || !close(standard_pose.position[1], 1.0) ||
        !close(standard_pose.position[2], 0.0) || !close(standard_pose.rotation[0][0], 1.0) ||
        !close(standard_pose.rotation[1][1], 1.0) || !close(standard_pose.rotation[2][2], 1.0))
    {
        return fail("standard DH forward worked example");
    }

    kin::SerialChainSpec modified{};
    modified.joint_count = 1;
    modified.convention = kin::DhConvention::modified;
    modified.links[0] = {0.4, Pi * 0.5, 0.2, 0.0, -Pi, Pi};
    const kin::SerialChain modified_chain(modified);
    const double q_modified[1] = {Pi * 0.5};
    kin::Pose6 modified_pose{};
    modified_chain.forward(q_modified, modified_pose);
    if (!close(modified_pose.position[0], 0.4) || !close(modified_pose.position[1], -0.2) ||
        !close(modified_pose.position[2], 0.0) || !close(modified_pose.rotation[0][0], 0.0) ||
        !close(modified_pose.rotation[0][1], -1.0) || !close(modified_pose.rotation[1][2], -1.0) ||
        !close(modified_pose.rotation[2][0], 1.0))
    {
        return fail("modified DH forward worked example");
    }
    const double modified_seed[1] = {Pi * 0.5 - 0.01};
    double modified_solved[1] = {};
    if (modified_chain.inverse(modified_pose, modified_seed, 0.1, modified_solved) !=
            rt::ErrorCode::ok ||
        !close(modified_solved[0], q_modified[0], 1e-6))
    {
        return fail("modified DH numerical inverse");
    }
    return 0;
}

int check_fixed_pose_oracles()
{
    const kin::SerialChain ur_like(ur_like_spec());
    const double ur_joints[6] = {0.3, -0.8, 1.1, -0.5, 0.7, -0.2};
    const double ur_position[3] = {-0.65761706418974819, -0.38356709588501103, 0.19588750820416417};
    const double ur_rotation[3][3] = {
        {0.85071978417198768, 0.36610573129081581, -0.37715042401458837},
        {-0.39773560626185733, -0.020720087282554096, -0.91726608216728267},
        {-0.34363095950434908, 0.93034255599699434, 0.12798629680985407}};
    kin::Pose6 ur_pose{};
    ur_like.forward(ur_joints, ur_pose);

    const kin::SerialChain seven_dof(test_support::seven_dof_arm_spec());
    const double seven_joints[7] = {0.2, -0.6, 0.8, -1.0, 0.7, 0.5, -0.3};
    const double seven_position[3] = {-0.18906522312996427, 0.32207984313793769,
                                      1.0287996660808150};
    const double seven_rotation[3][3] = {
        {0.32538577778236061, -0.94212906116227901, 0.080727490549693304},
        {0.33601366451587766, 0.19500545688903856, 0.92144869040121202},
        {-0.88386589077505406, -0.27270075888596268, 0.38002024054968508}};
    kin::Pose6 seven_pose{};
    seven_dof.forward(seven_joints, seven_pose);

    for (int row = 0; row < 3; ++row)
    {
        if (!close(ur_pose.position[row], ur_position[row], 1e-12) ||
            !close(seven_pose.position[row], seven_position[row], 1e-12))
        {
            return fail("fixed serial-chain position oracle");
        }
        for (int column = 0; column < 3; ++column)
        {
            if (!close(ur_pose.rotation[row][column], ur_rotation[row][column], 1e-12) ||
                !close(seven_pose.rotation[row][column], seven_rotation[row][column], 1e-12))
            {
                return fail("fixed serial-chain rotation oracle");
            }
        }
    }

    kin::SerialChainSpec rotation_spec{};
    rotation_spec.joint_count = 1;
    rotation_spec.links[0] = {0.0, 0.0, 0.0, 0.0, -Pi, Pi};
    const kin::SerialChain rotation_only(rotation_spec);
    const double zero[1] = {0.0};
    for (const double expected : {0.2, -0.2})
    {
        kin::Pose6 target{};
        rotation_only.forward(&expected, target);
        double solved[1] = {};
        if (rotation_only.inverse(target, zero, 0.5, solved) != rt::ErrorCode::ok ||
            !close(solved[0], expected, 1e-8) || (expected > 0.0) != (solved[0] > 0.0))
        {
            return fail("SO3 log current-to-target direction");
        }
    }
    return 0;
}

int check_ur_like_warm_seed_inverse()
{
    const kin::SerialChain chain(ur_like_spec());
    const double expected[6] = {0.3, -0.8, 1.1, -0.5, 0.7, -0.2};
    const double seed[6] = {0.31, -0.81, 1.11, -0.49, 0.69, -0.19};
    kin::Pose6 target{};
    chain.forward(expected, target);
    double solved[6] = {};
    if (chain.inverse(target, seed, 0.25, solved) != rt::ErrorCode::ok)
    {
        return fail("UR-like warm-seed inverse");
    }
    kin::Pose6 reached{};
    chain.forward(solved, reached);
    double position = 0.0;
    double orientation = 0.0;
    pose_residual(reached, target, position, orientation);
    if (position > 1e-9 || orientation > 1e-9)
    {
        std::printf("residual p=%.12g r=%.12g\n", position, orientation);
        return fail("UR-like dual convergence gate");
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (std::fabs(solved[i] - expected[i]) > 1e-6)
        {
            return fail("UR-like seed-local branch");
        }
    }

    kin::Pose6 unreachable = target;
    unreachable.position[0] += 10.0;
    double untouched[6] = {91.0, 92.0, 93.0, 94.0, 95.0, 96.0};
    const rt::ErrorCode unreachable_code = chain.inverse(unreachable, seed, 0.25, untouched);
    if (unreachable_code != rt::ErrorCode::not_converged)
    {
        std::printf("unreachable code=%d\n", static_cast<int>(unreachable_code));
        return fail("workspace-outside target exhausts bounded solve");
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (untouched[i] != 91.0 + static_cast<double>(i))
        {
            return fail("strict inverse keeps failed output untouched");
        }
    }
    return 0;
}

int check_redundant_preference_and_determinism()
{
    const kin::SerialChainSpec spec = test_support::seven_dof_arm_spec();
    const kin::SerialChain chain(spec);
    const double seed[7] = {0.2, -0.6, 0.8, -1.0, 0.7, 0.5, -0.3};
    kin::Pose6 target{};
    chain.forward(seed, target);

    double minimum_norm[7] = {};
    if (chain.inverse(target, seed, 2.0, minimum_norm) != rt::ErrorCode::ok)
    {
        return fail("7DOF minimum-norm solve");
    }
    for (std::size_t i = 0; i < 7; ++i)
    {
        if (minimum_norm[i] != seed[i])
        {
            return fail("7DOF zero preference keeps seed solution");
        }
    }

    const double preferred[7] = {0.5, -0.3, 0.4, -0.7, 0.4, 0.2, 0.1};
    kin::SerialChainSolveOptions options{};
    options.preferred_joints = preferred;
    options.preference_weight = 1e-4;
    const kin::SerialChainSolveResult first_result =
        chain.solve_best_effort(target, seed, 2.0, options);
    const kin::SerialChainSolveResult second_result =
        chain.solve_best_effort(target, seed, 2.0, options);
    if (first_result.code != rt::ErrorCode::ok || second_result.code != rt::ErrorCode::ok ||
        first_result.iterations > kin::SerialChain::MaxIterations ||
        second_result.iterations > kin::SerialChain::MaxIterations)
    {
        return fail("7DOF preferred solve");
    }
    const double *first = first_result.joints.data();
    const double *second = second_result.joints.data();
    double seed_cost = 0.0;
    double solved_cost = 0.0;
    bool upstream_joint_moved = false;
    for (std::size_t i = 0; i < 7; ++i)
    {
        const double seed_difference = seed[i] - preferred[i];
        const double solved_difference = first[i] - preferred[i];
        seed_cost += seed_difference * seed_difference;
        solved_cost += solved_difference * solved_difference;
        if (i < 6 && first[i] != seed[i])
        {
            upstream_joint_moved = true;
        }
    }
    if (!(solved_cost < seed_cost) || !upstream_joint_moved)
    {
        std::printf("preference seed_cost=%.17g solved_cost=%.17g "
                    "q={%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g}\n",
                    seed_cost, solved_cost, first[0], first[1], first[2], first[3], first[4],
                    first[5], first[6]);
        return fail("7DOF preference biases redundant solution");
    }
    for (std::size_t i = 0; i < 7; ++i)
    {
        if (first[i] != second[i])
        {
            return fail("7DOF deterministic result");
        }
    }
    kin::Pose6 reached{};
    chain.forward(first, reached);
    double position = 0.0;
    double orientation = 0.0;
    pose_residual(reached, target, position, orientation);
    if (position > options.position_tolerance || orientation > options.orientation_tolerance)
    {
        return fail("7DOF preference preserves primary pose");
    }
    return 0;
}

int check_failure_classification_and_best_effort()
{
    const kin::SerialChain chain(ur_like_spec());
    const double target_joints[6] = {0.35310894250869751, 0.11347472667694092,
                                     0.34137189388275146, -1.1558330059051514,
                                     1.2696322798728943,  -0.095789134502410889};
    const double distant_seed[6] = {0.065711736679077148, -2.2440433502197266, -1.5994098782539368,
                                    -1.4418348670005798,  1.0893398523330688,  -2.0330539345741272};
    kin::Pose6 target{};
    chain.forward(target_joints, target);
    const kin::SerialChainSolveResult exhausted =
        chain.solve_best_effort(target, distant_seed, 6.0);
    bool best_moved = false;
    for (std::size_t i = 0; i < 6; ++i)
    {
        best_moved = best_moved || exhausted.joints[i] != distant_seed[i];
    }
    kin::Pose6 best_pose{};
    chain.forward(exhausted.joints.data(), best_pose);
    double measured_position = 0.0;
    double measured_orientation = 0.0;
    pose_residual(best_pose, target, measured_position, measured_orientation);
    if (exhausted.code != rt::ErrorCode::not_converged ||
        exhausted.iterations != kin::SerialChain::MaxIterations || !best_moved ||
        !std::isfinite(exhausted.position_residual) ||
        !std::isfinite(exhausted.orientation_residual) ||
        std::fabs(exhausted.position_residual - measured_position) > 1e-12 ||
        std::fabs(exhausted.orientation_residual - measured_orientation) > 1e-12 ||
        (exhausted.position_residual <= 1e-9 && exhausted.orientation_residual <= 1e-9))
    {
        std::printf("exhausted code=%d iterations=%zu p=%.12g r=%.12g\n",
                    static_cast<int>(exhausted.code), exhausted.iterations,
                    exhausted.position_residual, exhausted.orientation_residual);
        return fail("iteration exhaustion reports best effort");
    }
    double strict_output[6] = {31.0, 32.0, 33.0, 34.0, 35.0, 36.0};
    if (chain.inverse(target, distant_seed, 6.0, strict_output) != rt::ErrorCode::not_converged)
    {
        return fail("iteration exhaustion strict code");
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (strict_output[i] != 31.0 + static_cast<double>(i))
        {
            return fail("iteration exhaustion strict output");
        }
    }

    kin::SerialChainSpec limited_spec{};
    limited_spec.joint_count = 1;
    limited_spec.convention = kin::DhConvention::standard;
    limited_spec.links[0] = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const kin::SerialChain limited(limited_spec);
    kin::SerialChainSpec free_spec = limited_spec;
    free_spec.links[0].min_position = -Pi;
    free_spec.links[0].max_position = Pi;
    const kin::SerialChain free_joint(free_spec);
    const double displaced[1] = {0.5};
    const double zero[1] = {0.0};
    kin::Pose6 limited_target{};
    free_joint.forward(displaced, limited_target);
    const kin::SerialChainSolveResult limited_result =
        limited.solve_best_effort(limited_target, zero, 1.0);
    if (limited_result.code != rt::ErrorCode::limit_infeasible)
    {
        return fail("hard limit failure code");
    }

    kin::SerialChainSpec singular_spec{};
    singular_spec.joint_count = 1;
    singular_spec.convention = kin::DhConvention::standard;
    singular_spec.links[0] = {0.0, 0.0, 0.0, 0.0, -Pi, Pi};
    const kin::SerialChain singular(singular_spec);
    kin::Pose6 singular_target{};
    singular.forward(zero, singular_target);
    singular_target.position[0] = 1.0;
    if (singular.solve_best_effort(singular_target, zero, 1.0).code !=
        rt::ErrorCode::singular_region)
    {
        return fail("singular region failure code");
    }

    double gated_output[6] = {};
    const double near_target[6] = {0.3, -0.8, 1.1, -0.5, 0.7, -0.2};
    const double near_seed[6] = {0.31, -0.81, 1.11, -0.49, 0.69, -0.19};
    kin::Pose6 gated_target{};
    chain.forward(near_target, gated_target);
    if (chain.inverse(gated_target, near_seed, 1e-4, gated_output) != rt::ErrorCode::infeasible)
    {
        return fail("seed step gate failure code");
    }

    kin::SerialChainSpec invalid_spec{};
    const kin::SerialChain invalid(invalid_spec);
    double invalid_output[1] = {17.0};
    if (invalid.inverse(singular_target, zero, 1.0, invalid_output) !=
            rt::ErrorCode::invalid_argument ||
        invalid_output[0] != 17.0)
    {
        return fail("invalid serial-chain spec");
    }
    invalid_spec = singular_spec;
    invalid_spec.convention = static_cast<kin::DhConvention>(99);
    const kin::SerialChain invalid_convention(invalid_spec);
    if (invalid_convention.inverse(singular_target, zero, 1.0, invalid_output) !=
        rt::ErrorCode::invalid_argument)
    {
        return fail("invalid DH convention");
    }
    kin::SerialChainSolveOptions invalid_options{};
    invalid_options.position_tolerance = 0.0;
    if (chain.solve(target, distant_seed, 6.0, invalid_options, strict_output) !=
        rt::ErrorCode::invalid_argument)
    {
        return fail("invalid solve options");
    }
    return 0;
}

int check_roundtrip_fuzz()
{
    const kin::SerialChain chain(ur_like_spec());
    const kin::SerialChainSpec redundant_spec = test_support::seven_dof_arm_spec();
    const kin::SerialChain redundant(redundant_spec);
    Lcg rng;
    for (int sample = 0; sample < 20000; ++sample)
    {
        double expected[7] = {rng.range(-1.5, 1.5),  rng.range(-1.2, -0.2), rng.range(0.3, 1.4),
                              rng.range(-1.4, -0.4), rng.range(0.3, 1.4),   rng.range(-1.4, 1.4),
                              rng.range(-1.4, 1.4)};
        double seed[7];
        for (int joint = 0; joint < 7; ++joint)
        {
            seed[joint] = expected[joint] + rng.range(-0.01, 0.01);
        }

        kin::Pose6 target{};
        chain.forward(expected, target);
        double solved[6] = {};
        if (chain.inverse(target, seed, 0.25, solved) != rt::ErrorCode::ok)
        {
            std::printf("6DOF fuzz failure at %d\n", sample);
            return fail("UR-like roundtrip fuzz solves");
        }
        for (int joint = 0; joint < 6; ++joint)
        {
            if (std::fabs(solved[joint] - expected[joint]) > 1e-6)
            {
                return fail("UR-like roundtrip fuzz identity");
            }
        }

        redundant.forward(expected, target);
        double redundant_solved[7] = {};
        const rt::ErrorCode redundant_code =
            redundant.inverse(target, seed, 0.25, redundant_solved);
        if (redundant_code != rt::ErrorCode::ok)
        {
            const kin::SerialChainSolveResult detail =
                redundant.solve_best_effort(target, seed, 0.25);
            std::printf("7DOF fuzz failure at %d code=%d p=%.12g r=%.12g "
                        "q={%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g} "
                        "seed={%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g}\n",
                        sample, static_cast<int>(redundant_code), detail.position_residual,
                        detail.orientation_residual, expected[0], expected[1], expected[2],
                        expected[3], expected[4], expected[5], expected[6], seed[0], seed[1],
                        seed[2], seed[3], seed[4], seed[5], seed[6]);
            return fail("7DOF roundtrip fuzz solves");
        }
        kin::Pose6 reached{};
        redundant.forward(redundant_solved, reached);
        double position = 0.0;
        double orientation = 0.0;
        pose_residual(reached, target, position, orientation);
        if (position > 1e-9 || orientation > 1e-9)
        {
            return fail("7DOF roundtrip fuzz pose gate");
        }
        for (int joint = 0; joint < 7; ++joint)
        {
            if (redundant_solved[joint] < redundant_spec.links[joint].min_position ||
                redundant_solved[joint] > redundant_spec.links[joint].max_position)
            {
                return fail("7DOF roundtrip fuzz limits");
            }
        }
    }
    return 0;
}

} // namespace

int main()
{
    if (check_dh_forward_examples() != 0 || check_fixed_pose_oracles() != 0 ||
        check_ur_like_warm_seed_inverse() != 0 ||
        check_redundant_preference_and_determinism() != 0 ||
        check_failure_classification_and_best_effort() != 0 || check_roundtrip_fuzz() != 0)
    {
        return 1;
    }
    std::printf("PASS serial chain tests\n");
    return 0;
}
