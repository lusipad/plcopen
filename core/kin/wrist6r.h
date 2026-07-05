#pragma once

// BS3.5: spherical-wrist 6R analytic inverse kinematics (approved kinematics
// matrix, decision #7 item 3 — the standalone batch). Pre-integration form:
// full 6-DOF poses (position + rotation matrix) do not fit the v1 Kinematics
// ABI (2/3 translational coordinates), so this mechanism ships as its own
// class with the same contract discipline (analytic, seed-branch selection,
// singularity margins, no allocation, no exceptions); the group wiring
// arrives with the orientation (RPY) batch.
//
// Geometry (Pieper-compliant articulated arm):
//   q1 about the base Z; shoulder at height d1; upper arm a2 and forearm d4
//   swing about the shoulder Y axis (q2, q3 measured from the vertical);
//   spherical wrist ZYZ (q4 about Z3, q5 about Y, q6 about Z) at the wrist
//   center; tool point offset d6 along the tool Z.
//   R = Rz(q1) * Ry(q2+q3) * Rz(q4) * Ry(q5) * Rz(q6)
//   wc = [c1*r, s1*r, d1 + z],  r = a2*s2 + d4*s23,  z = a2*c2 + d4*c23
//   p  = wc + d6 * R * ez
//
// Eight solution branches: shoulder (q1 vs q1+pi with the reach mirrored),
// elbow (q3 sign), wrist (q5 sign). inverse() enumerates all eight and picks
// the branch closest to the seed (largest joint deviation minimized, each
// revolute wrapped into the seed's turn); no same-turn candidate inside the
// step bound is `infeasible` — never an implicit branch flip.

#include <cmath>
#include <cstddef>

#include "kin/pose.h"
#include "rt/error.h"

namespace plcopen::core::kin
{

class SphericalWrist6R final : public PoseKinematics
{
public:
    SphericalWrist6R(double base_height, double upper_arm, double forearm, double tool)
        : d1_(base_height)
        , a2_(upper_arm)
        , d4_(forearm)
        , d6_(tool)
    {
    }

    static constexpr std::size_t JointCount = 6;

    std::size_t joint_count() const override
    {
        return JointCount;
    }

    void forward(const double *q, Pose6 &pose) const override
    {
        double rotation[3][3];
        compose_rotation(q, rotation);
        const double s2 = std::sin(q[1]);
        const double c2 = std::cos(q[1]);
        const double s23 = std::sin(q[1] + q[2]);
        const double c23 = std::cos(q[1] + q[2]);
        const double radius = a2_ * s2 + d4_ * s23;
        const double height = a2_ * c2 + d4_ * c23;
        const double c1 = std::cos(q[0]);
        const double s1 = std::sin(q[0]);
        const double wc[3] = {c1 * radius, s1 * radius, d1_ + height};
        for(int i = 0; i < 3; ++i) {
            pose.position[i] = wc[i] + d6_ * rotation[i][2];
            for(int j = 0; j < 3; ++j) {
                pose.rotation[i][j] = rotation[i][j];
            }
        }
    }

    // max_joint_step bounds the seed distance per joint (same-turn wrapped);
    // it is the "no branch flip" gate, not a numeric tolerance.
    rt::ErrorCode inverse(const Pose6 &pose,
                          const double *seed,
                          double max_joint_step,
                          double *joints_out) const override
    {
        // Wrist center from the tool pose.
        const double wc[3] = {pose.position[0] - d6_ * pose.rotation[0][2],
                              pose.position[1] - d6_ * pose.rotation[1][2],
                              pose.position[2] - d6_ * pose.rotation[2][2]};
        const double planar = std::sqrt(wc[0] * wc[0] + wc[1] * wc[1]);
        const double lift = wc[2] - d1_;

        double best[JointCount] = {};
        double best_score = 1e300;
        bool found = false;

        for(int shoulder = 0; shoulder < 2; ++shoulder) {
            // Shoulder branch: q1 faces the wrist center, or looks away with
            // the reach mirrored through the base axis.
            double q1 = std::atan2(wc[1], wc[0]);
            double reach = planar;
            if(shoulder == 1) {
                q1 += Pi;
                reach = -planar;
            }

            // Planar 2R (a2, d4) in the (reach, lift) plane, angles measured
            // from the vertical: reach = a2*s2 + d4*s23, lift = a2*c2 + d4*c23.
            const double radius_squared = reach * reach + lift * lift;
            const double cos_elbow =
                (radius_squared - a2_ * a2_ - d4_ * d4_) / (2.0 * a2_ * d4_);
            if(cos_elbow > 1.0 + 1e-12 || cos_elbow < -1.0 - 1e-12) {
                continue; // out of the annular workspace on this branch
            }
            const double clamped =
                cos_elbow > 1.0 ? 1.0 : (cos_elbow < -1.0 ? -1.0 : cos_elbow);
            const double elbow_magnitude = std::acos(clamped);

            for(int elbow = 0; elbow < 2; ++elbow) {
                const double q3 = elbow == 0 ? elbow_magnitude : -elbow_magnitude;
                // From the vertical: q2 = atan2(reach, lift) - atan2(d4*s3, a2 + d4*c3)
                const double q2 = std::atan2(reach, lift) -
                                  std::atan2(d4_ * std::sin(q3),
                                             a2_ + d4_ * std::cos(q3));

                // Wrist: R36 = Ry(q2+q3)^T * Rz(q1)^T * R = Rz(q4) Ry(q5) Rz(q6).
                double r36[3][3];
                arm_transpose_times(q1, q2 + q3, pose.rotation, r36);
                for(int wrist = 0; wrist < 2; ++wrist) {
                    double q4;
                    double q5;
                    double q6;
                    const double sin_q5 =
                        std::sqrt(r36[0][2] * r36[0][2] + r36[1][2] * r36[1][2]);
                    q5 = std::atan2(wrist == 0 ? sin_q5 : -sin_q5, r36[2][2]);
                    if(sin_q5 > 1e-12) {
                        const double sign = wrist == 0 ? 1.0 : -1.0;
                        q4 = std::atan2(sign * r36[1][2], sign * r36[0][2]);
                        q6 = std::atan2(sign * r36[2][1], -sign * r36[2][0]);
                    } else {
                        // Wrist singularity: q4+q6 is the only observable;
                        // keep the seed's q4 and put the rest into q6.
                        q4 = seed[3];
                        q6 = std::atan2(r36[1][0], r36[0][0]) - q4;
                        if(wrist == 1) {
                            continue; // both wrist branches coincide here
                        }
                    }

                    double candidate[JointCount] = {q1, q2, q3, q4, q5, q6};
                    double score = 0.0;
                    bool inside = true;
                    for(std::size_t j = 0; j < JointCount; ++j) {
                        candidate[j] = wrap_to_seed(candidate[j], seed[j]);
                        const double deviation = std::fabs(candidate[j] - seed[j]);
                        if(deviation > max_joint_step) {
                            inside = false;
                            break;
                        }
                        if(deviation > score) {
                            score = deviation;
                        }
                    }
                    if(inside && score < best_score) {
                        best_score = score;
                        for(std::size_t j = 0; j < JointCount; ++j) {
                            best[j] = candidate[j];
                        }
                        found = true;
                    }
                }
            }
        }

        if(!found) {
            return rt::ErrorCode::infeasible;
        }
        for(std::size_t j = 0; j < JointCount; ++j) {
            joints_out[j] = best[j];
        }
        return rt::ErrorCode::ok;
    }

    // Angular distance to the nearest singular configuration: wrist
    // (q5 = 0 or +-pi), elbow (q3 = 0 or +-pi), and shoulder (wrist center on
    // the base axis, measured as the planar reach angle).
    double singularity_margin(const double *q) const override
    {
        const double wrist = fold(q[4]);
        const double elbow = fold(q[2]);
        // Shoulder: reach angle of the wrist center off the base axis.
        const double s2 = std::sin(q[1]);
        const double s23 = std::sin(q[1] + q[2]);
        const double radius = a2_ * s2 + d4_ * s23;
        const double height = a2_ * std::cos(q[1]) + d4_ * std::cos(q[1] + q[2]);
        const double shoulder = std::fabs(std::atan2(radius, std::fabs(height) + d1_));
        double margin = wrist < elbow ? wrist : elbow;
        return shoulder < margin ? shoulder : margin;
    }

private:
    static constexpr double Pi = 3.14159265358979323846;

    static double fold(double angle)
    {
        double magnitude = std::fmod(std::fabs(angle), 2.0 * Pi);
        if(magnitude > Pi) {
            magnitude = 2.0 * Pi - magnitude;
        }
        const double to_pi = Pi - magnitude;
        return magnitude < to_pi ? magnitude : to_pi;
    }

    static double wrap_to_seed(double angle, double seed)
    {
        return angle + 2.0 * Pi * std::floor((seed - angle) / (2.0 * Pi) + 0.5);
    }

    // rotation = Rz(q1) * Ry(q2+q3) * Rz(q4) * Ry(q5) * Rz(q6)
    static void compose_rotation(const double *q, double rotation[3][3])
    {
        double arm[3][3];
        rotation_zy(q[0], q[1] + q[2], arm);
        double wrist[3][3];
        double zy[3][3];
        rotation_zy(q[3], q[4], zy);
        double z6[3][3];
        rotation_z(q[5], z6);
        multiply(zy, z6, wrist);
        multiply(arm, wrist, rotation);
    }

    // R36 = Ry(theta)^T * Rz(q1)^T * R
    static void arm_transpose_times(double q1, double theta, const double r[3][3],
                                    double out[3][3])
    {
        double arm[3][3];
        rotation_zy(q1, theta, arm);
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                out[i][j] =
                    arm[0][i] * r[0][j] + arm[1][i] * r[1][j] + arm[2][i] * r[2][j];
            }
        }
    }

    static void rotation_z(double angle, double out[3][3])
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        out[0][0] = c;
        out[0][1] = -s;
        out[0][2] = 0.0;
        out[1][0] = s;
        out[1][1] = c;
        out[1][2] = 0.0;
        out[2][0] = 0.0;
        out[2][1] = 0.0;
        out[2][2] = 1.0;
    }

    // Rz(a) * Ry(b)
    static void rotation_zy(double a, double b, double out[3][3])
    {
        const double ca = std::cos(a);
        const double sa = std::sin(a);
        const double cb = std::cos(b);
        const double sb = std::sin(b);
        out[0][0] = ca * cb;
        out[0][1] = -sa;
        out[0][2] = ca * sb;
        out[1][0] = sa * cb;
        out[1][1] = ca;
        out[1][2] = sa * sb;
        out[2][0] = -sb;
        out[2][1] = 0.0;
        out[2][2] = cb;
    }

    static void multiply(const double a[3][3], const double b[3][3], double out[3][3])
    {
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            }
        }
    }

    double d1_ = 0.0;
    double a2_ = 1.0;
    double d4_ = 1.0;
    double d6_ = 0.0;
};

} // namespace plcopen::core::kin
