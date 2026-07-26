// H3 public-contract tests: analytic pendulums, model rejection, atomic input
// rejection, and deterministic fixed-capacity evaluation.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "dyn/fixed_base_chain.h"
#include "test_support/dynamics_fixture.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool close(double actual, double expected, double tolerance = 1e-10)
{
    return std::fabs(actual - expected) <= tolerance;
}

dyn::FixedBaseChainSpec one_link_spec()
{
    dyn::FixedBaseChainSpec spec{};
    spec.joint_count = 1;
    dyn::RevoluteBody &body = spec.bodies[0];
    body.joint_axis = {0.0, 0.0, 1.0};
    body.mass = 2.0;
    body.center_of_mass = {0.4, 0.0, 0.0};
    body.inertia_com[0][0] = 0.20;
    body.inertia_com[1][1] = 0.25;
    body.inertia_com[2][2] = 0.30;
    return spec;
}

dyn::FixedBaseChainSpec two_link_spec()
{
    dyn::FixedBaseChainSpec spec = one_link_spec();
    spec.joint_count = 2;
    dyn::RevoluteBody &second = spec.bodies[1];
    second.parent_from_joint_zero.translation = {0.8, 0.0, 0.0};
    second.joint_axis = {0.0, 0.0, 1.0};
    second.mass = 1.5;
    second.center_of_mass = {0.3, 0.0, 0.0};
    second.inertia_com[0][0] = 0.10;
    second.inertia_com[1][1] = 0.14;
    second.inertia_com[2][2] = 0.18;
    return spec;
}

int check_one_link_analytic()
{
    const dyn::FixedBaseChain chain(one_link_spec());
    if (!chain.valid() || chain.joint_count() != 1)
    {
        return fail("one-link model validity");
    }
    constexpr double Q = 0.37;
    constexpr double Ddq = -1.2;
    constexpr double GravityMagnitude = 9.81;
    const double q[1] = {Q};
    const double dq[1] = {0.7};
    const double ddq[1] = {Ddq};
    const double gravity[3] = {0.0, -GravityMagnitude, 0.0};
    double tau[1] = {};
    if (chain.inverse_dynamics(q, dq, ddq, gravity, tau) != rt::ErrorCode::ok)
    {
        return fail("one-link inverse dynamics");
    }
    const double expected =
        (0.30 + 2.0 * 0.4 * 0.4) * Ddq + 2.0 * GravityMagnitude * 0.4 * std::cos(Q);
    if (!close(tau[0], expected))
    {
        std::printf("one-link actual=%.17g expected=%.17g\n", tau[0], expected);
        return fail("one-link inertia and gravity");
    }
    return 0;
}

int check_two_link_analytic()
{
    const dyn::FixedBaseChain chain(two_link_spec());
    const double q[2] = {0.31, -0.52};
    const double dq[2] = {0.8, -0.4};
    const double ddq[2] = {1.1, -0.9};
    constexpr double GravityMagnitude = 9.81;
    const double gravity[3] = {0.0, -GravityMagnitude, 0.0};
    double tau[2] = {};
    if (chain.inverse_dynamics(q, dq, ddq, gravity, tau) != rt::ErrorCode::ok)
    {
        return fail("two-link inverse dynamics");
    }

    constexpr double M1 = 2.0;
    constexpr double M2 = 1.5;
    constexpr double L1 = 0.8;
    constexpr double C1 = 0.4;
    constexpr double C2 = 0.3;
    constexpr double I1 = 0.30;
    constexpr double I2 = 0.18;
    const double cos_q2 = std::cos(q[1]);
    const double sin_q2 = std::sin(q[1]);
    const double m11 =
        I1 + I2 + M1 * C1 * C1 + M2 * (L1 * L1 + C2 * C2 + 2.0 * L1 * C2 * cos_q2);
    const double m12 = I2 + M2 * (C2 * C2 + L1 * C2 * cos_q2);
    const double m22 = I2 + M2 * C2 * C2;
    const double h = -M2 * L1 * C2 * sin_q2;
    const double coriolis1 = h * (2.0 * dq[0] * dq[1] + dq[1] * dq[1]);
    const double coriolis2 = -h * dq[0] * dq[0];
    const double gravity1 =
        GravityMagnitude *
        (M1 * C1 * std::cos(q[0]) +
         M2 * (L1 * std::cos(q[0]) + C2 * std::cos(q[0] + q[1])));
    const double gravity2 = GravityMagnitude * M2 * C2 * std::cos(q[0] + q[1]);
    const double expected[2] = {m11 * ddq[0] + m12 * ddq[1] + coriolis1 + gravity1,
                                m12 * ddq[0] + m22 * ddq[1] + coriolis2 + gravity2};
    if (!close(tau[0], expected[0], 1e-9) || !close(tau[1], expected[1], 1e-9))
    {
        std::printf("two-link actual={%.17g,%.17g} expected={%.17g,%.17g}\n", tau[0],
                    tau[1], expected[0], expected[1]);
        return fail("two-link coupled closed form");
    }
    return 0;
}

int check_model_rejection()
{
    dyn::FixedBaseChainSpec spec = one_link_spec();
    spec.joint_count = 0;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("zero joint count rejected");
    }
    spec = one_link_spec();
    spec.joint_count = dyn::FixedBaseChain::MaxJoints + 1;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("excess joint count rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].parent_from_joint_zero.rotation[0][0] = 2.0;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-orthonormal rotation rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].parent_from_joint_zero.rotation[0][0] = -1.0;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("reflection rotation rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].parent_from_joint_zero.translation.y =
        std::numeric_limits<double>::quiet_NaN();
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-finite translation rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].joint_axis = {0.0, 0.0, 2.0};
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-unit axis rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].joint_axis.x = std::numeric_limits<double>::infinity();
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-finite axis rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].mass = 0.0;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-positive mass rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].center_of_mass.x = std::numeric_limits<double>::infinity();
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-finite center of mass rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].inertia_com[0][1] = 0.01;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("asymmetric inertia rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].inertia_com[2][2] =
        std::numeric_limits<double>::quiet_NaN();
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-finite inertia rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].inertia_com[0][0] = -0.01;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("non-positive-definite inertia rejected");
    }
    spec = one_link_spec();
    spec.bodies[0].inertia_com[0][0] = 0.8;
    if (dyn::FixedBaseChain(spec).valid())
    {
        return fail("principal-inertia triangle rejected");
    }
    return 0;
}

int check_input_rejection_and_atomicity()
{
    const dyn::FixedBaseChain chain(two_link_spec());
    const double q[2] = {0.1, 0.2};
    const double dq[2] = {0.3, 0.4};
    const double ddq[2] = {0.5, 0.6};
    const double gravity[3] = {0.0, 0.0, -9.81};
    const double original[2] = {91.25, -72.5};
    double tau[2] = {original[0], original[1]};
    if (chain.inverse_dynamics(nullptr, dq, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("null input leaves output untouched");
    }
    if (chain.inverse_dynamics(q, nullptr, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0 ||
        chain.inverse_dynamics(q, dq, nullptr, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0 ||
        chain.inverse_dynamics(q, dq, ddq, nullptr, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("null state vectors leave output untouched");
    }
    if (chain.inverse_dynamics(q, dq, ddq, gravity, nullptr) !=
        rt::ErrorCode::invalid_argument)
    {
        return fail("null output rejected");
    }
    double bad_q[2] = {q[0], std::numeric_limits<double>::quiet_NaN()};
    if (chain.inverse_dynamics(bad_q, dq, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("NaN joint leaves output untouched");
    }
    double bad_dq[2] = {dq[0], std::numeric_limits<double>::infinity()};
    double bad_ddq[2] = {ddq[0], std::numeric_limits<double>::quiet_NaN()};
    if (chain.inverse_dynamics(q, bad_dq, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0 ||
        chain.inverse_dynamics(q, dq, bad_ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("non-finite state leaves output untouched");
    }
    double bad_gravity[3] = {0.0, std::numeric_limits<double>::infinity(), 0.0};
    if (chain.inverse_dynamics(q, dq, ddq, bad_gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("infinite gravity leaves output untouched");
    }
    double overflowing_dq[2] = {1e308, 1e308};
    if (chain.inverse_dynamics(q, overflowing_dq, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0)
    {
        return fail("non-finite result leaves output untouched");
    }
    dyn::FixedBaseChainSpec invalid_spec = one_link_spec();
    invalid_spec.bodies[0].mass = -1.0;
    const dyn::FixedBaseChain invalid_chain(invalid_spec);
    if (invalid_chain.inverse_dynamics(q, dq, ddq, gravity, tau) !=
            rt::ErrorCode::invalid_argument ||
        std::memcmp(tau, original, sizeof(tau)) != 0 || invalid_chain.joint_count() != 0)
    {
        return fail("invalid model leaves output untouched");
    }
    return 0;
}

int check_determinism()
{
    const dyn::FixedBaseChain chain(test_support::eight_joint_dynamics_spec());
    if (!chain.valid() || chain.joint_count() != 8)
    {
        return fail("spatial fixture validity");
    }
    const double q[8] = {0.2, -0.3, 0.4, -0.5, 0.6, -0.7, 0.8, -0.9};
    const double dq[8] = {-0.4, 0.3, -0.2, 0.1, 0.2, -0.3, 0.4, -0.5};
    const double ddq[8] = {0.7, -0.6, 0.5, -0.4, 0.3, -0.2, 0.1, 0.0};
    const double gravity[3] = {0.3, -0.2, -9.7};
    double first[8] = {};
    double second[8] = {};
    if (chain.inverse_dynamics(q, dq, ddq, gravity, first) != rt::ErrorCode::ok ||
        chain.inverse_dynamics(q, dq, ddq, gravity, second) != rt::ErrorCode::ok ||
        std::memcmp(first, second, sizeof(first)) != 0)
    {
        return fail("deterministic spatial result");
    }
    return 0;
}

} // namespace

int main()
{
    if (check_one_link_analytic() != 0 || check_two_link_analytic() != 0 ||
        check_model_rejection() != 0 || check_input_rejection_and_atomicity() != 0 ||
        check_determinism() != 0)
    {
        return 1;
    }
    std::puts("PASS H3 fixed-base inverse dynamics contract tests");
    return 0;
}
