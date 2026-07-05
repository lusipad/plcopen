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

// Full rigid transform (orientation batch, approved matrix decision #4/#5):
// a rotation matrix plus a translation. Built from RPY angles only
// (R = Rz(yaw) * Ry(pitch) * Rx(roll)), so rigidity holds by construction —
// no arbitrary-matrix orthogonality validation. RPY is the input direction
// exclusively; the matrix-to-RPY direction (and its gimbal ambiguity) is
// deliberately absent.
struct RigidTransform
{
    double rotation[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    Vec3 translation{};
};

inline RigidTransform make_rpy_transform(double x,
                                         double y,
                                         double z,
                                         double roll,
                                         double pitch,
                                         double yaw)
{
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);

    RigidTransform transform{};
    transform.rotation[0][0] = cy * cp;
    transform.rotation[0][1] = cy * sp * sr - sy * cr;
    transform.rotation[0][2] = cy * sp * cr + sy * sr;
    transform.rotation[1][0] = sy * cp;
    transform.rotation[1][1] = sy * sp * sr + cy * cr;
    transform.rotation[1][2] = sy * sp * cr - cy * sr;
    transform.rotation[2][0] = -sp;
    transform.rotation[2][1] = cp * sr;
    transform.rotation[2][2] = cp * cr;
    transform.translation = Vec3{x, y, z};
    return transform;
}

inline Vec3 transform_rotate(const RigidTransform &transform, Vec3 v)
{
    return Vec3{transform.rotation[0][0] * v.x + transform.rotation[0][1] * v.y +
                    transform.rotation[0][2] * v.z,
                transform.rotation[1][0] * v.x + transform.rotation[1][1] * v.y +
                    transform.rotation[1][2] * v.z,
                transform.rotation[2][0] * v.x + transform.rotation[2][1] * v.y +
                    transform.rotation[2][2] * v.z};
}

inline Vec3 transform_point(const RigidTransform &transform, Vec3 p)
{
    return transform_rotate(transform, p) + transform.translation;
}

// a then b in a's frame: (a ∘ b)(p) = a(b(p)).
inline RigidTransform compose(const RigidTransform &a, const RigidTransform &b)
{
    RigidTransform out{};
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            out.rotation[i][j] = a.rotation[i][0] * b.rotation[0][j] +
                                 a.rotation[i][1] * b.rotation[1][j] +
                                 a.rotation[i][2] * b.rotation[2][j];
        }
    }
    out.translation = transform_point(a, b.translation);
    return out;
}

// Cartesian-interpolation batch (approved matrix decision #4): rotation
// primitives for the per-cycle geodesic. relative_axis_angle extracts the
// axis-angle of b relative to a (a-transpose times b); angles at or beyond
// pi are rejected by the caller (the geodesic is not unique there), and
// close to pi the axis comes from the symmetric form with skew-part signs
// for stability.
inline void rotation_multiply(const double a[3][3],
                              const double b[3][3],
                              double out[3][3])
{
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
}

inline void relative_axis_angle(const double a[3][3],
                                const double b[3][3],
                                double axis[3],
                                double &angle)
{
    double rel[3][3];
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            rel[i][j] = a[0][i] * b[0][j] + a[1][i] * b[1][j] + a[2][i] * b[2][j];
        }
    }
    const double trace = rel[0][0] + rel[1][1] + rel[2][2];
    double cosine = (trace - 1.0) * 0.5;
    cosine = cosine > 1.0 ? 1.0 : (cosine < -1.0 ? -1.0 : cosine);
    angle = std::acos(cosine);
    axis[0] = 0.0;
    axis[1] = 0.0;
    axis[2] = 1.0;
    if(angle < 1e-12) {
        angle = 0.0;
        return;
    }
    const double skew[3] = {rel[2][1] - rel[1][2], rel[0][2] - rel[2][0],
                            rel[1][0] - rel[0][1]};
    if(angle < 3.0) {
        const double scale = 1.0 / (2.0 * std::sin(angle));
        axis[0] = skew[0] * scale;
        axis[1] = skew[1] * scale;
        axis[2] = skew[2] * scale;
    } else {
        for(int i = 0; i < 3; ++i) {
            double squared = (rel[i][i] - cosine) / (1.0 - cosine);
            squared = squared < 0.0 ? 0.0 : squared;
            axis[i] = std::sqrt(squared);
            if(skew[i] < 0.0) {
                axis[i] = -axis[i];
            }
        }
    }
    const double norm =
        std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if(norm > 0.0) {
        axis[0] /= norm;
        axis[1] /= norm;
        axis[2] /= norm;
    }
}

inline void rodrigues(const double axis[3], double angle, double out[3][3])
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis[0];
    const double y = axis[1];
    const double z = axis[2];
    out[0][0] = t * x * x + c;
    out[0][1] = t * x * y - s * z;
    out[0][2] = t * x * z + s * y;
    out[1][0] = t * x * y + s * z;
    out[1][1] = t * y * y + c;
    out[1][2] = t * y * z - s * x;
    out[2][0] = t * x * z - s * y;
    out[2][1] = t * y * z + s * x;
    out[2][2] = t * z * z + c;
}

// Readback batch (approved matrix decision #4): the declared inverse of
// make_rpy_transform. pitch = atan2(-r20, hypot(r00, r10)) in [-pi/2, pi/2],
// roll/yaw from the cosine-pitch element pairs, all in (-pi, pi]. Inside the
// gimbal band (hypot(r00, r10) < 1e-9) the rotational freedom folds into yaw
// with roll = 0 by convention and the return value flags it; the rebuilt
// matrix stays exact either way. This is the only place the matrix-to-RPY
// direction exists — command inputs stay RPY-only (orientation matrix
// decision #10).
inline bool extract_rpy(const double rotation[3][3],
                        double &roll,
                        double &pitch,
                        double &yaw)
{
    const double cos_pitch = std::hypot(rotation[0][0], rotation[1][0]);
    pitch = std::atan2(-rotation[2][0], cos_pitch);
    if(cos_pitch < 1e-9) {
        roll = 0.0;
        yaw = std::atan2(-rotation[0][1], rotation[1][1]);
        return true;
    }
    roll = std::atan2(rotation[2][1], rotation[2][2]);
    yaw = std::atan2(rotation[1][0], rotation[0][0]);
    return false;
}

inline RigidTransform invert(const RigidTransform &transform)
{
    RigidTransform out{};
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            out.rotation[i][j] = transform.rotation[j][i];
        }
    }
    out.translation = Vec3{0.0, 0.0, 0.0} - transform_rotate(out, transform.translation);
    return out;
}

} // namespace plcopen::core::geom
