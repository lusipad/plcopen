#pragma once

// Orientation batch (approved matrix: doc/compliance/orientation-semantics.md,
// decision #2): the 6-DOF pose plugin contract, parallel to the v1
// translational `Kinematics` ABI — the two consume different command
// dimensions and are configured mutually exclusively on a group. Same
// contract discipline as KB-037/KB-041: analytic solutions, seed-branch
// selection with a no-branch-flip gate, singularity margins, no allocation,
// no exceptions, bounded time.

#include <cstddef>

#include "rt/error.h"

namespace plcopen::core::kin
{

struct Pose6
{
    double position[3] = {0.0, 0.0, 0.0};
    // Row-major rotation matrix (must be orthonormal; callers own that).
    double rotation[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
};

class PoseKinematics
{
public:
    virtual ~PoseKinematics() = default;

    virtual std::size_t joint_count() const = 0;

    virtual void forward(const double *joints, Pose6 &pose) const = 0;

    // Seed-branch semantics with the max_joint_step no-branch-flip gate
    // (KB-041 contract carried over): no same-turn candidate inside the step
    // bound reports `infeasible`, never an implicit branch flip.
    virtual rt::ErrorCode inverse(const Pose6 &pose,
                                  const double *seed,
                                  double max_joint_step,
                                  double *joints_out) const = 0;

    virtual double singularity_margin(const double *joints) const = 0;
};

} // namespace plcopen::core::kin
