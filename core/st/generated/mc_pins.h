#pragma once

namespace generated
{

inline constexpr PinDesc kPinsMC_Power[] = {
    {"axis", Type::axis_ref, true},
    {"enable", Type::bool_, true},
    {"enablepositive", Type::bool_, true},
    {"enablenegative", Type::bool_, true},
    {"status", Type::bool_, false},
    {"valid", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_Home[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"position", Type::lreal, true},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_Stop[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_Halt[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_MoveAbsolute[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"continuousupdate", Type::bool_, true},
    {"position", Type::lreal, true},
    {"velocity", Type::lreal, true},
    {"acceleration", Type::lreal, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"direction", Type::dint, true, binding_type::mc_direction},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_MoveRelative[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"continuousupdate", Type::bool_, true},
    {"distance", Type::lreal, true},
    {"velocity", Type::lreal, true},
    {"acceleration", Type::lreal, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_MoveAdditive[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"continuousupdate", Type::bool_, true},
    {"distance", Type::lreal, true},
    {"velocity", Type::lreal, true},
    {"acceleration", Type::lreal, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_MoveVelocity[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"continuousupdate", Type::bool_, true},
    {"velocity", Type::lreal, true},
    {"acceleration", Type::lreal, true},
    {"deceleration", Type::lreal, true},
    {"jerk", Type::lreal, true},
    {"direction", Type::dint, true, binding_type::mc_direction},
    {"buffermode", Type::dint, true, binding_type::mc_buffer_mode},
    {"invelocity", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"active", Type::bool_, false},
    {"commandaborted", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_SetOverride[] = {
    {"axis", Type::axis_ref, true},
    {"enable", Type::bool_, true},
    {"velfactor", Type::lreal, true},
    {"accfactor", Type::lreal, true},
    {"jerkfactor", Type::lreal, true},
    {"enabled", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

inline constexpr PinDesc kPinsMC_Reset[] = {
    {"axis", Type::axis_ref, true},
    {"execute", Type::bool_, true},
    {"done", Type::bool_, false},
    {"busy", Type::bool_, false},
    {"error", Type::bool_, false},
    {"errorid", Type::dint, false},
};

} // namespace generated
