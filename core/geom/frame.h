#pragma once

// Rigid coordinate frame for the B1 v1 coordinate stack (approved matrix:
// doc/compliance/part4-coordinate-semantics.md, decisions #4/#10): a
// translation plus one rotation about Z ("2.5D" workpiece placement). The
// rotation is stored as a cached cosine/sine pair, so applying the frame is
// a handful of multiplies; rigidity holds by construction — there is no
// arbitrary-matrix input to validate for orthogonality.
//
// RT constraints: plain value math, no allocation, no exceptions. Frame
// application happens at submit time (planning domain) only.

#include <cmath>

#include "geom/geometry.h"
#include "rt/error.h"

namespace plcopen::core::geom
{

struct RigidFrame
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double cos_rz = 1.0;
    double sin_rz = 0.0;
};

inline rt::Result<RigidFrame> make_frame(double x, double y, double z, double rot_z)
{
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(rot_z)) {
        return rt::Result<RigidFrame>::failure(rt::ErrorCode::invalid_argument);
    }
    RigidFrame frame{};
    frame.x = x;
    frame.y = y;
    frame.z = z;
    frame.cos_rz = std::cos(rot_z);
    frame.sin_rz = std::sin(rot_z);
    return rt::Result<RigidFrame>::success(frame);
}

// Rotation only: for direction vectors (relative distances).
inline Vec3 frame_rotate(const RigidFrame &frame, Vec3 direction)
{
    return Vec3{frame.cos_rz * direction.x - frame.sin_rz * direction.y,
                frame.sin_rz * direction.x + frame.cos_rz * direction.y,
                direction.z};
}

// Frame-local point to base coordinates: rotate about Z, then translate.
inline Vec3 frame_to_base(const RigidFrame &frame, Vec3 local)
{
    const Vec3 rotated = frame_rotate(frame, local);
    return Vec3{rotated.x + frame.x, rotated.y + frame.y, rotated.z + frame.z};
}

} // namespace plcopen::core::geom
