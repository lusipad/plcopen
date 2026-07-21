#pragma once

// AxisGroup batch 3 Cartesian path/window implementation.
// Included by group.h after AxisGroup is complete.

namespace plcopen::core::axis
{

inline rt::Result<std::uint32_t> AxisGroup::submit_cartesian_window(GroupCommand command)
{
    if(command.command_id == 0) {
        command.command_id = next_command_id_++;
    }
    if(command.coord_system != CoordSystem::mcs &&
       command.coord_system != CoordSystem::pcs) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.relative || !queue_.empty() || joint_window_.active_) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.tool_number != active_tool_ ||
       command.payload_number != active_payload_) {
        last_blend_degraded_id_ = command.command_id;
        command.buffer_mode = BufferMode::buffered;
        command.transition_mode = TransitionMode::none;
        command.transition_velocity = 0.0;
        command.transition_parameter = 0.0;
        const rt::ErrorCode prepared = prepare_cartesian_linear(command);
        if(prepared != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(prepared);
        }
        const rt::ErrorCode queued = queue_.push_back(command);
        return queued == rt::ErrorCode::ok
                   ? rt::Result<std::uint32_t>::success(command.command_id)
                   : rt::Result<std::uint32_t>::failure(queued);
    }
    const bool extend = cartesian_.window_active_;
    if(!extend) {
        // Conversion seed: an active, non-chain, non-arc Cartesian line.
        if(!active_ || status_ != GroupStatus::moving ||
           active_kind_ != GroupPathKind::cartesian_linear ||
           cartesian_.active_segment_.arc_path || cartesian_.active_segment_.chain ||
           cartesian_.active_segment_.pose || active_path_length_ <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
    } else if(cartesian_.window_stopping_) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }

    // Successor target in the plugin Cartesian domain.
    geom::Vec3 target_point = cartesian_part(command.target);
    if(command.coord_system == CoordSystem::pcs) {
        target_point = geom::transform_point(pose_frames_.workpiece_frame_, target_point);
    }
    target_point = target_point - pose_frames_.tool_offset_;

    // Tail geometry: the line the corner attaches to.
    geom::Vec3 tail_end{};
    geom::Vec3 tail_dir{};
    double tail_room = 0.0; // trimmable room on the tail line
    std::size_t tail_index = 0;
    if(!extend) {
        const otg::State1D live = otg::sample(
            active_profile_, rt::CycleTick::from_cycles(active_tick_));
        double s_live = live.position < 0.0 ? 0.0 : live.position;
        s_live = s_live > active_path_length_ ? active_path_length_ : s_live;
        tail_end = geom::Vec3{cartesian_.active_segment_.start.x + cartesian_.active_segment_.delta.x,
                              cartesian_.active_segment_.start.y + cartesian_.active_segment_.delta.y,
                              cartesian_.active_segment_.start.z + cartesian_.active_segment_.delta.z};
        const double len = geom::norm(cartesian_.active_segment_.delta);
        tail_dir = geom::Vec3{cartesian_.active_segment_.delta.x / len,
                              cartesian_.active_segment_.delta.y / len,
                              cartesian_.active_segment_.delta.z / len};
        tail_room = active_path_length_ - s_live;
    } else {
        tail_index = cartesian_.window_.size() - 1;
        const CartPiece &tail = cartesian_.window_[tail_index];
        tail_end = geom::Vec3{tail.start.x + tail.dir.x * tail.length,
                              tail.start.y + tail.dir.y * tail.length,
                              tail.start.z + tail.dir.z * tail.length};
        tail_dir = tail.dir;
        if(tail_index == cartesian_.piece_index_) {
            const otg::State1D live = otg::sample(
                cartesian_.window_[tail_index].profile,
                rt::CycleTick::from_cycles(cartesian_.piece_tick_));
            tail_room = tail.length - live.position;
        } else if(tail_index > cartesian_.piece_index_) {
            tail_room = tail.length;
        } else {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
    }

    const geom::Vec3 out_vec = target_point - tail_end;
    const double len_b = geom::norm(out_vec);
    bool degrade = false;
    double trim = 0.0;
    double corner_cap = 0.0;
    geom::QuinticBlendSegment corner{};
    bool passthrough = false;
    if(len_b <= 1e-12) {
        degrade = true;
    } else {
        const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                            out_vec.z / len_b};
        const double dot =
            tail_dir.x * t1.x + tail_dir.y * t1.y + tail_dir.z * t1.z;
        if(dot <= -0.999) {
            degrade = true;
        } else if(dot >= 1.0 - 1e-9) {
            passthrough = true;
        } else {
            const geom::Vec3 diff{t1.x - tail_dir.x, t1.y - tail_dir.y,
                                  t1.z - tail_dir.z};
            const double turn = geom::norm(diff);
            trim = command.transition_parameter * 96.0 / (23.0 * turn);
            const double room = tail_room < len_b ? tail_room : len_b;
            if(trim > 0.5 * room) {
                trim = 0.5 * room;
            }
            if(trim <= 1e-9 || tail_room <= trim) {
                degrade = true;
            } else {
                const geom::Vec3 entry{tail_end.x - tail_dir.x * trim,
                                       tail_end.y - tail_dir.y * trim,
                                       tail_end.z - tail_dir.z * trim};
                const geom::Vec3 exit{tail_end.x + t1.x * trim,
                                      tail_end.y + t1.y * trim,
                                      tail_end.z + t1.z * trim};
                const rt::Result<geom::QuinticBlendSegment> blend =
                    geom::make_quintic_blend(entry, tail_end, exit,
                                             command.transition_parameter);
                if(!blend) {
                    degrade = true;
                } else {
                    corner = blend.value();
                    const double axis_accel =
                        command.acceleration < command.deceleration
                            ? command.acceleration
                            : command.deceleration;
                    corner_cap = corner.max_curvature > 1e-12
                                     ? std::sqrt(axis_accel /
                                                 corner.max_curvature)
                                     : command.velocity;
                }
            }
        }
    }

    if(!degrade &&
       cartesian_.window_.size() + (passthrough ? 1 : 2) > CartWindowPieces) {
        return rt::Result<std::uint32_t>::failure(
            rt::ErrorCode::capacity_exceeded);
    }
    if(!degrade && !passthrough && command.transition_velocity > 0.0 &&
       command.transition_velocity < corner_cap) {
        corner_cap = command.transition_velocity;
    }

    if(!degrade) {
        // Pre-validation: seed-chain the inverse along the new line (the
        // corner stays inside the tolerance ball of the lines,
        // declared); update the window tail joints.
        double chain[MaxAxes] = {};
        if(!extend) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                chain[i] = active_finish_[i];
            }
        } else {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                chain[i] = cart_tail_joints_[i];
            }
        }
        double q[MaxAxes] = {};
        constexpr int Samples = 32;
        bool valid = true;
        rt::ErrorCode failure = rt::ErrorCode::ok;
        for(int k = 0; k <= Samples && valid; ++k) {
            const double fraction =
                static_cast<double>(k) / static_cast<double>(Samples);
            const geom::Vec3 sample{
                tail_end.x + (target_point.x - tail_end.x) * fraction,
                tail_end.y + (target_point.y - tail_end.y) * fraction,
                tail_end.z + (target_point.z - tail_end.z) * fraction};
            const rt::ErrorCode solved =
                pose_frames_.kinematics_->inverse(sample, chain, axes_.size(), q);
            if(solved != rt::ErrorCode::ok) {
                valid = false;
                failure = solved;
                break;
            }
            if(pose_frames_.kinematics_->singularity_margin(q, axes_.size()) <
               pose_frames_.kinematics_min_margin_) {
                valid = false;
                failure = rt::ErrorCode::precondition_failed;
                break;
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                chain[i] = q[i];
            }
        }
        if(!valid) {
            return rt::Result<std::uint32_t>::failure(failure);
        }

        // Commit geometry. Extending while riding a corner piece is a
        // declared too-late degrade (transient, one corner long).
        if(extend && cartesian_.window_[cartesian_.piece_index_].corner) {
            degrade = true;
        }
        if(!degrade && !extend) {
            cart_window_convert(command, trim, passthrough);
        } else if(!degrade) {
            // Re-anchor the currently executing line piece to its live
            // state so the rebuild replans from reality.
            CartPiece &current = cartesian_.window_[cartesian_.piece_index_];
            const otg::State1D live = otg::sample(
                current.profile, rt::CycleTick::from_cycles(cartesian_.piece_tick_));
            double s_live = live.position < 0.0 ? 0.0 : live.position;
            s_live = s_live > current.length ? current.length : s_live;
            current.start = geom::Vec3{current.start.x + current.dir.x * s_live,
                                       current.start.y + current.dir.y * s_live,
                                       current.start.z + current.dir.z * s_live};
            current.length -= s_live;
            cartesian_.piece_tick_ = 0;
            cartesian_.window_entry_velocity_ = live.velocity < 0.0 ? 0.0 : live.velocity;
            cartesian_.window_entry_acceleration_ = live.acceleration;
            cartesian_.window_[tail_index].length -= passthrough ? 0.0 : trim;
        }
        if(!degrade) {
        if(!passthrough) {
            CartPiece piece{};
            piece.corner = true;
            piece.blend = corner;
            piece.length = corner.length;
            piece.cap = corner_cap;
            cartesian_.window_.push_back(piece);
        }
        CartPiece line{};
        const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                            out_vec.z / len_b};
        line.start = passthrough
                         ? tail_end
                         : geom::Vec3{tail_end.x + t1.x * trim,
                                      tail_end.y + t1.y * trim,
                                      tail_end.z + t1.z * trim};
        line.dir = t1;
        line.length = passthrough ? len_b : len_b - trim;
        line.cap = command.velocity;
        if(passthrough && command.transition_velocity > 0.0 &&
           command.transition_velocity < line.cap) {
            line.cap = command.transition_velocity;
        }
        cartesian_.window_.push_back(line);
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            cart_tail_joints_[i] = chain[i];
        }
        cartesian_.window_acceleration_ = cartesian_.window_acceleration_ < command.acceleration
                               ? cartesian_.window_acceleration_
                               : command.acceleration;
        cartesian_.window_deceleration_ = cartesian_.window_deceleration_ < command.deceleration
                               ? cartesian_.window_deceleration_
                               : command.deceleration;
        cartesian_.window_jerk_ = cartesian_.window_jerk_ < command.jerk
                                ? cartesian_.window_jerk_
                                : command.jerk;
        if(!cart_window_rebuild()) {
            // The rebuild failing after commit would strand geometry;
            // fall back to an immediate errorstop-free degrade: brake.
            cart_window_reset();
            status_ = GroupStatus::standby;
            return rt::Result<std::uint32_t>::failure(
                rt::ErrorCode::infeasible);
        }
        cartesian_.window_last_id_ = command.command_id;
        return rt::Result<std::uint32_t>::success(command.command_id);
        }
    }

    // Reported degradation: plain buffered Cartesian segment behind the
    // window (or behind the active segment).
    last_blend_degraded_id_ = command.command_id;
    command.buffer_mode = BufferMode::buffered;
    command.transition_mode = TransitionMode::none;
    command.transition_velocity = 0.0;
    command.transition_parameter = 0.0;
    const rt::ErrorCode prepared = prepare_cartesian_linear(command);
    if(prepared != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(prepared);
    }
    const rt::ErrorCode queued = queue_.push_back(command);
    if(queued != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(queued);
    }
    return rt::Result<std::uint32_t>::success(command.command_id);
}

inline void AxisGroup::cart_window_convert(const GroupCommand &command, double trim,
                         bool passthrough)
{
    const otg::State1D live =
        otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
    double s_live = live.position < 0.0 ? 0.0 : live.position;
    s_live = s_live > active_path_length_ ? active_path_length_ : s_live;
    const double len = geom::norm(cartesian_.active_segment_.delta);
    const geom::Vec3 dir{cartesian_.active_segment_.delta.x / len,
                         cartesian_.active_segment_.delta.y / len,
                         cartesian_.active_segment_.delta.z / len};
    CartPiece first{};
    first.start = geom::Vec3{cartesian_.active_segment_.start.x + dir.x * s_live,
                             cartesian_.active_segment_.start.y + dir.y * s_live,
                             cartesian_.active_segment_.start.z + dir.z * s_live};
    first.dir = dir;
    first.length = (active_path_length_ - s_live) -
                   (passthrough ? 0.0 : trim);
    first.cap = active_command_.velocity;
    cartesian_.window_.clear();
    cartesian_.window_.push_back(first);
    cartesian_.window_entry_velocity_ = live.velocity < 0.0 ? 0.0 : live.velocity;
    cartesian_.window_entry_acceleration_ = live.acceleration;
    cartesian_.window_acceleration_ = active_command_.acceleration;
    cartesian_.window_deceleration_ = active_command_.deceleration;
    cartesian_.window_jerk_ = active_command_.jerk;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        cartesian_.window_joints_[i] = cartesian_.joints_[i];
    }
    (void)command;
    active_ = false;
    cartesian_.window_active_ = true;
    cartesian_.piece_index_ = 0;
    cartesian_.piece_tick_ = 0;
    status_ = GroupStatus::moving;
}

// Bidirectional node scan and per-line profile planning over every
// piece from the current one onward. Committed pieces before the
// current index are never touched.
inline bool AxisGroup::cart_window_rebuild()
{
    const double acc = cartesian_.window_acceleration_;
    const double dec = cartesian_.window_deceleration_;
    const double jerk = cartesian_.window_jerk_;
    const std::size_t count = cartesian_.window_.size();

    // Forward pass: reachable node velocities.
    double v = cartesian_.window_entry_velocity_;
    for(std::size_t i = cartesian_.piece_index_; i < count; ++i) {
        CartPiece &piece = cartesian_.window_[i];
        if(piece.corner) {
            v = v < piece.cap ? v : piece.cap;
            piece.v_in = v;
            piece.v_out = v;
            continue;
        }
        piece.v_in = v;
        double reach = plan::jerk_reachable_speed(v, piece.length, acc, jerk);
        reach = reach < piece.cap ? reach : piece.cap;
        if(pose_frames_.cartesian_velocity_limit_ > 0.0 &&
           reach > pose_frames_.cartesian_velocity_limit_) {
            reach = pose_frames_.cartesian_velocity_limit_;
        }
        piece.v_out = reach;
        v = reach;
    }
    // Backward pass: terminal rest.
    v = 0.0;
    for(std::size_t r = count; r > cartesian_.piece_index_; --r) {
        CartPiece &piece = cartesian_.window_[r - 1];
        if(piece.corner) {
            v = v < piece.cap ? v : piece.cap;
            piece.v_out = piece.v_out < v ? piece.v_out : v;
            piece.v_in = piece.v_out;
            v = piece.v_in;
            continue;
        }
        piece.v_out = piece.v_out < v ? piece.v_out : v;
        double reach =
            plan::jerk_reachable_speed(piece.v_out, piece.length, dec, jerk);
        piece.v_in = piece.v_in < reach ? piece.v_in : reach;
        v = piece.v_in;
    }
    for(std::size_t i = cartesian_.piece_index_; i < count; ++i) {
        CartPiece &piece = cartesian_.window_[i];
        if(piece.corner) {
            const double speed = piece.v_in > 1e-12 ? piece.v_in : 1e-12;
            piece.duration =
                static_cast<std::int64_t>(piece.length / speed) + 1;
            continue;
        }
        const bool live_entry = i == cartesian_.piece_index_;
        const double entry_v = live_entry ? cartesian_.window_entry_velocity_ : piece.v_in;
        const double entry_a = live_entry ? cartesian_.window_entry_acceleration_ : 0.0;
        double cap = piece.cap;
        if(pose_frames_.cartesian_velocity_limit_ > 0.0 && cap > pose_frames_.cartesian_velocity_limit_) {
            cap = pose_frames_.cartesian_velocity_limit_;
        }
        const otg::Limits1D lim{cap, acc, dec, jerk};
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            {0.0, entry_v, entry_a}, {piece.length, piece.v_out, 0.0},
            lim);
        if(!profile) {
            return false;
        }
        piece.profile = profile.value();
        piece.duration = piece.profile.duration_cycles();
        const double avg_v = entry_v > piece.v_out
            ? entry_v : (piece.v_out > 1e-12 ? piece.v_out : entry_v);
        if(avg_v > 1e-12) {
            const std::int64_t ideal = static_cast<std::int64_t>(
                std::ceil(piece.length / avg_v));
            if(piece.duration > ideal + 4) {
                for(std::int64_t t = ideal; t <= ideal + 4; ++t) {
                    const rt::Result<otg::Profile1D> ft =
                        otg::solve_fixed_time(
                            {0.0, entry_v, entry_a},
                            {piece.length, piece.v_out, 0.0}, lim, t);
                    if(ft) {
                        piece.profile = ft.value();
                        piece.duration = ft.value().duration_cycles();
                        break;
                    }
                }
            }
        }
    }
    return true;
}

inline geom::Vec3 AxisGroup::cart_piece_point(const CartPiece &piece, double s) const
{
    if(piece.corner) {
        const double u = geom::quintic_parameter_at_length(piece.blend, s);
        return geom::quintic_point(piece.blend, u);
    }
    const double clamped = s < 0.0 ? 0.0 : (s > piece.length ? piece.length : s);
    return geom::Vec3{piece.start.x + piece.dir.x * clamped,
                      piece.start.y + piece.dir.y * clamped,
                      piece.start.z + piece.dir.z * clamped};
}

inline bool AxisGroup::cart_window_emit(geom::Vec3 point)
{
    double q[MaxAxes] = {};
    rt::ErrorCode solved =
        pose_frames_.kinematics_->inverse(point, cartesian_.window_joints_, axes_.size(), q);
    if(solved == rt::ErrorCode::ok &&
       pose_frames_.kinematics_->singularity_margin(q, axes_.size()) <
           pose_frames_.kinematics_min_margin_) {
        solved = rt::ErrorCode::precondition_failed;
    }
    if(solved != rt::ErrorCode::ok) {
        cartesian_.last_error_ = solved;
        abort_motion();
        set_group_error(solved);
        return false;
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        axes_[i]->set_synchronized_position(q[i]);
        cartesian_.window_joints_[i] = q[i];
    }
    return true;
}

inline void AxisGroup::cart_window_cycle()
{
    if(cartesian_.window_stopping_) {
        ++cartesian_.halt_tick_;
        const otg::State1D state = otg::sample(
            cartesian_.halt_profile_, rt::CycleTick::from_cycles(cartesian_.halt_tick_));
        const geom::Vec3 point =
            cart_window_point_at(cartesian_.halt_origin_ + state.position);
        if(!cart_window_emit(point)) {
            return;
        }
        if(cartesian_.halt_tick_ >= cartesian_.halt_duration_) {
            cart_window_reset();
            status_ = GroupStatus::standby;
            start_next_queued();
        }
        return;
    }

    ++cartesian_.piece_tick_;
    CartPiece &piece = cartesian_.window_[cartesian_.piece_index_];
    double s = 0.0;
    if(piece.corner) {
        s = piece.v_in * static_cast<double>(cartesian_.piece_tick_);
        s = s > piece.length ? piece.length : s;
    } else {
        const otg::State1D state = otg::sample(
            piece.profile, rt::CycleTick::from_cycles(cartesian_.piece_tick_));
        s = state.position;
    }
    if(!cart_window_emit(cart_piece_point(piece, s))) {
        return;
    }
    if(cartesian_.piece_tick_ >= piece.duration) {
        if(cartesian_.piece_index_ + 1 < cartesian_.window_.size()) {
            ++cartesian_.piece_index_;
            cartesian_.piece_tick_ = 0;
            // Entry state for the freshly entered piece.
            const CartPiece &next = cartesian_.window_[cartesian_.piece_index_];
            cartesian_.window_entry_velocity_ = next.v_in;
            cartesian_.window_entry_acceleration_ = 0.0;
        } else {
            cart_window_reset();
            status_ = GroupStatus::standby;
            start_next_queued();
        }
    }
}

// Composite arc-length lookup from the live point onward (halt walker).
inline geom::Vec3 AxisGroup::cart_window_point_at(double composite) const
{
    double remaining = composite;
    for(std::size_t i = cartesian_.piece_index_; i < cartesian_.window_.size(); ++i) {
        const CartPiece &piece = cartesian_.window_[i];
        double offset = 0.0;
        if(i == cartesian_.piece_index_) {
            offset = cartesian_.halt_piece_offset_;
        }
        const double available = piece.length - offset;
        if(remaining <= available) {
            return cart_piece_point(piece, offset + remaining);
        }
        remaining -= available;
    }
    const CartPiece &last = cartesian_.window_[cartesian_.window_.size() - 1];
    return cart_piece_point(last, last.length);
}

inline rt::ErrorCode AxisGroup::cart_window_stop(double deceleration, double jerk)
{
    if(!std::isfinite(deceleration) || deceleration <= 0.0 ||
       !std::isfinite(jerk) || jerk <= 0.0) {
        return rt::ErrorCode::invalid_argument;
    }
    CartPiece &piece = cartesian_.window_[cartesian_.piece_index_];
    otg::State1D state{};
    if(piece.corner) {
        double s = piece.v_in * static_cast<double>(cartesian_.piece_tick_);
        s = s > piece.length ? piece.length : s;
        state = {s, piece.v_in, 0.0};
    } else {
        state = otg::sample(piece.profile,
                            rt::CycleTick::from_cycles(cartesian_.piece_tick_));
    }
    double remaining = piece.length - state.position;
    for(std::size_t i = cartesian_.piece_index_ + 1; i < cartesian_.window_.size(); ++i) {
        remaining += cartesian_.window_[i].length;
    }
    const otg::Limits1D halt_limits{state.velocity > 1e-12 ? state.velocity
                                                           : 1e-12,
                                    deceleration, deceleration, jerk};
    double target = state.velocity * state.velocity / (2.0 * deceleration) +
                    state.velocity * (deceleration / jerk);
    if(target > remaining) {
        target = remaining;
    }
    rt::Result<otg::Profile1D> halt =
        rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
    for(int attempt = 0; attempt < 8; ++attempt) {
        halt = otg::plan_time_optimal({0.0, state.velocity, state.acceleration},
                                      {target, 0.0, 0.0}, halt_limits);
        if(halt || target >= remaining) {
            break;
        }
        target = target * 1.5 < remaining ? target * 1.5 : remaining;
    }
    if(!halt) {
        // Immediate stop fallback (same as the group linear path).
        cart_window_reset();
        queue_.clear();
        status_ = GroupStatus::standby;
        return rt::ErrorCode::ok;
    }
    cartesian_.halt_profile_ = halt.value();
    cartesian_.halt_duration_ = cartesian_.halt_profile_.duration_cycles();
    cartesian_.halt_tick_ = 0;
    cartesian_.halt_origin_ = 0.0;
    cartesian_.halt_piece_offset_ = state.position;
    cartesian_.window_stopping_ = true;
    queue_.clear();
    status_ = GroupStatus::stopping;
    return rt::ErrorCode::ok;
}

inline void AxisGroup::cart_window_reset()
{
    cartesian_.window_.clear();
    cartesian_.window_active_ = false;
    cartesian_.window_stopping_ = false;
    cartesian_.piece_index_ = 0;
    cartesian_.piece_tick_ = 0;
    cartesian_.halt_tick_ = 0;
    cartesian_.halt_duration_ = 0;
    cartesian_.halt_origin_ = 0.0;
    cartesian_.halt_piece_offset_ = 0.0;
}

// Cartesian v2-C (approved addendum): fuse the active Cartesian line,
// a Cartesian-space quintic corner inside the tolerance band, and the
// successor line into one chain driven by one profile planned from the
// live path state. The chain velocity carries the corner curvature cap;
// orientation rides a single geodesic over the whole chain (declared).
// Reflex corners, too-late submissions, and chains that do not beat the
// full-stop baseline degrade to BUFFERED and are reported.
inline rt::Result<std::uint32_t> AxisGroup::submit_cartesian_blend(GroupCommand command)
{
    if(command.command_id == 0) {
        command.command_id = next_command_id_++;
    }
    if(command.coord_system != CoordSystem::mcs &&
       command.coord_system != CoordSystem::pcs) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.relative ||
       (pose_frames_.kinematics_ == nullptr && pose_frames_.pose_kinematics_ == nullptr)) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    // Mixed-mode blending, arc/rotation-driven actives, committed
    // chains, windows, and non-empty queues are all outside the v1
    // fusion shape.
    if(!active_ || status_ != GroupStatus::moving ||
       active_kind_ != GroupPathKind::cartesian_linear ||
       cartesian_.active_segment_.arc_path || cartesian_.active_segment_.angle_driven ||
       cartesian_.active_segment_.chain || joint_window_.active_ || !queue_.empty() ||
       active_path_length_ <= 0.0) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }

    // Successor target in the Cartesian (TCP) domain.
    geom::Vec3 target_point = cartesian_part(command.target);
    geom::RigidTransform target_pose{};
    if(pose_frames_.pose_kinematics_ != nullptr) {
        target_pose = geom::make_rpy_transform(
            command.target.value[0], command.target.value[1],
            command.target.value[2], command.target.value[3],
            command.target.value[4], command.target.value[5]);
        if(command.coord_system == CoordSystem::pcs) {
            target_pose = geom::compose(pose_frames_.workpiece_frame_, target_pose);
        }
        target_point = target_pose.translation;
    } else {
        if(command.coord_system == CoordSystem::pcs) {
            target_point = geom::transform_point(pose_frames_.workpiece_frame_, target_point);
        }
        target_point = target_point - pose_frames_.tool_offset_;
    }

    // Live path state and geometry.
    const otg::State1D live =
        otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
    const double s_live = live.position < 0.0
                              ? 0.0
                              : (live.position > active_path_length_
                                     ? active_path_length_
                                     : live.position);
    const double remaining = active_path_length_ - s_live;
    const geom::Vec3 live_point =
        cartesian_point_at(cartesian_.active_segment_, s_live / active_path_length_);
    const geom::Vec3 corner_point = geom::Vec3{
        cartesian_.active_segment_.start.x + cartesian_.active_segment_.delta.x,
        cartesian_.active_segment_.start.y + cartesian_.active_segment_.delta.y,
        cartesian_.active_segment_.start.z + cartesian_.active_segment_.delta.z};
    const double active_len = geom::norm(cartesian_.active_segment_.delta);
    if(active_len <= 1e-12) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    const geom::Vec3 t0{cartesian_.active_segment_.delta.x / active_len,
                        cartesian_.active_segment_.delta.y / active_len,
                        cartesian_.active_segment_.delta.z / active_len};
    const geom::Vec3 out_vec = target_point - corner_point;
    const double len_b = geom::norm(out_vec);
    const double dot = len_b > 1e-12
                           ? (t0.x * out_vec.x + t0.y * out_vec.y +
                              t0.z * out_vec.z) /
                                 len_b
                           : -1.0;

    bool degrade = false;
    double trim = 0.0;
    bool passthrough = false;
    if(len_b <= 1e-12 || dot <= -0.999) {
        degrade = true;
    } else if(dot >= 1.0 - 1e-9) {
        passthrough = true;
    } else {
        const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                            out_vec.z / len_b};
        const double turn = geom::norm(t1 - t0);
        trim = command.transition_parameter * 96.0 / (23.0 * turn);
        const double room = remaining < len_b ? remaining : len_b;
        if(trim > 0.5 * room) {
            trim = 0.5 * room;
        }
        if(trim <= 1e-9 || remaining <= trim) {
            degrade = true;
        }
    }

    CartesianSegment segment{};
    rt::Result<otg::Profile1D> chain_profile =
        rt::Result<otg::Profile1D>::failure(rt::ErrorCode::invalid_argument);
    if(!degrade) {
        const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                            out_vec.z / len_b};
        segment.pose = cartesian_.active_segment_.pose;
        segment.chain = true;
        segment.start = live_point;
        segment.dir1 = t0;
        if(passthrough) {
            segment.line1 = remaining;
            segment.exit_point = corner_point;
        } else {
            const geom::Vec3 entry{corner_point.x - t0.x * trim,
                                   corner_point.y - t0.y * trim,
                                   corner_point.z - t0.z * trim};
            const geom::Vec3 exit{corner_point.x + t1.x * trim,
                                  corner_point.y + t1.y * trim,
                                  corner_point.z + t1.z * trim};
            const rt::Result<geom::QuinticBlendSegment> blend =
                geom::make_quintic_blend(entry, corner_point, exit,
                                         command.transition_parameter);
            if(!blend) {
                degrade = true;
            } else {
                segment.corner = blend.value();
                segment.line1 = remaining - trim;
                segment.exit_point = exit;
            }
        }
        segment.dir2 = t1;
        segment.line2 = len_b - trim;
        segment.delta = target_point - live_point;
        segment.length = segment.line1 + segment.corner.length + segment.line2;

        if(!degrade && segment.pose) {
            const geom::RigidTransform live_tcp = pose_start_tcp(cartesian_.joints_);
            if(command.orientation_mode == OrientationMode::constant) {
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        target_pose.rotation[i][j] = live_tcp.rotation[i][j];
                    }
                }
            }
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    segment.rotation_start[i][j] = live_tcp.rotation[i][j];
                }
            }
            geom::relative_axis_angle(live_tcp.rotation, target_pose.rotation,
                                      segment.axis, segment.angle);
            if(segment.angle >= 3.14159265358979323846 - 1e-6) {
                return rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::invalid_argument);
            }
        }
    }

    if(!degrade) {
        // Chain envelope: both commands and the corner curvature cap.
        GroupCommand fused = command;
        fused.velocity = fused.velocity < active_command_.velocity
                             ? fused.velocity
                             : active_command_.velocity;
        fused.acceleration = fused.acceleration < active_command_.acceleration
                                 ? fused.acceleration
                                 : active_command_.acceleration;
        fused.deceleration = fused.deceleration < active_command_.deceleration
                                 ? fused.deceleration
                                 : active_command_.deceleration;
        fused.jerk =
            fused.jerk < active_command_.jerk ? fused.jerk : active_command_.jerk;
        if(command.transition_velocity > 0.0 &&
           command.transition_velocity < fused.velocity) {
            // This planner has one profile for the fused chain, so the
            // explicit junction cap conservatively bounds that profile.
            fused.velocity = command.transition_velocity;
        }
        if(segment.corner.max_curvature > 1e-12) {
            const double axis_accel =
                fused.acceleration < fused.deceleration ? fused.acceleration
                                                        : fused.deceleration;
            const double cap = std::sqrt(axis_accel / segment.corner.max_curvature);
            if(cap < fused.velocity) {
                fused.velocity = cap;
            }
        }
        double chain_seed[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            chain_seed[i] = cartesian_.joints_[i];
        }
        const rt::ErrorCode validated =
            prevalidate_cartesian(fused, segment, chain_seed);
        if(validated != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(validated);
        }
        chain_profile = otg::plan_time_optimal(
            {0.0, live.velocity, live.acceleration},
            {fused.cart.length, 0.0, 0.0},
            {fused.velocity, fused.acceleration, fused.deceleration, fused.jerk});
        if(!chain_profile) {
            degrade = true;
        } else {
            // Constructive gate: the fused chain must beat the full-stop
            // baseline (finish the active segment, then run the successor
            // from rest).
            const rt::Result<otg::Profile1D> tail = otg::plan_time_optimal(
                {0.0, 0.0, 0.0}, {len_b, 0.0, 0.0},
                {command.velocity, command.acceleration, command.deceleration,
                 command.jerk});
            if(tail) {
                const std::int64_t baseline = (active_duration_ - active_tick_) +
                                              tail.value().duration_cycles();
                if(chain_profile.value().duration_cycles() >= baseline) {
                    degrade = true;
                }
            } else {
                degrade = true;
            }
        }
        if(!degrade) {
            active_command_ = fused;
            cartesian_.active_segment_ = fused.cart;
            active_kind_ = GroupPathKind::cartesian_linear;
            active_arc_ = geom::ArcSegment{};
            active_path_length_ = fused.cart.length;
            active_profile_ = chain_profile.value();
            active_tick_ = 0;
            active_duration_ = active_profile_.duration_cycles();
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                active_start_[i] = axes_[i]->snapshot().command_position;
                active_finish_[i] = fused.target.value[i];
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }
    }

    // Reported degradation to a plain buffered Cartesian segment.
    last_blend_degraded_id_ = command.command_id;
    command.buffer_mode = BufferMode::buffered;
    command.transition_mode = TransitionMode::none;
    command.transition_velocity = 0.0;
    command.transition_parameter = 0.0;
    const rt::ErrorCode prepared = prepare_cartesian_linear(command);
    if(prepared != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(prepared);
    }
    const rt::ErrorCode queued = queue_.push_back(command);
    if(queued != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(queued);
    }
    return rt::Result<std::uint32_t>::success(command.command_id);
}

inline rt::ErrorCode AxisGroup::prepare_cartesian_linear(GroupCommand &command)
{
    const rt::ErrorCode guarded = cartesian_guards(command);
    if(guarded != rt::ErrorCode::ok) {
        return guarded;
    }
    double chain[MaxAxes] = {};
    segment_start_joints(command, chain);

    CartesianSegment segment{};
    if(pose_frames_.pose_kinematics_ != nullptr) {
        segment.pose = true;
        const geom::RigidTransform requested = geom::make_rpy_transform(
            command.target.value[0], command.target.value[1],
            command.target.value[2], command.target.value[3],
            command.target.value[4], command.target.value[5]);
        const geom::RigidTransform start = pose_start_tcp(chain);
        geom::RigidTransform target = requested;
        if(command.relative) {
            geom::Vec3 displacement = requested.translation;
            if(command.coord_system == CoordSystem::pcs) {
                displacement = geom::transform_rotate(pose_frames_.workpiece_frame_, displacement);
            }
            target.translation = start.translation + displacement;
            if(command.orientation_mode == OrientationMode::constant) {
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        target.rotation[i][j] = start.rotation[i][j];
                    }
                }
            } else {
                geom::rotation_multiply(start.rotation, requested.rotation,
                                        target.rotation);
            }
        } else {
            if(command.coord_system == CoordSystem::pcs) {
                target = geom::compose(pose_frames_.workpiece_frame_, target);
            }
            if(command.orientation_mode == OrientationMode::constant) {
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        target.rotation[i][j] = start.rotation[i][j];
                    }
                }
            }
        }
        segment.start = start.translation;
        segment.delta = target.translation - start.translation;
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                segment.rotation_start[i][j] = start.rotation[i][j];
            }
        }
        geom::relative_axis_angle(start.rotation, target.rotation, segment.axis,
                                  segment.angle);
        if(segment.angle >= 3.14159265358979323846 - 1e-6) {
            return rt::ErrorCode::invalid_argument;
        }
        segment.length = geom::norm(segment.delta);
        if(segment.length < 1e-12 && segment.angle > 0.0) {
            segment.angle_driven = true;
            segment.length = segment.angle;
        }
    } else {
        geom::Vec3 point = cartesian_part(command.target);
        geom::Vec3 start{};
        const rt::ErrorCode forwarded =
            pose_frames_.kinematics_->forward(chain, axes_.size(), start);
        if(forwarded != rt::ErrorCode::ok) {
            return forwarded;
        }
        if(command.relative) {
            if(command.coord_system == CoordSystem::pcs) {
                point = geom::transform_rotate(pose_frames_.workpiece_frame_, point);
            }
            point = start + point;
        } else {
            if(command.coord_system == CoordSystem::pcs) {
                point = geom::transform_point(pose_frames_.workpiece_frame_, point);
            }
            point = point - pose_frames_.tool_offset_;
        }
        segment.start = start;
        segment.delta = point - start;
        segment.length = geom::norm(segment.delta);
    }
    command.relative = false;
    return prevalidate_cartesian(command, segment, chain);
}

// Cartesian v2-B (approved addendum): three-point BORDER arcs in the
// Cartesian XY plane, z following the path parameter linearly — the
// KB-030 plane convention transplanted to the TCP domain (arbitrary
// spatial arc planes stay a follow-up, recorded). Pose groups ride the
// start-to-target geodesic along the arc fraction; aux orientation
// slots are ignored (declared).
inline rt::ErrorCode AxisGroup::prepare_cartesian_circular(GroupCommand &command)
{
    const rt::ErrorCode guarded = cartesian_guards(command);
    if(guarded != rt::ErrorCode::ok) {
        return guarded;
    }
    const bool pcs = command.coord_system == CoordSystem::pcs;
    double chain[MaxAxes] = {};
    segment_start_joints(command, chain);

    CartesianSegment segment{};
    geom::Vec3 start_point{};
    geom::Vec3 aux_point = cartesian_part(command.aux);
    geom::Vec3 target_point = cartesian_part(command.target);
    if(pose_frames_.pose_kinematics_ != nullptr) {
        segment.pose = true;
        const geom::RigidTransform requested = geom::make_rpy_transform(
            command.target.value[0], command.target.value[1],
            command.target.value[2], command.target.value[3],
            command.target.value[4], command.target.value[5]);
        const geom::RigidTransform start = pose_start_tcp(chain);
        start_point = start.translation;
        geom::RigidTransform target = requested;
        if(command.relative) {
            geom::Vec3 target_delta = requested.translation;
            if(pcs) {
                target_delta = geom::transform_rotate(pose_frames_.workpiece_frame_, target_delta);
                aux_point = geom::transform_rotate(pose_frames_.workpiece_frame_, aux_point);
            }
            target.translation = start.translation + target_delta;
            aux_point = start.translation + aux_point;
            if(command.orientation_mode == OrientationMode::constant) {
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        target.rotation[i][j] = start.rotation[i][j];
                    }
                }
            } else {
                geom::rotation_multiply(start.rotation, requested.rotation,
                                        target.rotation);
            }
        } else {
            if(pcs) {
                target = geom::compose(pose_frames_.workpiece_frame_, target);
                aux_point = geom::transform_point(pose_frames_.workpiece_frame_, aux_point);
            }
            if(command.orientation_mode == OrientationMode::constant) {
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        target.rotation[i][j] = start.rotation[i][j];
                    }
                }
            }
        }
        target_point = target.translation;
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                segment.rotation_start[i][j] = start.rotation[i][j];
            }
        }
        geom::relative_axis_angle(start.rotation, target.rotation, segment.axis,
                                  segment.angle);
        if(segment.angle >= 3.14159265358979323846 - 1e-6) {
            return rt::ErrorCode::invalid_argument;
        }
    } else {
        geom::Vec3 start{};
        const rt::ErrorCode forwarded =
            pose_frames_.kinematics_->forward(chain, axes_.size(), start);
        if(forwarded != rt::ErrorCode::ok) {
            return forwarded;
        }
        start_point = start;
        if(command.relative) {
            if(pcs) {
                aux_point = geom::transform_rotate(pose_frames_.workpiece_frame_, aux_point);
                target_point = geom::transform_rotate(pose_frames_.workpiece_frame_, target_point);
            }
            aux_point = start + aux_point;
            target_point = start + target_point;
        } else {
            if(pcs) {
                aux_point = geom::transform_point(pose_frames_.workpiece_frame_, aux_point);
                target_point = geom::transform_point(pose_frames_.workpiece_frame_, target_point);
            }
            aux_point = aux_point - pose_frames_.tool_offset_;
            target_point = target_point - pose_frames_.tool_offset_;
        }
    }

    const geom::Vec3 plane_start{start_point.x, start_point.y, 0.0};
    const geom::Vec3 plane_aux{aux_point.x, aux_point.y, 0.0};
    const geom::Vec3 plane_finish{target_point.x, target_point.y, 0.0};
    constexpr double PointTolerance = 1e-12;
    if(geom::norm(plane_aux - plane_start) <= PointTolerance ||
       geom::norm(plane_finish - plane_aux) <= PointTolerance ||
       geom::norm(plane_finish - plane_start) <= PointTolerance) {
        return rt::ErrorCode::invalid_argument;
    }
    const rt::Result<geom::ArcSegment> arc =
        geom::make_arc(plane_start, plane_aux, plane_finish);
    if(!arc) {
        return rt::ErrorCode::invalid_argument;
    }
    if(arc.value().radius > geom::norm(plane_finish - plane_start) * 1e6) {
        return rt::ErrorCode::invalid_argument;
    }
    const CircPathChoice derived = arc.value().sweep >= 0.0
                                       ? CircPathChoice::counter_clockwise
                                       : CircPathChoice::clockwise;
    if(derived != command.path_choice) {
        return rt::ErrorCode::invalid_argument;
    }
    if(command.tolerance > 0.0) {
        const double via_sweep = geom::normalize_sweep(
            geom::angle_of(plane_aux, arc.value().center) -
                arc.value().start_angle,
            arc.value().sweep);
        const double fraction = via_sweep / arc.value().sweep;
        const double expected_z = start_point.z +
                                  (target_point.z - start_point.z) * fraction;
        if(std::fabs(aux_point.z - expected_z) > command.tolerance) {
            return rt::ErrorCode::invalid_argument;
        }
    }
    segment.arc_path = true;
    segment.arc = arc.value();
    segment.start = start_point;
    segment.delta = target_point - start_point;
    segment.length = arc.value().length;
    command.relative = false;
    return prevalidate_cartesian(command, segment, chain);
}

inline rt::ErrorCode AxisGroup::cartesian_guards(const GroupCommand &command) const
{
    if(command.coord_system != CoordSystem::mcs &&
       command.coord_system != CoordSystem::pcs) {
        return rt::ErrorCode::unsupported;
    }
    if((command.relative &&
        command.orientation_mode == OrientationMode::joint_space) ||
       command.buffer_mode == BufferMode::blending_low ||
       command.buffer_mode == BufferMode::blending_high ||
       command.transition_mode != TransitionMode::none ||
       command.transition_parameter != 0.0) {
        return rt::ErrorCode::unsupported;
    }
    if(pose_frames_.kinematics_ == nullptr && pose_frames_.pose_kinematics_ == nullptr) {
        return rt::ErrorCode::unsupported;
    }
    return rt::ErrorCode::ok;
}

inline void AxisGroup::segment_start_joints(const GroupCommand &command, double *chain) const
{
    const bool aborting = command.buffer_mode == BufferMode::aborting;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        chain[i] = aborting ? axes_[i]->snapshot().command_position
                            : queued_finish(i);
    }
}

inline geom::RigidTransform AxisGroup::pose_start_tcp(const double *chain) const
{
    kin::Pose6 flange{};
    pose_frames_.pose_kinematics_->forward(chain, flange);
    geom::RigidTransform start{};
    start.translation = geom::Vec3{flange.position[0], flange.position[1],
                                   flange.position[2]};
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            start.rotation[i][j] = flange.rotation[i][j];
        }
    }
    return geom::compose(start, pose_frames_.pose_tool_);
}

// Shared 33-sample pre-validation and command commit for every
// Cartesian segment shape.
inline rt::ErrorCode AxisGroup::prevalidate_cartesian(GroupCommand &command,
                                    CartesianSegment &segment,
                                    double *chain)
{
    constexpr int Samples = 32;
    double q[MaxAxes] = {};
    double worst_step = 0.0;

    for(int k = 0; k <= Samples; ++k) {
        const double fraction =
            static_cast<double>(k) / static_cast<double>(Samples);
        rt::ErrorCode solved = rt::ErrorCode::ok;
        if(segment.pose) {
            geom::RigidTransform tcp{};
            cartesian_pose_at(segment, fraction, tcp);
            const geom::RigidTransform flange_target =
                geom::compose(tcp, pose_frames_.pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange_target.translation.x;
            pose.position[1] = flange_target.translation.y;
            pose.position[2] = flange_target.translation.z;
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    pose.rotation[i][j] = flange_target.rotation[i][j];
                }
            }
            solved = pose_frames_.pose_kinematics_->inverse(pose, chain,
                                               pose_frames_.pose_max_joint_step_, q);
            if(solved == rt::ErrorCode::ok &&
               pose_frames_.pose_kinematics_->singularity_margin(q) < pose_frames_.pose_min_margin_) {
                return rt::ErrorCode::precondition_failed;
            }
        } else {
            const geom::Vec3 sample = cartesian_point_at(segment, fraction);
            solved = pose_frames_.kinematics_->inverse(sample, chain, axes_.size(), q);
            if(solved == rt::ErrorCode::ok &&
               pose_frames_.kinematics_->singularity_margin(q, axes_.size()) <
                   pose_frames_.kinematics_min_margin_) {
                return rt::ErrorCode::precondition_failed;
            }
        }
        if(solved != rt::ErrorCode::ok) {
            return solved;
        }
        if(k > 0) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double step = std::fabs(q[i] - chain[i]);
                if(step > worst_step) {
                    worst_step = step;
                }
            }
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            chain[i] = q[i];
        }
    }
    // Velocity budget (decision #6): the step gate doubles as the joint
    // velocity budget with a safety factor of two (pose pipeline).
    if(segment.pose && worst_step > 0.0 && segment.length > 0.0) {
        const double per_unit =
            worst_step / (segment.length / static_cast<double>(Samples));
        const double allowed = 0.5 * pose_frames_.pose_max_joint_step_ / per_unit;
        if(allowed < command.velocity) {
            command.velocity = allowed;
        }
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        command.target.value[i] = q[i];
    }
    if(pose_frames_.cartesian_velocity_limit_ > 0.0 &&
       pose_frames_.cartesian_velocity_limit_ < command.velocity) {
        command.velocity = pose_frames_.cartesian_velocity_limit_;
    }
    command.coord_system = CoordSystem::acs;
    command.path_kind = GroupPathKind::cartesian_linear;
    command.cart = segment;
    return preflight_member_targets(command.target);
}

inline geom::Vec3 AxisGroup::cartesian_point_at(const CartesianSegment &segment,
                              double fraction) const
{
    if(segment.chain) {
        const double s = fraction * segment.length;
        if(s <= segment.line1) {
            return geom::Vec3{segment.start.x + segment.dir1.x * s,
                              segment.start.y + segment.dir1.y * s,
                              segment.start.z + segment.dir1.z * s};
        }
        const double in_corner = s - segment.line1;
        if(in_corner <= segment.corner.length) {
            const double u =
                geom::quintic_parameter_at_length(segment.corner, in_corner);
            return geom::quintic_point(segment.corner, u);
        }
        const double tail = s - segment.line1 - segment.corner.length;
        return geom::Vec3{segment.exit_point.x + segment.dir2.x * tail,
                          segment.exit_point.y + segment.dir2.y * tail,
                          segment.exit_point.z + segment.dir2.z * tail};
    }
    if(segment.arc_path) {
        geom::Vec3 point =
            geom::sample(segment.arc, fraction * segment.arc.length);
        point.z = segment.start.z + segment.delta.z * fraction;
        return point;
    }
    return geom::Vec3{segment.start.x + segment.delta.x * fraction,
                      segment.start.y + segment.delta.y * fraction,
                      segment.start.z + segment.delta.z * fraction};
}

inline void AxisGroup::cartesian_pose_at(const CartesianSegment &segment,
                       double fraction,
                       geom::RigidTransform &tcp) const
{
    tcp.translation = cartesian_point_at(segment, fraction);
    double relative[3][3];
    geom::rodrigues(segment.axis, segment.angle * fraction, relative);
    geom::rotation_multiply(segment.rotation_start, relative, tcp.rotation);
}

// Cycle-path Cartesian sampling (approved matrix decisions #3/#4/#7):
// one analytic inverse per cycle, seeded by the previous cycle's
// joints. A failure between the pre-validation samples is the declared
// group errorstop — members keep the last good setpoint, nothing
// extrapolates, nothing flips branches.
inline bool AxisGroup::cartesian_cycle(double ratio)
{
    double q[MaxAxes] = {};
    rt::ErrorCode solved = rt::ErrorCode::ok;
    if(cartesian_.active_segment_.pose) {
        geom::RigidTransform tcp{};
        cartesian_pose_at(cartesian_.active_segment_, ratio, tcp);
        const geom::RigidTransform flange_target =
            geom::compose(tcp, pose_frames_.active_pose_tool_inverse_);
        kin::Pose6 pose{};
        pose.position[0] = flange_target.translation.x;
        pose.position[1] = flange_target.translation.y;
        pose.position[2] = flange_target.translation.z;
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                pose.rotation[i][j] = flange_target.rotation[i][j];
            }
        }
        solved = pose_frames_.pose_kinematics_->inverse(pose, cartesian_.joints_,
                                           pose_frames_.pose_max_joint_step_, q);
        if(solved == rt::ErrorCode::ok &&
           pose_frames_.pose_kinematics_->singularity_margin(q) < pose_frames_.pose_min_margin_) {
            solved = rt::ErrorCode::precondition_failed;
        }
    } else {
        const geom::Vec3 point = cartesian_point_at(cartesian_.active_segment_, ratio);
        solved = pose_frames_.kinematics_->inverse(point, cartesian_.joints_, axes_.size(), q);
        if(solved == rt::ErrorCode::ok &&
           pose_frames_.kinematics_->singularity_margin(q, axes_.size()) <
               pose_frames_.kinematics_min_margin_) {
            solved = rt::ErrorCode::precondition_failed;
        }
    }
    if(solved != rt::ErrorCode::ok) {
        cartesian_.last_error_ = solved;
        abort_motion();
        set_group_error(solved);
        return false;
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        axes_[i]->set_synchronized_position(q[i]);
        cartesian_.joints_[i] = q[i];
    }
    return true;
}

} // namespace plcopen::core::axis
