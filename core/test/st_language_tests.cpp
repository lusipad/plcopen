// D1 ST LanguageDocument acceptance tests (approved
// st-language-server-semantics.md): tolerant diagnostics and queries,
// authoritative built-ins, user symbol definitions, UTF-16 positions and
// POU-grained index reuse. Plain-main + fail() per house style.

#include <cstdio>
#include <string>
#include <string_view>

#include "st/language.h"

namespace
{

using namespace plcopen::core;

int failures = 0;

void fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    ++failures;
}

void check(bool condition, const char *name)
{
    if (!condition)
    {
        fail(name);
    }
}

std::size_t utf8_scalar_bytes(std::string_view text, std::size_t offset)
{
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U)
        return 1U;
    if ((lead & 0xe0U) == 0xc0U && offset + 1U < text.size())
        return 2U;
    if ((lead & 0xf0U) == 0xe0U && offset + 2U < text.size())
        return 3U;
    if ((lead & 0xf8U) == 0xf0U && offset + 3U < text.size())
        return 4U;
    return 1U;
}

st::LanguagePosition position_at(std::string_view text, std::size_t offset)
{
    st::LanguagePosition result;
    std::size_t at = 0;
    while (at < offset)
    {
        if (text[at] == '\r' && at + 1U < offset && text[at + 1U] == '\n')
        {
            ++result.line;
            result.character = 0;
            at += 2U;
            continue;
        }
        if (text[at] == '\n')
        {
            ++result.line;
            result.character = 0;
            ++at;
            continue;
        }
        const std::size_t bytes = utf8_scalar_bytes(text, at);
        result.character += bytes == 4U ? 2 : 1;
        at += bytes;
    }
    return result;
}

st::LanguagePosition position_of(std::string_view text, std::string_view needle,
                                 std::size_t from = 0)
{
    const std::size_t offset = text.find(needle, from);
    if (offset == std::string_view::npos)
    {
        fail("test needle exists");
        return {};
    }
    return position_at(text, offset);
}

bool has_completion(const st::LanguageCompletionList &result, std::string_view label)
{
    for (const st::LanguageCompletionItem &item : result.items)
    {
        if (st::language_ascii_iequals(item.label, label))
        {
            return true;
        }
    }
    return false;
}

const st::LanguageCompletionItem *completion(const st::LanguageCompletionList &result,
                                             std::string_view label)
{
    for (const st::LanguageCompletionItem &item : result.items)
    {
        if (st::language_ascii_iequals(item.label, label))
        {
            return &item;
        }
    }
    return nullptr;
}

void tolerant_diagnostics_and_authoritative_completion()
{
    const std::string source = "PROGRAM Main\n"
                               "VAR\n"
                               "  Counter : INT;\n"
                               "  Motion : MC_MoveAbsolute;\n"
                               "END_VAR\n"
                               "Counter := ;\n"
                               "Counter := ABS(Counter);\n"
                               "END_PROGRAM\n";

    st::LanguageDocument document(source);
    check(!document.diagnostics().empty(), "bad source keeps compile diagnostics");
    check(!document.diagnostics().front().code.empty(), "diagnostic exposes stable code");

    const st::LanguageCompletionList result =
        document.complete(position_of(source, "Counter := ;"));
    check(has_completion(result, "Counter"), "completion has local variable");
    check(has_completion(result, "ABS"), "completion has standard function");
    check(has_completion(result, "BOOL"), "completion has builtin type");
    check(has_completion(result, "MC_MoveAbsolute"), "completion has binding manifest FB");

    const st::LanguageHover local = document.hover(position_of(source, "Counter := ;"));
    check(local.found, "local hover found");
    check(local.contents.find("Counter") != std::string::npos,
          "local hover preserves declaration spelling");
    check(local.contents.find("INT") != std::string::npos, "local hover carries declared type");

    const st::LanguageHover standard = document.hover(position_of(source, "ABS(Counter)"));
    check(standard.found, "standard function hover found");
    check(standard.contents.find("ABS") != std::string::npos,
          "standard function hover uses manifest spelling");

    for (const st::StandardFunctionManifestEntry &function : st::standard_function_manifest())
    {
        if (function.registered)
        {
            check(has_completion(result, function.name), "all standard functions are offered");
        }
    }
    for (const st::generated::StBindingFbMetadata &fb : st::generated::kStBindingFbs)
    {
        check(has_completion(result, fb.name), "all declared standard FBs are offered");
    }
    check(has_completion(result, "INT_TO_REAL"), "registered conversion function is offered");
}

void scoped_definition_and_user_fb_pins()
{
    const std::string source = "VAR_GLOBAL\n"
                               "  Value : DINT;\n"
                               "END_VAR\n"
                               "FUNCTION_BLOCK Controller\n"
                               "VAR_INPUT\n"
                               "  Setpoint : REAL;\n"
                               "END_VAR\n"
                               "END_FUNCTION_BLOCK\n"
                               "PROGRAM Main\n"
                               "VAR\n"
                               "  Value : INT;\n"
                               "  Ctrl : Controller;\n"
                               "END_VAR\n"
                               "(* \xF0\x9F\x98\x80 *) Value := 1;\n"
                               "Ctrl.Setpoint := 2.0;\n"
                               "END_PROGRAM\n";

    st::LanguageDocument document(source);
    const std::size_t use_offset = source.find("Value := 1");
    const st::LanguageDefinition local = document.definition(position_at(source, use_offset));
    check(local.found, "local definition found");
    check(local.selection.start == position_of(source, "Value : INT"),
          "local shadows same-named GVL");

    const st::LanguageDefinition pin = document.definition(position_of(source, "Setpoint := 2.0"));
    check(pin.found, "user FB pin definition found");
    check(pin.selection.start == position_of(source, "Setpoint : REAL"),
          "user FB pin targets its declaration");

    const std::size_t dot = source.find("Ctrl.Setpoint") + 5U;
    const st::LanguageCompletionList pins = document.complete(position_at(source, dot));
    check(has_completion(pins, "Setpoint"), "instance dot completion has user FB pin");
    const st::LanguageCompletionItem *setpoint = completion(pins, "Setpoint");
    check(setpoint != nullptr && setpoint->detail.find("input") != std::string::npos,
          "user FB pin completion carries direction");
}

void standard_fb_pin_completion_and_hover()
{
    const std::string source = "PROGRAM Main\n"
                               "VAR\n"
                               "  Motion : MC_MoveAbsolute;\n"
                               "END_VAR\n"
                               "Motion.Execute := TRUE;\n"
                               "END_PROGRAM\n";
    st::LanguageDocument document(source);

    const std::size_t dot = source.find("Motion.Execute") + 7U;
    const st::LanguageCompletionList pins = document.complete(position_at(source, dot));
    check(has_completion(pins, "Execute"), "standard FB completion has input pin");
    check(has_completion(pins, "Done"), "standard FB completion has output pin");

    const st::LanguageHover hover = document.hover(position_of(source, "Execute := TRUE"));
    check(hover.found, "standard FB pin hover found");
    check(hover.contents.find("input") != std::string::npos,
          "standard FB pin hover carries direction");
    check(hover.contents.find("BOOL") != std::string::npos,
          "standard FB pin hover carries authoritative type");
}

void pou_cache_and_utf16_reprojection()
{
    const std::string original = "FUNCTION Scale : REAL\n"
                                 "VAR_INPUT x : REAL; END_VAR\n"
                                 "Scale := x;\n"
                                 "END_FUNCTION\n"
                                 "FUNCTION_BLOCK Filter\n"
                                 "VAR_INPUT x : REAL; END_VAR\n"
                                 "VAR y : REAL; END_VAR\n"
                                 "y := x;\n"
                                 "END_FUNCTION_BLOCK\n"
                                 "PROGRAM Main\n"
                                 "VAR value : REAL; END_VAR\n"
                                 "value := Scale(value);\n"
                                 "END_PROGRAM\n";

    st::LanguageDocument document(original);
    check(document.last_update().reparsed_pous == 3U, "initial index parses three POUs");
    check(document.last_update().reused_pous == 0U, "initial index reuses no POU");

    std::string changed = original;
    const std::size_t body = changed.find("y := x;");
    changed.replace(body, 7U, "y := x + 1.0;");
    const st::LanguageUpdateReport changed_report = document.update(changed);
    check(changed_report.reparsed_pous == 1U, "one changed POU is reparsed");
    check(changed_report.reused_pous == 2U, "two unchanged POUs are reused");

    const std::string shifted = "(* \xF0\x9F\x98\x80 *)\r\n" + changed;
    const st::LanguageUpdateReport shifted_report = document.update(shifted);
    check(shifted_report.reparsed_pous == 0U, "prefix edit reparses no unchanged POU");
    check(shifted_report.reused_pous == 3U, "prefix edit reuses all POU indexes");

    const std::size_t call = shifted.find("Scale(value)");
    const st::LanguageDefinition definition = document.definition(position_at(shifted, call));
    check(definition.found, "shifted POU definition found");
    check(definition.selection.start == position_of(shifted, "Scale : REAL"),
          "reused definition range is reprojected after emoji CRLF prefix");
}

void ambiguity_and_completion_capacity()
{
    const std::string ambiguous = "VAR_GLOBAL\n"
                                  "  Duplicate : INT;\n"
                                  "  Duplicate : DINT;\n"
                                  "END_VAR\n"
                                  "PROGRAM Main\n"
                                  "Duplicate := 1;\n"
                                  "END_PROGRAM\n";
    st::LanguageDocument ambiguous_document(ambiguous);
    check(!ambiguous_document.definition(position_of(ambiguous, "Duplicate := 1")).found,
          "same-priority duplicate definition is not guessed");

    std::string large = "PROGRAM Main\nVAR\n";
    for (std::size_t index = 0; index < st::LanguageDocument::max_completion_items + 8U; ++index)
    {
        large += "  value";
        large += std::to_string(index);
        large += " : INT;\n";
    }
    large += "END_VAR\nEND_PROGRAM\n";
    st::LanguageDocument large_document(large);
    const st::LanguageCompletionList completion =
        large_document.complete(position_of(large, "END_PROGRAM"));
    check(completion.is_incomplete, "completion overflow is explicit");
    check(completion.items.size() == st::LanguageDocument::max_completion_items,
          "completion result respects hard capacity");
}

} // namespace

int main()
{
    tolerant_diagnostics_and_authoritative_completion();
    scoped_definition_and_user_fb_pins();
    standard_fb_pin_completion_and_hover();
    pou_cache_and_utf16_reprojection();
    ambiguity_and_completion_capacity();

    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ST language document tests passed\n");
    return 0;
}
