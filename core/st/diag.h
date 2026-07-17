#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Diagnostic contract (approved st-l0-semantics 2.2): {line, column, stable
// machine-readable code, message}. Codes carry explicit stable values and are
// grouped by decade blocks; unsupported IEC constructs get dedicated codes
// distinct from syntax errors (matrix 6). Load-domain only.

namespace plcopen::core::st
{

enum class DiagCode : std::uint16_t
{
    // 1xx lexical
    lex_invalid_character = 100,
    lex_unterminated_comment = 101,
    lex_bad_numeric_literal = 102,
    lex_bad_time_literal = 103,
    lex_literal_overflow = 104,
    lex_bad_identifier = 105,

    // 2xx parse
    parse_expected_token = 200,
    parse_unexpected_token = 201,
    parse_nesting_too_deep = 202,
    parse_expected_statement = 203,
    parse_expected_expression = 204,
    parse_expected_type = 205,

    // 3xx semantic
    sema_unknown_identifier = 300,
    sema_duplicate_identifier = 301,
    sema_type_mismatch = 302,
    sema_literal_out_of_range = 303,
    sema_not_assignable = 304,
    sema_division_by_zero_const = 305,
    sema_for_control_assigned = 306,
    sema_for_step_zero_const = 307,
    sema_exit_outside_loop = 308,
    sema_unknown_fb_pin = 309,
    sema_pin_not_input = 310,
    sema_pin_not_output = 311,
    sema_not_const_expr = 312,
    sema_case_label_duplicate = 313,
    sema_case_label_range_invalid = 314,
    sema_not_fb_instance = 315,
    sema_operand_type_invalid = 316,
    sema_ambiguous_literal = 317,
    sema_range_violation = 318,
    sema_index_out_of_range = 319,
    sema_invalid_array_bounds = 320,
    sema_recursive_type = 321,
    sema_string_capacity_exceeded = 322,
    sema_invalid_string_literal = 323,
    date_time_range_violation = 324,
    sema_alias_violation = 325,
    sema_inout_requires_lvalue = 326,
    sema_missing_argument = 327,
    sema_duplicate_argument = 328,
    sema_call_form_mixed = 329,
    sema_duplicate_pou = 330,
    sema_external_required = 331,
    sema_external_not_unique = 332,
    sema_fb_instance_required = 333,
    sema_standard_name_conflict = 334,
    sema_recursive_pou = 335,

    // 4xx capacity (all explicit, never silent truncation; matrix 3.11)
    capacity_code = 400,
    capacity_variables = 401,
    capacity_fb_instances = 402,
    capacity_diagnostics = 403,
    capacity_stack = 404,
    capacity_types = 405,
    capacity_exceeded = 406,

    // 5xx unsupported constructs with batch ownership (matrix 6)
    unsupported_l1 = 501,
    unsupported_l2 = 502,
    unsupported_l3 = 503,
    unsupported_l4 = 504,
    unsupported_l5 = 505,
    unsupported_l6 = 506,
    unsupported_l7 = 507,
    unsupported_non_goal = 508,
    unsupported_l1b2_dynamic_array = 509,
    unsupported_l1b3_timezone = 510,
    unsupported_l2b_object_extension = 511,
};

struct Diagnostic
{
    std::int32_t line = 0;
    std::int32_t column = 0;
    DiagCode code = DiagCode::parse_unexpected_token;
    std::string message;
};

constexpr const char *to_string(DiagCode code)
{
    switch(code) {
    case DiagCode::lex_invalid_character: return "lex_invalid_character";
    case DiagCode::lex_unterminated_comment: return "lex_unterminated_comment";
    case DiagCode::lex_bad_numeric_literal: return "lex_bad_numeric_literal";
    case DiagCode::lex_bad_time_literal: return "lex_bad_time_literal";
    case DiagCode::lex_literal_overflow: return "lex_literal_overflow";
    case DiagCode::lex_bad_identifier: return "lex_bad_identifier";
    case DiagCode::parse_expected_token: return "parse_expected_token";
    case DiagCode::parse_unexpected_token: return "parse_unexpected_token";
    case DiagCode::parse_nesting_too_deep: return "parse_nesting_too_deep";
    case DiagCode::parse_expected_statement: return "parse_expected_statement";
    case DiagCode::parse_expected_expression: return "parse_expected_expression";
    case DiagCode::parse_expected_type: return "parse_expected_type";
    case DiagCode::sema_unknown_identifier: return "sema_unknown_identifier";
    case DiagCode::sema_duplicate_identifier: return "sema_duplicate_identifier";
    case DiagCode::sema_type_mismatch: return "sema_type_mismatch";
    case DiagCode::sema_literal_out_of_range: return "sema_literal_out_of_range";
    case DiagCode::sema_not_assignable: return "sema_not_assignable";
    case DiagCode::sema_division_by_zero_const: return "sema_division_by_zero_const";
    case DiagCode::sema_for_control_assigned: return "sema_for_control_assigned";
    case DiagCode::sema_for_step_zero_const: return "sema_for_step_zero_const";
    case DiagCode::sema_exit_outside_loop: return "sema_exit_outside_loop";
    case DiagCode::sema_unknown_fb_pin: return "sema_unknown_fb_pin";
    case DiagCode::sema_pin_not_input: return "sema_pin_not_input";
    case DiagCode::sema_pin_not_output: return "sema_pin_not_output";
    case DiagCode::sema_not_const_expr: return "sema_not_const_expr";
    case DiagCode::sema_case_label_duplicate: return "sema_case_label_duplicate";
    case DiagCode::sema_case_label_range_invalid: return "sema_case_label_range_invalid";
    case DiagCode::sema_not_fb_instance: return "sema_not_fb_instance";
    case DiagCode::sema_operand_type_invalid: return "sema_operand_type_invalid";
    case DiagCode::sema_ambiguous_literal: return "sema_ambiguous_literal";
    case DiagCode::sema_range_violation: return "sema_range_violation";
    case DiagCode::sema_index_out_of_range: return "sema_index_out_of_range";
    case DiagCode::sema_invalid_array_bounds: return "sema_invalid_array_bounds";
    case DiagCode::sema_recursive_type: return "sema_recursive_type";
    case DiagCode::sema_string_capacity_exceeded: return "sema_string_capacity_exceeded";
    case DiagCode::sema_invalid_string_literal: return "sema_invalid_string_literal";
    case DiagCode::date_time_range_violation: return "date_time_range_violation";
    case DiagCode::sema_alias_violation: return "sema_alias_violation";
    case DiagCode::sema_inout_requires_lvalue: return "sema_inout_requires_lvalue";
    case DiagCode::sema_missing_argument: return "sema_missing_argument";
    case DiagCode::sema_duplicate_argument: return "sema_duplicate_argument";
    case DiagCode::sema_call_form_mixed: return "sema_call_form_mixed";
    case DiagCode::sema_duplicate_pou: return "sema_duplicate_pou";
    case DiagCode::sema_external_required: return "sema_external_required";
    case DiagCode::sema_external_not_unique: return "sema_external_not_unique";
    case DiagCode::sema_fb_instance_required: return "sema_fb_instance_required";
    case DiagCode::sema_standard_name_conflict: return "sema_standard_name_conflict";
    case DiagCode::sema_recursive_pou: return "sema_recursive_pou";
    case DiagCode::capacity_code: return "capacity_code";
    case DiagCode::capacity_variables: return "capacity_variables";
    case DiagCode::capacity_fb_instances: return "capacity_fb_instances";
    case DiagCode::capacity_diagnostics: return "capacity_diagnostics";
    case DiagCode::capacity_stack: return "capacity_stack";
    case DiagCode::capacity_types: return "capacity_types";
    case DiagCode::capacity_exceeded: return "capacity_exceeded";
    case DiagCode::unsupported_l1: return "unsupported_l1";
    case DiagCode::unsupported_l2: return "unsupported_l2";
    case DiagCode::unsupported_l3: return "unsupported_l3";
    case DiagCode::unsupported_l4: return "unsupported_l4";
    case DiagCode::unsupported_l5: return "unsupported_l5";
    case DiagCode::unsupported_l6: return "unsupported_l6";
    case DiagCode::unsupported_l7: return "unsupported_l7";
    case DiagCode::unsupported_non_goal: return "unsupported_non_goal";
    case DiagCode::unsupported_l1b2_dynamic_array: return "unsupported_l1b2_dynamic_array";
    case DiagCode::unsupported_l1b3_timezone: return "unsupported_l1b3_timezone";
    case DiagCode::unsupported_l2b_object_extension: return "unsupported_l2b_object_extension";
    }
    return "unknown";
}

} // namespace plcopen::core::st
