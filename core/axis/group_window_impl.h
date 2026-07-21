#pragma once

// AxisGroup batch 2 implementation. Included by group.h after AxisGroup is complete.

namespace plcopen::core::axis
{
inline void AxisGroup::window_tangent(const WindowSegment &seg, bool at_exit,
                    std::array<double, MaxAxes> &out) const
{
    if(seg.kind == WindowKind::line || seg.kind == WindowKind::direct_line) {
        out = seg.dir;
        return;
    }
    const geom::Vec3 plane =
        geom::tangent(seg.arc_geom, at_exit ? seg.arc_geom.length : 0.0);
    out[0] = plane.x;
    out[1] = plane.y;
    double norm = plane.x * plane.x + plane.y * plane.y;
    for(std::size_t i = 2; i < axes_.size(); ++i) {
        const double slope = (seg.target[i] - seg.entry[i]) / seg.full_length;
        out[i] = slope;
        norm += slope * slope;
    }
    norm = std::sqrt(norm);
    if(norm > 0.0) {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            out[i] /= norm;
        }
    }
}

inline rt::Result<std::uint32_t> AxisGroup::submit_blend(GroupCommand command)
{
    // v1 declared boundary: blending extends the active window (or the
    // active plain linear command); a stopping window, plain queued
    // commands, or anything else is an explicit error.
    if(joint_window_.stopping_ || !queue_.empty()) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.tool_number != active_tool_ ||
       command.payload_number != active_payload_) {
        return degrade_blend(command);
    }
    if(!joint_window_.active_) {
        if(!active_ || active_kind_ != GroupPathKind::linear ||
           status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
    }
    if(joint_window_.active_ && joint_window_.segments_.size() >= joint_window_.depth_) {
        // Window capacity is a declared limit, never a silent drop.
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
    }

    // Predecessor tail geometry (window tail or the active linear command).
    std::array<double, MaxAxes> pred_target{};
    std::array<double, MaxAxes> pred_dir{};
    double pred_full = 0.0;
    double pred_trim_out_room = 0.0; // half-length truncation budget
    bool pred_is_line = true;
    otg::Limits1D pred_limits{};
    if(joint_window_.active_) {
        const WindowSegment &tail = joint_window_.segments_[joint_window_.segments_.size() - 1];
        pred_target = tail.target;
        window_tangent(tail, true, pred_dir);
        pred_full = tail.full_length;
        pred_limits = tail.limits;
        pred_is_line = tail.kind == WindowKind::line ||
                       tail.kind == WindowKind::direct_line;
    } else {
        double length1 = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double d = active_finish_[i] - active_start_[i];
            pred_dir[i] = d;
            pred_target[i] = active_finish_[i];
            length1 += d * d;
        }
        length1 = std::sqrt(length1);
        if(length1 <= 1e-12) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            pred_dir[i] /= length1;
        }
        pred_full = length1;
        const double scale1 = length1 / active_path_length_;
        pred_limits = otg::Limits1D{active_command_.velocity * scale1,
                                    active_command_.acceleration * scale1,
                                    active_command_.deceleration * scale1,
                                    active_command_.jerk * scale1};
    }
    pred_trim_out_room = pred_is_line ? pred_full * 0.5 : 0.0;

    // Successor geometry.
    double length2 = 0.0;
    double longest2 = 0.0;
    std::array<double, MaxAxes> u2{};
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        const double d = command.target.value[i] - pred_target[i];
        u2[i] = d;
        length2 += d * d;
        const double travel = std::fabs(d);
        if(travel > longest2) {
            longest2 = travel;
        }
    }
    length2 = std::sqrt(length2);
    if(length2 <= 1e-12) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
    }
    double alignment = 0.0;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        u2[i] /= length2;
        alignment += pred_dir[i] * u2[i];
    }
    const double scale2 = longest2 > 0.0 ? length2 / longest2 : 1.0;
    const otg::Limits1D limits2{command.velocity * scale2,
                                command.acceleration * scale2,
                                command.deceleration * scale2, command.jerk * scale2};

    if(alignment < -0.999) {
        // Reflex corner: degrade to a BUFFERED full-stop join, reported.
        return degrade_blend(command);
    }

    // Corner construction (geometry frozen at creation).
    WindowNode node{};
    double trim = 0.0;
    double corner_cap =
        pred_limits.max_velocity < limits2.max_velocity ? pred_limits.max_velocity
                                                        : limits2.max_velocity;
    if(alignment <= 0.999 && !pred_is_line) {
        // No tolerance-band curve exists between an arc and a line (v3
        // scope); a non-tangent junction degrades to a full-stop join.
        return degrade_blend(command);
    }
    if(alignment <= 0.999) {
        double turn = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double diff = u2[i] - pred_dir[i];
            turn += diff * diff;
        }
        turn = std::sqrt(turn);
        trim = command.transition_parameter * 96.0 / (23.0 * turn);
        if(trim > pred_trim_out_room) {
            trim = pred_trim_out_room;
        }
        if(trim > length2 * 0.5) {
            trim = length2 * 0.5;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double corner = pred_target[i];
            node.ctrl[0][i] = corner - pred_dir[i] * trim;
            node.ctrl[1][i] = corner - pred_dir[i] * (trim * 2.0 / 3.0);
            node.ctrl[2][i] = corner - pred_dir[i] * (trim / 3.0);
            node.ctrl[3][i] = corner + u2[i] * (trim / 3.0);
            node.ctrl[4][i] = corner + u2[i] * (trim * 2.0 / 3.0);
            node.ctrl[5][i] = corner + u2[i] * trim;
        }
        double accumulated = 0.0;
        std::array<double, MaxAxes> previous{};
        blend_point(node.ctrl, 0.0, previous);
        node.cumulative[0] = 0.0;
        for(std::size_t step = 1; step < BlendTableSize; ++step) {
            const double u =
                static_cast<double>(step) / static_cast<double>(BlendTableSize - 1);
            std::array<double, MaxAxes> point{};
            blend_point(node.ctrl, u, point);
            double chord = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double diff = point[i] - previous[i];
                chord += diff * diff;
            }
            accumulated += std::sqrt(chord);
            node.cumulative[step] = accumulated;
            previous = point;
        }
        node.curve_length = accumulated;
        if(!std::isfinite(node.curve_length) || node.curve_length <= 0.0) {
            return degrade_blend(command);
        }
        double max_curvature = 0.0;
        for(std::size_t step = 0; step <= 64; ++step) {
            const double u = static_cast<double>(step) / 64.0;
            const double curvature = blend_curvature(node.ctrl, u);
            if(curvature > max_curvature) {
                max_curvature = curvature;
            }
        }
        if(max_curvature > 0.0) {
            double junction = pred_limits.max_acceleration;
            if(pred_limits.max_deceleration < junction) {
                junction = pred_limits.max_deceleration;
            }
            if(limits2.max_acceleration < junction) {
                junction = limits2.max_acceleration;
            }
            if(limits2.max_deceleration < junction) {
                junction = limits2.max_deceleration;
            }
            const double geometric = std::sqrt(junction / max_curvature);
            if(geometric < corner_cap) {
                corner_cap = geometric;
            }
        }
        node.has_curve = true;
    }
    if(command.transition_velocity > 0.0) {
        const double transition_cap = command.transition_velocity * scale2;
        if(transition_cap < corner_cap) {
            corner_cap = transition_cap;
        }
    }
    node.corner_cap = corner_cap;

    // Baseline for the constructive cycle-time gate (captured pre-append).
    const std::int64_t old_remaining = joint_window_.active_ ? window_remaining_cycles() : -1;

    // Convert the active linear command into window segment zero.
    bool converted = false;
    if(!joint_window_.active_) {
        if(!convert_active_linear_to_window()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        converted = true;
    }

    // Append: tail gains the corner, the new segment enters the window.
    WindowSegment &tail = joint_window_.segments_[joint_window_.segments_.size() - 1];
    const double saved_trim_out = tail.trim_out;
    const WindowNode saved_node = tail.node;
    tail.trim_out = trim;
    tail.node = node;

    WindowSegment fresh{};
    fresh.kind = command.direct_semantics ? WindowKind::direct_line
                                          : WindowKind::line;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        fresh.entry[i] = node.has_curve ? node.ctrl[5][i] : pred_target[i];
        fresh.dir[i] = u2[i];
        fresh.target[i] = command.target.value[i];
    }
    fresh.full_length = length2;
    fresh.trim_in = trim;
    fresh.limits = limits2;
    fresh.command_id = command.command_id;
    if(joint_window_.segments_.size() >= joint_window_.depth_ ||
       joint_window_.segments_.push_back(fresh) != rt::ErrorCode::ok) {
        tail.trim_out = saved_trim_out;
        tail.node = saved_node;
        if(converted) {
            joint_window_.segments_.clear();
        }
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
    }

    bool late = false;
    if(!window_rebuild(late)) {
        joint_window_.segments_.pop_back();
        WindowSegment &restore = joint_window_.segments_[joint_window_.segments_.size() - 1];
        restore.trim_out = saved_trim_out;
        restore.node = saved_node;
        if(converted) {
            joint_window_.segments_.clear();
        } else if(!window_rebuild(late)) {
            // Restoring the previous window must succeed; if the live
            // state has drifted past a boundary, stop safely.
            window_reset();
            clear_axes_synchronized();
            status_ = GroupStatus::standby;
            abort_motion();
        }
        return degrade_blend(command);
    }

    // Constructive cycle-time gate (KB-031 carried over): the extended
    // window must beat "previous window then a standalone full-stop move".
    if(old_remaining >= 0) {
        const rt::Result<otg::Profile1D> standalone = otg::plan_time_optimal(
            {0.0, 0.0, 0.0}, {length2, 0.0, 0.0}, limits2);
        if(standalone &&
           window_remaining_cycles() >=
               old_remaining + standalone.value().duration_cycles()) {
            joint_window_.segments_.pop_back();
            WindowSegment &restore = joint_window_.segments_[joint_window_.segments_.size() - 1];
            restore.trim_out = saved_trim_out;
            restore.node = saved_node;
            if(!window_rebuild(late)) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                abort_motion();
            }
            return degrade_blend(command);
        }
    }

    if(converted) {
        active_ = false;
        joint_window_.active_ = true;
    }
    status_ = GroupStatus::moving;
    return rt::Result<std::uint32_t>::success(command.command_id);
}

// Seed the window from the active linear command: segment zero carries
// the euclidean geometry and the live state (captured for the rebuild).
inline bool AxisGroup::convert_active_linear_to_window()
{
    double length1 = 0.0;
    std::array<double, MaxAxes> direction{};
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        const double d = active_finish_[i] - active_start_[i];
        direction[i] = d;
        length1 += d * d;
    }
    length1 = std::sqrt(length1);
    if(length1 <= 1e-12 || active_path_length_ <= 0.0) {
        return false;
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        direction[i] /= length1;
    }
    const double scale1 = length1 / active_path_length_;

    WindowSegment seg0{};
    seg0.kind = active_command_.direct_semantics ? WindowKind::direct_line
                                                 : WindowKind::line;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        seg0.entry[i] = active_start_[i];
        seg0.dir[i] = direction[i];
        seg0.target[i] = active_finish_[i];
    }
    seg0.full_length = length1;
    seg0.limits = otg::Limits1D{active_command_.velocity * scale1,
                                active_command_.acceleration * scale1,
                                active_command_.deceleration * scale1,
                                active_command_.jerk * scale1};
    seg0.command_id = active_command_.command_id;
    const otg::State1D raw =
        otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
    seg0.entry_velocity = raw.velocity * scale1; // updated by rebuild
    seg0.profile = active_profile_;              // replaced by rebuild
    joint_window_.segments_.clear();
    joint_window_.segments_.push_back(seg0);
    joint_window_.seed_state_ = otg::State1D{raw.position * scale1, raw.velocity * scale1,
                                      raw.acceleration * scale1};
    joint_window_.index_ = 0;
    joint_window_.in_curve_ = false;
    joint_window_.tick_ = 0;
    return true;
}

// A5 v2 (KB-033): append a KB-030 BORDER arc to the look-ahead window.
// The junction must be tangent-continuous (no tolerance-band curve exists
// between lines and arcs until v3); anything else degrades to a BUFFERED
// full-stop join, reported. The arc segment's velocity limit is clamped
// to the centripetal bound sqrt(a*R) for the whole segment.
inline rt::Result<std::uint32_t> AxisGroup::submit_blend_arc(GroupCommand command,
                                           const std::array<double, MaxAxes> &start_point)
{
    if(joint_window_.stopping_ || !queue_.empty()) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.tool_number != active_tool_ ||
       command.payload_number != active_payload_) {
        return degrade_blend(command);
    }
    if(!joint_window_.active_) {
        if(!active_ || active_kind_ != GroupPathKind::linear ||
           status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
            // Converting an active circular command is a declared v2
            // boundary: only linear actives seed a window.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
    }
    if(joint_window_.active_ && joint_window_.segments_.size() >= joint_window_.depth_) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
    }

    // Arc segment descriptor. KB-030 dynamics are already stated in the
    // plane arc-length domain, so no metric conversion applies; the
    // centripetal bound clamps the whole segment.
    WindowSegment fresh{};
    fresh.kind = WindowKind::arc;
    fresh.arc_geom = command.arc;
    fresh.full_length = command.arc.length;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        fresh.entry[i] = start_point[i];
        fresh.target[i] = command.target.value[i];
    }
    fresh.entry[0] = command.arc.start.x;
    fresh.entry[1] = command.arc.start.y;
    fresh.limits = otg::Limits1D{command.velocity, command.acceleration,
                                 command.deceleration, command.jerk};
    double junction = fresh.limits.max_acceleration;
    if(fresh.limits.max_deceleration < junction) {
        junction = fresh.limits.max_deceleration;
    }
    const double centripetal = std::sqrt(junction * command.arc.radius);
    if(centripetal < fresh.limits.max_velocity) {
        fresh.limits.max_velocity = centripetal;
    }
    fresh.command_id = command.command_id;

    // Predecessor exit tangent vs the arc entry tangent (N-dimensional).
    std::array<double, MaxAxes> pred_tangent{};
    otg::Limits1D pred_limits{};
    if(joint_window_.active_) {
        const WindowSegment &tail = joint_window_.segments_[joint_window_.segments_.size() - 1];
        window_tangent(tail, true, pred_tangent);
        pred_limits = tail.limits;
    } else {
        double length1 = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double d = active_finish_[i] - active_start_[i];
            pred_tangent[i] = d;
            length1 += d * d;
        }
        length1 = std::sqrt(length1);
        if(length1 <= 1e-12) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            pred_tangent[i] /= length1;
        }
        const double scale1 = length1 / active_path_length_;
        pred_limits = otg::Limits1D{active_command_.velocity * scale1,
                                    active_command_.acceleration * scale1,
                                    active_command_.deceleration * scale1,
                                    active_command_.jerk * scale1};
    }
    std::array<double, MaxAxes> arc_tangent{};
    window_tangent(fresh, false, arc_tangent);
    double alignment = 0.0;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        alignment += pred_tangent[i] * arc_tangent[i];
    }
    if(alignment <= 0.999) {
        // Non-tangent junction: full-stop join, reported (approved v2).
        return degrade_blend(command);
    }

    // Pass-through node (no curve, no trims).
    WindowNode node{};
    node.corner_cap = pred_limits.max_velocity < fresh.limits.max_velocity
                          ? pred_limits.max_velocity
                          : fresh.limits.max_velocity;
    if(command.transition_velocity > 0.0 &&
       command.transition_velocity < node.corner_cap) {
        node.corner_cap = command.transition_velocity;
    }

    const std::int64_t old_remaining = joint_window_.active_ ? window_remaining_cycles() : -1;

    bool converted = false;
    if(!joint_window_.active_) {
        if(!convert_active_linear_to_window()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        converted = true;
    }

    WindowSegment &tail = joint_window_.segments_[joint_window_.segments_.size() - 1];
    const WindowNode saved_node = tail.node;
    tail.node = node;
    if(joint_window_.segments_.size() >= joint_window_.depth_ ||
       joint_window_.segments_.push_back(fresh) != rt::ErrorCode::ok) {
        tail.node = saved_node;
        if(converted) {
            joint_window_.segments_.clear();
        }
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
    }

    bool late = false;
    if(!window_rebuild(late)) {
        joint_window_.segments_.pop_back();
        WindowSegment &restore = joint_window_.segments_[joint_window_.segments_.size() - 1];
        restore.node = saved_node;
        if(converted) {
            joint_window_.segments_.clear();
        } else if(!window_rebuild(late)) {
            window_reset();
            clear_axes_synchronized();
            status_ = GroupStatus::standby;
            abort_motion();
        }
        return degrade_blend(command);
    }

    if(old_remaining >= 0) {
        const rt::Result<otg::Profile1D> standalone = otg::plan_time_optimal(
            {0.0, 0.0, 0.0}, {fresh.full_length, 0.0, 0.0}, fresh.limits);
        if(standalone &&
           window_remaining_cycles() >=
               old_remaining + standalone.value().duration_cycles()) {
            joint_window_.segments_.pop_back();
            WindowSegment &restore = joint_window_.segments_[joint_window_.segments_.size() - 1];
            restore.node = saved_node;
            if(!window_rebuild(late)) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                abort_motion();
            }
            return degrade_blend(command);
        }
    }

    if(converted) {
        active_ = false;
        joint_window_.active_ = true;
    }
    status_ = GroupStatus::moving;
    return rt::Result<std::uint32_t>::success(command.command_id);
}

// Trapezoid-level bidirectional scan + per-segment profile planning over
// the not-yet-started part of the window. Returns false when any segment
// profile is infeasible (caller degrades).
inline bool AxisGroup::window_rebuild(bool &late)
{
    late = false;
    const std::size_t count = joint_window_.segments_.size();
    if(count == 0 || joint_window_.index_ >= count) {
        return false;
    }

    // Live state along the current line piece (euclidean, local coords).
    double s_live = 0.0;
    double v_live = 0.0;
    double a_live = 0.0;
    std::size_t first = joint_window_.index_;
    if(joint_window_.active_) {
        if(joint_window_.in_curve_) {
            // The current curve is committed; rebuild from the next line.
            const WindowNode &cur = joint_window_.segments_[joint_window_.index_].node;
            double s_curve = cur.curve_velocity * static_cast<double>(joint_window_.tick_);
            if(s_curve > cur.curve_length) {
                s_curve = cur.curve_length;
            }
            (void)s_curve;
            first = joint_window_.index_ + 1;
            if(first >= count) {
                return false;
            }
            s_live = 0.0;
            v_live = cur.curve_velocity;
            a_live = 0.0;
        } else {
            const otg::State1D raw = otg::sample(
                joint_window_.segments_[joint_window_.index_].profile, rt::CycleTick::from_cycles(joint_window_.tick_));
            s_live = raw.position;
            v_live = raw.velocity;
            a_live = raw.acceleration;
        }
    } else {
        // Fresh conversion: seed state captured in submit_blend.
        s_live = joint_window_.seed_state_.position;
        v_live = joint_window_.seed_state_.velocity;
        a_live = joint_window_.seed_state_.acceleration;
    }

    // Late submission: already inside (or past) the tail transition region.
    const WindowSegment &first_seg = joint_window_.segments_[first];
    if(first == count - 2 || count == 2) {
        // The newly trimmed segment is the live one: check the room.
    }
    if(first < count && joint_window_.segments_[first].line_length() <= 0.0 && first + 1 < count) {
        // Fully consumed line between two corners is allowed only when
        // both node velocities agree; v1 degrades instead.
        return false;
    }
    if(!joint_window_.in_curve_ && s_live >= joint_window_.segments_[first].line_length()) {
        late = true;
        return false;
    }
    (void)first_seg;

    // Forward pass (accelerating limit), then backward pass (braking).
    std::array<double, WindowCapacity> node_v{};
    double forward = v_live;
    for(std::size_t i = first; i < count; ++i) {
        const double length = i == first && !joint_window_.in_curve_
                                  ? joint_window_.segments_[i].line_length() - s_live
                                  : joint_window_.segments_[i].line_length();
        const double usable = length > 0.0 ? length : 0.0;
        // Approved look-ahead v2: exact jerk-limited reachability
        // replaces the trapezoid estimate (declared change, KB-039).
        double reachable = plan::jerk_reachable_speed(
            forward, usable, joint_window_.segments_[i].limits.max_acceleration,
            joint_window_.segments_[i].limits.max_jerk);
        if(i + 1 < count) {
            const double cap = joint_window_.segments_[i].node.corner_cap;
            if(reachable > cap) {
                reachable = cap;
            }
            node_v[i] = reachable;
            forward = reachable;
        } else {
            node_v[i] = 0.0; // terminal rest
        }
    }
    double backward = 0.0;
    for(std::size_t r = count; r > first; --r) {
        const std::size_t i = r - 1;
        const double length = i == first && !joint_window_.in_curve_
                                  ? joint_window_.segments_[i].line_length() - s_live
                                  : joint_window_.segments_[i].line_length();
        const double usable = length > 0.0 ? length : 0.0;
        if(i + 1 < count && backward < node_v[i]) {
            node_v[i] = backward;
        }
        backward = plan::jerk_reachable_speed(node_v[i], usable,
                                              joint_window_.segments_[i].limits.max_deceleration,
                                              joint_window_.segments_[i].limits.max_jerk);
        if(i + 1 < count) {
            backward = backward; // entry allowance of segment i
        }
    }

    // Quantize curve velocities and plan the per-segment profiles.
    double entry_velocity = v_live;
    double entry_acceleration = a_live;
    double entry_position = joint_window_.in_curve_ ? 0.0 : s_live;
    for(std::size_t i = first; i < count; ++i) {
        WindowSegment &seg = joint_window_.segments_[i];
        double exit_velocity = 0.0;
        if(i + 1 < count) {
            if(seg.node.has_curve) {
                double v = node_v[i];
                if(v <= 1e-12) {
                    return false; // corner requires rest: degrade
                }
                std::int64_t cycles = static_cast<std::int64_t>(
                    std::ceil(seg.node.curve_length / v));
                if(cycles < 1) {
                    cycles = 1;
                }
                seg.node.curve_velocity =
                    seg.node.curve_length / static_cast<double>(cycles);
                seg.node.curve_cycles = cycles;
                exit_velocity = seg.node.curve_velocity;
            } else {
                exit_velocity = node_v[i];
            }
        }
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            {entry_position, entry_velocity, entry_acceleration},
            {seg.line_length(), exit_velocity, 0.0}, seg.limits);
        if(!profile) {
            return false;
        }
        seg.entry_velocity = entry_velocity;
        seg.exit_velocity = exit_velocity;
        seg.profile = profile.value();
        entry_velocity = exit_velocity;
        entry_acceleration = 0.0;
        entry_position = 0.0;
    }
    if(!joint_window_.in_curve_) {
        // The live line profile was replanned from the live state; its
        // tick restarts. A committed curve keeps its own progress.
        joint_window_.tick_ = 0;
    }
    return true;
}

inline std::int64_t AxisGroup::window_remaining_cycles() const
{
    std::int64_t total = 0;
    for(std::size_t i = joint_window_.index_; i < joint_window_.segments_.size(); ++i) {
        if(!(i == joint_window_.index_ && joint_window_.in_curve_)) {
            total += joint_window_.segments_[i].profile.duration_cycles();
        }
        if(i + 1 < joint_window_.segments_.size() && joint_window_.segments_[i].node.has_curve) {
            total += joint_window_.segments_[i].node.curve_cycles;
        }
    }
    return total;
}

inline void AxisGroup::window_cycle()
{
    if(joint_window_.override_paused_ && !joint_window_.stopping_) {
        return;
    }
    ++joint_window_.tick_;
    if(joint_window_.stopping_) {
        const otg::State1D st = otg::sample(joint_window_.stop_profile_,
                                            rt::CycleTick::from_cycles(joint_window_.tick_));
        sample_window_arclength(joint_window_.stop_origin_ + st.position);
        if(joint_window_.tick_ >= joint_window_.stop_profile_.duration_cycles()) {
            if(joint_window_.override_paused_) {
                interrupting_ = false;
                joint_window_.stopping_ = false;
                status_ = GroupStatus::moving;
            } else if(interrupting_) {
                interrupting_ = false;
                joint_window_.stopping_ = false;
                status_ = GroupStatus::interrupted;
            } else {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                start_next_queued();
            }
        }
        return;
    }

    WindowSegment &seg = joint_window_.segments_[joint_window_.index_];
    if(joint_window_.in_curve_) {
        double s = seg.node.curve_velocity * static_cast<double>(joint_window_.tick_);
        if(s > seg.node.curve_length) {
            s = seg.node.curve_length;
        }
        sample_window_curve(seg.node, s);
        if(joint_window_.tick_ >= seg.node.curve_cycles) {
            if(seg.kind == WindowKind::direct_line) complete_direct(seg.command_id);
            ++joint_window_.index_;
            joint_window_.in_curve_ = false;
            joint_window_.tick_ = 0;
        }
        return;
    }

    const otg::State1D st =
        otg::sample(seg.profile, rt::CycleTick::from_cycles(joint_window_.tick_));
    // No clamping: a terminal profile may legally overshoot the target by
    // a hair inside its envelope and come back; clamping would turn that
    // into a hard stop (acceleration step). The final sample lands on the
    // exact segment end by the profile's endpoint contract.
    const double s = st.position;
    sample_window_segment(seg, s);
    if(joint_window_.tick_ >= seg.profile.duration_cycles()) {
        if(joint_window_.index_ + 1 < joint_window_.segments_.size()) {
            if(seg.node.has_curve) {
                joint_window_.in_curve_ = true;
                joint_window_.tick_ = 0;
            } else {
                if(seg.kind == WindowKind::direct_line) complete_direct(seg.command_id);
                ++joint_window_.index_;
                joint_window_.tick_ = 0;
            }
        } else {
            if(seg.kind == WindowKind::direct_line) complete_direct(seg.command_id);
            window_reset();
            clear_axes_synchronized();
            status_ = GroupStatus::standby;
            start_next_queued();
        }
    }
}

inline void AxisGroup::sample_window_segment(const WindowSegment &seg, double arclength)
{
    if(seg.kind == WindowKind::arc) {
        double s = arclength;
        if(s < 0.0) {
            s = 0.0;
        }
        if(s > seg.full_length) {
            s = seg.full_length;
        }
        const geom::Vec3 point = geom::sample(seg.arc_geom, s);
        axes_[0]->set_synchronized_position(point.x);
        axes_[1]->set_synchronized_position(point.y);
        const double ratio = seg.full_length > 0.0 ? s / seg.full_length : 1.0;
        for(std::size_t i = 2; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(
                seg.entry[i] + (seg.target[i] - seg.entry[i]) * ratio);
        }
        return;
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        axes_[i]->set_synchronized_position(seg.entry[i] + seg.dir[i] * arclength);
    }
}

inline void AxisGroup::sample_window_curve(const WindowNode &node, double arclength)
{
    double target = arclength;
    if(target < 0.0) {
        target = 0.0;
    }
    if(target > node.curve_length) {
        target = node.curve_length;
    }
    std::size_t low = 0;
    for(std::size_t i = 1; i < BlendTableSize; ++i) {
        if(node.cumulative[i] >= target) {
            low = i - 1;
            break;
        }
        low = i - 1;
    }
    const double segment = node.cumulative[low + 1] - node.cumulative[low];
    const double fraction =
        segment > 0.0 ? (target - node.cumulative[low]) / segment : 0.0;
    const double u = (static_cast<double>(low) + fraction) /
                     static_cast<double>(BlendTableSize - 1);
    std::array<double, MaxAxes> point{};
    blend_point(node.ctrl, u, point);
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        axes_[i]->set_synchronized_position(point[i]);
    }
}

// Composite arc length measured from the live piece at stop time: walks
// the remaining pieces (bounded by the window capacity, simple compares).
inline void AxisGroup::sample_window_arclength(double arclength)
{
    double remaining = arclength;
    bool in_curve = joint_window_.in_curve_;
    std::size_t index = joint_window_.index_;
    while(index < joint_window_.segments_.size()) {
        const WindowSegment &seg = joint_window_.segments_[index];
        if(!in_curve) {
            const double length = seg.line_length();
            if(remaining <= length || index + 1 >= joint_window_.segments_.size()) {
                const double s =
                    remaining < 0.0 ? 0.0 : (remaining > length ? length : remaining);
                sample_window_segment(seg, s);
                return;
            }
            remaining -= length;
            if(seg.node.has_curve) {
                in_curve = true;
            } else {
                ++index;
            }
        } else {
            if(remaining <= seg.node.curve_length) {
                sample_window_curve(seg.node, remaining);
                return;
            }
            remaining -= seg.node.curve_length;
            in_curve = false;
            ++index;
        }
    }
}

inline void AxisGroup::window_live_state(double &s_live, double &v_live, double &a_live) const
{
    if(joint_window_.in_curve_) {
        const WindowNode &node = joint_window_.segments_[joint_window_.index_].node;
        double s = node.curve_velocity * static_cast<double>(joint_window_.tick_);
        if(s > node.curve_length) {
            s = node.curve_length;
        }
        s_live = s;
        v_live = node.curve_velocity;
        a_live = 0.0;
        return;
    }
    const otg::State1D raw = otg::sample(joint_window_.segments_[joint_window_.index_].profile,
                                         rt::CycleTick::from_cycles(joint_window_.tick_));
    const double length = joint_window_.segments_[joint_window_.index_].line_length();
    s_live = raw.position < 0.0 ? 0.0 : (raw.position > length ? length : raw.position);
    v_live = raw.velocity;
    a_live = raw.acceleration;
}

inline void AxisGroup::window_reset()
{
    joint_window_.active_ = false;
    joint_window_.stopping_ = false;
    joint_window_.override_paused_ = false;
    joint_window_.in_curve_ = false;
    joint_window_.index_ = 0;
    joint_window_.tick_ = 0;
    joint_window_.segments_.clear();
}


inline rt::Result<std::uint32_t> AxisGroup::degrade_blend(GroupCommand command)
{
    last_blend_degraded_id_ = command.command_id;
    command.buffer_mode = BufferMode::buffered;
    command.transition_mode = TransitionMode::none;
    command.transition_velocity = 0.0;
    command.transition_parameter = 0.0;
    const rt::ErrorCode queued = queue_.push_back(command);
    if(queued != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(queued);
    }
    return rt::Result<std::uint32_t>::success(command.command_id);
}

inline void AxisGroup::blend_point(const std::array<std::array<double, MaxAxes>, 6> &control,
                 double u,
                 std::array<double, MaxAxes> &out) const
{
    const double v = 1.0 - u;
    const double v2 = v * v;
    const double u2 = u * u;
    const double w0 = v2 * v2 * v;
    const double w1 = 5.0 * v2 * v2 * u;
    const double w2 = 10.0 * v2 * v * u2;
    const double w3 = 10.0 * v2 * u2 * u;
    const double w4 = 5.0 * v * u2 * u2;
    const double w5 = u2 * u2 * u;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        out[i] = control[0][i] * w0 + control[1][i] * w1 + control[2][i] * w2 +
                 control[3][i] * w3 + control[4][i] * w4 + control[5][i] * w5;
    }
}

inline double AxisGroup::blend_curvature(const std::array<std::array<double, MaxAxes>, 6> &control,
                       double u) const
{
    const double v = 1.0 - u;
    const double v2 = v * v;
    const double u2 = u * u;
    const double d1w0 = 5.0 * v2 * v2;
    const double d1w1 = 20.0 * v2 * v * u;
    const double d1w2 = 30.0 * v2 * u2;
    const double d1w3 = 20.0 * v * u2 * u;
    const double d1w4 = 5.0 * u2 * u2;
    const double d2w0 = 20.0 * v * v * v;
    const double d2w1 = 60.0 * v * v * u;
    const double d2w2 = 60.0 * v * u * u;
    const double d2w3 = 20.0 * u * u * u;
    double norm1 = 0.0;
    double norm2 = 0.0;
    double dot12 = 0.0;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        const double e0 = control[1][i] - control[0][i];
        const double e1 = control[2][i] - control[1][i];
        const double e2 = control[3][i] - control[2][i];
        const double e3 = control[4][i] - control[3][i];
        const double e4 = control[5][i] - control[4][i];
        const double first = e0 * d1w0 + e1 * d1w1 + e2 * d1w2 + e3 * d1w3 + e4 * d1w4;
        const double f0 = e1 - e0;
        const double f1 = e2 - e1;
        const double f2 = e3 - e2;
        const double f3 = e4 - e3;
        const double second = f0 * d2w0 + f1 * d2w1 + f2 * d2w2 + f3 * d2w3;
        norm1 += first * first;
        norm2 += second * second;
        dot12 += first * second;
    }
    if(norm1 <= 1e-24) {
        return 0.0;
    }
    const double area = norm1 * norm2 - dot12 * dot12;
    if(area <= 0.0) {
        return 0.0;
    }
    return std::sqrt(area) / (norm1 * std::sqrt(norm1));
}

} // namespace plcopen::core::axis
