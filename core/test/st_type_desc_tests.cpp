#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "st/type_desc.h"

namespace
{

using namespace plcopen::core::st;

int failures = 0;

void check(bool condition, const char *name)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

void builtin_ids_and_kinds()
{
    check(builtin::bool_ == 1 && builtin::sint == 2 && builtin::int_ == 3 && builtin::dint == 4 &&
              builtin::lint == 5 && builtin::usint == 6 && builtin::uint_ == 7 &&
              builtin::udint == 8 && builtin::ulint == 9 && builtin::real == 10 &&
              builtin::lreal == 11 && builtin::time == 12 && builtin::byte_ == 13 &&
              builtin::word == 14 && builtin::dword == 15 && builtin::lword == 16 &&
              builtin::date == 17 && builtin::tod == 18 && builtin::dt == 19,
          "stable builtin TypeId mapping");

    TypeTable table;
    const TypeDesc *dint = table.get(builtin::dint);
    const TypeDesc *date = table.get(builtin::date);
    const TypeDesc *tod = table.get(builtin::tod);
    const TypeDesc *dt = table.get(builtin::dt);
    check(dint != nullptr && dint->kind == TypeKind::elementary && dint->size == 4 &&
              dint->alignment == 4,
          "DINT descriptor");
    check(date != nullptr && date->kind == TypeKind::date && date->size == 4, "DATE descriptor");
    check(tod != nullptr && tod->kind == TypeKind::tod && tod->size == 8, "TOD descriptor");
    check(dt != nullptr && dt->kind == TypeKind::dt && dt->size == 8, "DT descriptor");
}

void enum_contract()
{
    TypeTable table;
    TypeId id = invalid_type_id;
    const std::vector<EnumItem> items = {
        {"Idle", IntegerValue::signed_value(0)},
        {"Ready", IntegerValue::signed_value(1)},
        {"AlsoReady", IntegerValue::signed_value(1)},
        {"Fault", IntegerValue::signed_value(-1)},
    };
    check(table.add_enum("State", builtin::int_, items, id) == TypeError::ok, "enum accepted");
    const TypeDesc *desc = table.get(id);
    check(desc != nullptr && desc->kind == TypeKind::enum_ && desc->size == 2 &&
              desc->alignment == 2,
          "enum inherits base ABI");

    IntegerValue value;
    check(table.enum_value(id, "aLsOrEaDy", value) == TypeError::ok &&
              value == IntegerValue::signed_value(1),
          "enum lookup is case-insensitive");
    const std::string *name = nullptr;
    check(table.enum_first_name(id, IntegerValue::signed_value(1), name) == TypeError::ok &&
              name != nullptr && *name == "Ready",
          "duplicate enum value returns first declaration");

    TypeId ignored = invalid_type_id;
    check(table.add_enum(
              "BadNames", builtin::int_,
              {{"Same", IntegerValue::signed_value(0)}, {"sAME", IntegerValue::signed_value(1)}},
              ignored) == TypeError::duplicate_member,
          "duplicate enum names rejected");
    check(table.add_enum("TooWide", builtin::sint, {{"V", IntegerValue::signed_value(128)}},
                         ignored) == TypeError::value_out_of_range,
          "enum value must fit base");
}

void subrange_contract()
{
    TypeTable table;
    TypeId signed_id = invalid_type_id;
    TypeId unsigned_id = invalid_type_id;
    check(table.add_subrange("Temperature", builtin::int_, IntegerValue::signed_value(-40),
                             IntegerValue::signed_value(125), signed_id) == TypeError::ok,
          "signed subrange");
    check(table.add_subrange("Percent", builtin::usint, IntegerValue::unsigned_value(0),
                             IntegerValue::unsigned_value(100), unsigned_id) == TypeError::ok,
          "unsigned subrange");
    const TypeDesc *desc = table.get(signed_id);
    check(desc != nullptr && desc->kind == TypeKind::subrange &&
              desc->subrange.base == builtin::int_ &&
              desc->subrange.lower == IntegerValue::signed_value(-40) &&
              desc->subrange.upper == IntegerValue::signed_value(125) && desc->size == 2,
          "subrange descriptor");

    TypeId ignored = invalid_type_id;
    check(table.add_subrange("Reverse", builtin::int_, IntegerValue::signed_value(2),
                             IntegerValue::signed_value(1), ignored) == TypeError::invalid_bounds,
          "reversed subrange rejected");
    check(table.add_subrange("WrongSign", builtin::uint_, IntegerValue::signed_value(0),
                             IntegerValue::signed_value(10),
                             ignored) == TypeError::integer_sign_mismatch,
          "subrange bound sign must match base");
}

void array_contract()
{
    TypeTable table;
    TypeId id = invalid_type_id;
    check(table.add_array("Grid", builtin::dint, {{1, 2}, {-1, 1}}, id) == TypeError::ok,
          "two-dimensional array");
    const TypeDesc *desc = table.get(id);
    check(desc != nullptr && desc->kind == TypeKind::array && desc->array.dimensions.size() == 2 &&
              desc->array.dimensions[0].extent == 2 && desc->array.dimensions[0].stride == 12 &&
              desc->array.dimensions[1].extent == 3 && desc->array.dimensions[1].stride == 4 &&
              desc->size == 24 && desc->alignment == 4,
          "row-major array layout");

    TypeId cube = invalid_type_id;
    check(table.add_array("Cube", builtin::usint, {{0, 1}, {0, 2}, {0, 3}}, cube) ==
                  TypeError::ok &&
              table.get(cube)->size == 24 && table.get(cube)->array.dimensions[0].stride == 12 &&
              table.get(cube)->array.dimensions[1].stride == 4 &&
              table.get(cube)->array.dimensions[2].stride == 1,
          "three-dimensional row-major array");

    TypeId ignored = invalid_type_id;
    check(table.add_array("NoDims", builtin::dint, {}, ignored) == TypeError::invalid_dimensions,
          "zero-dimensional array rejected");
    check(table.add_array("FourDims", builtin::dint, {{0, 0}, {0, 0}, {0, 0}, {0, 0}}, ignored) ==
              TypeError::invalid_dimensions,
          "four-dimensional array rejected");
    check(table.add_array("Backwards", builtin::dint, {{2, 1}}, ignored) ==
              TypeError::invalid_bounds,
          "array reversed bounds rejected");
    check(table.add_array("Overflow", builtin::lreal,
                          {{0, std::numeric_limits<std::int64_t>::max()}},
                          ignored) == TypeError::size_overflow,
          "array size overflow rejected");
}

void struct_contract()
{
    TypeTable table;
    TypeId record = invalid_type_id;
    check(table.add_struct(
              "Record",
              {{"flag", builtin::bool_}, {"count", builtin::dint}, {"small", builtin::sint}},
              record) == TypeError::ok,
          "struct accepted");
    const TypeDesc *desc = table.get(record);
    check(desc != nullptr && desc->kind == TypeKind::struct_ && desc->alignment == 4 &&
              desc->size == 12 && desc->structure.fields[0].offset == 0 &&
              desc->structure.fields[1].offset == 4 && desc->structure.fields[2].offset == 8,
          "host-independent natural struct layout");
    const StructField *count = nullptr;
    check(table.struct_field(record, "CoUnT", count) == TypeError::ok && count != nullptr &&
              count->offset == 4,
          "struct field lookup is case-insensitive");

    TypeId nested = invalid_type_id;
    check(table.add_struct("Nested", {{"record", record}, {"value", builtin::lreal}}, nested) ==
                  TypeError::ok &&
              table.get(nested)->alignment == 8 &&
              table.get(nested)->structure.fields[1].offset == 16 && table.get(nested)->size == 24,
          "nested struct layout capped at eight");

    const TypeId self = table.next_type_id();
    TypeId ignored = invalid_type_id;
    check(table.add_struct("Recursive", {{"self", self}}, ignored) == TypeError::recursive_type,
          "direct recursive struct rejected");
    check(table.add_struct("DuplicateFields", {{"x", builtin::int_}, {"X", builtin::dint}},
                           ignored) == TypeError::duplicate_member,
          "duplicate struct fields rejected");
}

void fixed_string_and_ref_contract()
{
    TypeTable table;
    TypeId string_id = invalid_type_id;
    TypeId wstring_id = invalid_type_id;
    TypeId ref_id = invalid_type_id;
    check(table.add_string("Label", 80, string_id) == TypeError::ok &&
              table.get(string_id)->kind == TypeKind::string &&
              table.get(string_id)->string.capacity == 80 && table.get(string_id)->size == 84 &&
              table.get(string_id)->alignment == 4,
          "fixed-capacity STRING layout");
    check(table.add_wstring("WideLabel", 80, wstring_id) == TypeError::ok &&
              table.get(wstring_id)->kind == TypeKind::wstring &&
              table.get(wstring_id)->size == 324 && table.get(wstring_id)->alignment == 4,
          "fixed-capacity WSTRING layout");
    check(table.add_ref("RecordRef", builtin::dint, ref_id) == TypeError::ok &&
              table.get(ref_id)->kind == TypeKind::ref &&
              table.get(ref_id)->reference.target == builtin::dint &&
              table.get(ref_id)->size == 8 && table.get(ref_id)->alignment == 8,
          "host-independent reference slot");

    TypeId ignored = invalid_type_id;
    check(table.add_string("Empty", 0, ignored) == TypeError::invalid_capacity,
          "zero string capacity rejected");
    check(table.add_wstring("Huge", std::numeric_limits<std::uint64_t>::max(), ignored) ==
              TypeError::size_overflow,
          "wstring capacity overflow rejected");
}

void load_domain_stability()
{
    TypeTable table;
    TypeId first = invalid_type_id;
    check(table.add_string("First", 8, first) == TypeError::ok, "first load-domain type");
    const TypeDesc *stable = table.get(first);
    TypeId found = invalid_type_id;
    check(table.find("fIRSt", found) == TypeError::ok && found == first,
          "type lookup is case-insensitive");
    TypeId ignored = invalid_type_id;
    check(table.add_string("fIRST", 8, ignored) == TypeError::duplicate_type,
          "type duplicate is case-insensitive");
    check(table.add_string("dInT", 8, ignored) == TypeError::duplicate_type,
          "builtin duplicate is case-insensitive");
    for (int i = 0; i < 64; ++i)
    {
        ignored = invalid_type_id;
        const std::string name = "Later" + std::to_string(i);
        check(table.add_string(name, 8, ignored) == TypeError::ok, "load-domain allocation");
    }
    check(stable == table.get(first) && stable != nullptr && stable->name == "First",
          "type descriptors stay stable while table grows");
}

void canonical_dump_contract()
{
    auto populate = [](TypeTable &table)
    {
        TypeId state = invalid_type_id;
        TypeId grid = invalid_type_id;
        TypeId payload = invalid_type_id;
        return table.add_enum("State", builtin::int_,
                              {{"Idle", IntegerValue::signed_value(0)},
                               {"Ready", IntegerValue::signed_value(1)},
                               {"AlsoReady", IntegerValue::signed_value(1)}},
                              state) == TypeError::ok &&
               table.add_array("Grid", state, {{0, 1}, {1, 3}}, grid) == TypeError::ok &&
               table.add_struct("Payload", {{"state", state}, {"grid", grid}}, payload) ==
                   TypeError::ok;
    };

    TypeTable first;
    TypeTable second;
    check(populate(first) && populate(second), "dump fixtures built");
    std::string a;
    std::string b;
    std::string again;
    check(first.canonical_dump(a) == TypeError::ok && second.canonical_dump(b) == TypeError::ok &&
              first.canonical_dump(again) == TypeError::ok,
          "canonical dump succeeds");
    check(a == b && a == again, "canonical dump deterministic");
    check(a.find("type 65536 enum State size=2 align=2 base=3") != std::string::npos &&
              a.find("item Ready=s:1\nitem AlsoReady=s:1") != std::string::npos &&
              a.find("dim 0:1 extent=2 stride=6") != std::string::npos &&
              a.find("field grid type=65537 offset=2") != std::string::npos,
          "canonical dump carries ABI details");
}

} // namespace

int main()
{
    builtin_ids_and_kinds();
    enum_contract();
    subrange_contract();
    array_contract();
    struct_contract();
    fixed_string_and_ref_contract();
    load_domain_stability();
    canonical_dump_contract();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("st_type_desc_tests: OK");
    return 0;
}
