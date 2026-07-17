#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plcopen::core::st
{

using TypeId = std::uint32_t;

inline constexpr TypeId invalid_type_id = 0;
inline constexpr TypeId first_load_type_id = 65536;

// ABI-v2 built-in ids are append-only. User-defined types live in a separate
// load-time domain so adding a future built-in cannot renumber a manifest.
namespace builtin
{
inline constexpr TypeId bool_ = 1;
inline constexpr TypeId sint = 2;
inline constexpr TypeId int_ = 3;
inline constexpr TypeId dint = 4;
inline constexpr TypeId lint = 5;
inline constexpr TypeId usint = 6;
inline constexpr TypeId uint_ = 7;
inline constexpr TypeId udint = 8;
inline constexpr TypeId ulint = 9;
inline constexpr TypeId real = 10;
inline constexpr TypeId lreal = 11;
inline constexpr TypeId time = 12;
inline constexpr TypeId byte_ = 13;
inline constexpr TypeId word = 14;
inline constexpr TypeId dword = 15;
inline constexpr TypeId lword = 16;
inline constexpr TypeId date = 17;
inline constexpr TypeId tod = 18;
inline constexpr TypeId dt = 19;
inline constexpr TypeId last = dt;
} // namespace builtin

enum class TypeKind : std::uint8_t
{
    elementary,
    enum_,
    subrange,
    array,
    struct_,
    string,
    wstring,
    date,
    tod,
    dt,
    ref,
};

enum class TypeError : std::uint8_t
{
    ok,
    invalid_type,
    invalid_name,
    duplicate_type,
    duplicate_member,
    invalid_integer_base,
    integer_sign_mismatch,
    value_out_of_range,
    invalid_bounds,
    invalid_dimensions,
    invalid_capacity,
    recursive_type,
    size_overflow,
    not_found,
};

enum class IntegerSign : std::uint8_t
{
    none,
    signed_,
    unsigned_,
};

struct IntegerValue
{
    IntegerSign sign = IntegerSign::signed_;
    std::int64_t signed_number = 0;
    std::uint64_t unsigned_number = 0;

    static constexpr IntegerValue signed_value(std::int64_t value)
    {
        return {IntegerSign::signed_, value, 0};
    }

    static constexpr IntegerValue unsigned_value(std::uint64_t value)
    {
        return {IntegerSign::unsigned_, 0, value};
    }

    constexpr std::int64_t as_signed() const { return signed_number; }

    constexpr std::uint64_t as_unsigned() const { return unsigned_number; }
};

constexpr bool operator==(IntegerValue left, IntegerValue right)
{
    return left.sign == right.sign &&
           (left.sign == IntegerSign::signed_ ? left.signed_number == right.signed_number
                                              : left.unsigned_number == right.unsigned_number);
}

constexpr bool operator!=(IntegerValue left, IntegerValue right) { return !(left == right); }

struct EnumItem
{
    std::string name;
    IntegerValue value;
};

struct SubrangeDesc
{
    TypeId base = invalid_type_id;
    IntegerValue lower;
    IntegerValue upper;
};

struct ArrayBound
{
    std::int64_t lower = 0;
    std::int64_t upper = 0;
};

struct ArrayDimension
{
    std::int64_t lower = 0;
    std::int64_t upper = 0;
    std::uint64_t extent = 0;
    std::uint64_t stride = 0; // bytes; last dimension is contiguous
};

struct ArrayDesc
{
    TypeId element = invalid_type_id;
    std::vector<ArrayDimension> dimensions;
};

struct StructFieldSpec
{
    std::string name;
    TypeId type = invalid_type_id;
};

struct StructField
{
    std::string name;
    TypeId type = invalid_type_id;
    std::uint64_t offset = 0;
};

struct StructDesc
{
    std::vector<StructField> fields;
};

struct StringDesc
{
    std::uint64_t capacity = 0; // bytes (STRING) or Unicode scalars (WSTRING)
};

struct RefDesc
{
    TypeId target = invalid_type_id;
};

struct TypeDesc
{
    TypeId id = invalid_type_id;
    TypeKind kind = TypeKind::elementary;
    std::string name;
    std::uint64_t size = 0;
    std::uint32_t alignment = 1;
    IntegerSign integer_sign = IntegerSign::none;
    std::uint8_t integer_width = 0;
    TypeId enum_base = invalid_type_id;
    std::vector<EnumItem> enum_items;
    SubrangeDesc subrange;
    ArrayDesc array;
    StructDesc structure;
    StringDesc string;
    RefDesc reference;
};

namespace type_desc_detail
{

inline TypeDesc make_builtin(TypeId id, TypeKind kind, const char *name, std::uint64_t size,
                             std::uint32_t alignment, IntegerSign sign = IntegerSign::none,
                             std::uint8_t width = 0)
{
    TypeDesc desc;
    desc.id = id;
    desc.kind = kind;
    desc.name = name;
    desc.size = size;
    desc.alignment = alignment;
    desc.integer_sign = sign;
    desc.integer_width = width;
    return desc;
}

inline const std::array<TypeDesc, builtin::last> &builtin_types()
{
    static const std::array<TypeDesc, builtin::last> types = {
        make_builtin(builtin::bool_, TypeKind::elementary, "BOOL", 1, 1),
        make_builtin(builtin::sint, TypeKind::elementary, "SINT", 1, 1, IntegerSign::signed_, 8),
        make_builtin(builtin::int_, TypeKind::elementary, "INT", 2, 2, IntegerSign::signed_, 16),
        make_builtin(builtin::dint, TypeKind::elementary, "DINT", 4, 4, IntegerSign::signed_, 32),
        make_builtin(builtin::lint, TypeKind::elementary, "LINT", 8, 8, IntegerSign::signed_, 64),
        make_builtin(builtin::usint, TypeKind::elementary, "USINT", 1, 1, IntegerSign::unsigned_,
                     8),
        make_builtin(builtin::uint_, TypeKind::elementary, "UINT", 2, 2, IntegerSign::unsigned_,
                     16),
        make_builtin(builtin::udint, TypeKind::elementary, "UDINT", 4, 4, IntegerSign::unsigned_,
                     32),
        make_builtin(builtin::ulint, TypeKind::elementary, "ULINT", 8, 8, IntegerSign::unsigned_,
                     64),
        make_builtin(builtin::real, TypeKind::elementary, "REAL", 4, 4),
        make_builtin(builtin::lreal, TypeKind::elementary, "LREAL", 8, 8),
        make_builtin(builtin::time, TypeKind::elementary, "TIME", 8, 8),
        make_builtin(builtin::byte_, TypeKind::elementary, "BYTE", 1, 1),
        make_builtin(builtin::word, TypeKind::elementary, "WORD", 2, 2),
        make_builtin(builtin::dword, TypeKind::elementary, "DWORD", 4, 4),
        make_builtin(builtin::lword, TypeKind::elementary, "LWORD", 8, 8),
        make_builtin(builtin::date, TypeKind::date, "DATE", 4, 4),
        make_builtin(builtin::tod, TypeKind::tod, "TOD", 8, 8),
        make_builtin(builtin::dt, TypeKind::dt, "DT", 8, 8),
    };
    return types;
}

inline bool identifier(std::string_view name)
{
    if (name.empty())
    {
        return false;
    }
    const auto is_alpha = [](unsigned char value)
    { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); };
    const auto is_identifier_char = [is_alpha](char character)
    {
        const auto value = static_cast<unsigned char>(character);
        return is_alpha(value) || (value >= '0' && value <= '9') || value == '_';
    };
    const auto first = static_cast<unsigned char>(name.front());
    if (!is_alpha(first) && first != '_')
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), is_identifier_char);
}

inline unsigned char ascii_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

inline bool ascii_equal(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (ascii_fold(static_cast<unsigned char>(left[i])) !=
            ascii_fold(static_cast<unsigned char>(right[i])))
        {
            return false;
        }
    }
    return true;
}

inline bool multiply(std::uint64_t left, std::uint64_t right, std::uint64_t &result)
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

inline bool align_up(std::uint64_t value, std::uint32_t alignment, std::uint64_t &result)
{
    const std::uint64_t mask = alignment - 1U;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask)
    {
        return false;
    }
    result = (value + mask) & ~mask;
    return true;
}

inline bool value_fits(const TypeDesc &base, IntegerValue value)
{
    if (base.integer_sign != value.sign || base.integer_width == 0)
    {
        return false;
    }
    if (base.integer_width == 64)
    {
        return true;
    }
    if (value.sign == IntegerSign::unsigned_)
    {
        return value.as_unsigned() < (std::uint64_t{1} << base.integer_width);
    }
    const std::int64_t signed_value = value.as_signed();
    const std::int64_t minimum = -(std::int64_t{1} << (base.integer_width - 1U));
    const std::int64_t maximum = (std::int64_t{1} << (base.integer_width - 1U)) - 1;
    return signed_value >= minimum && signed_value <= maximum;
}

inline bool less(IntegerValue left, IntegerValue right)
{
    if (left.sign == IntegerSign::signed_)
    {
        return left.as_signed() < right.as_signed();
    }
    return left.as_unsigned() < right.as_unsigned();
}

inline void append_u64(std::string &out, std::uint64_t value)
{
    char buffer[32];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, converted.ptr);
}

inline void append_i64(std::string &out, std::int64_t value)
{
    char buffer[32];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, converted.ptr);
}

inline void append_integer(std::string &out, IntegerValue value)
{
    if (value.sign == IntegerSign::signed_)
    {
        out += "s:";
        append_i64(out, value.as_signed());
    }
    else
    {
        out += "u:";
        append_u64(out, value.as_unsigned());
    }
}

inline const char *kind_name(TypeKind kind)
{
    switch (kind)
    {
    case TypeKind::elementary:
        return "elementary";
    case TypeKind::enum_:
        return "enum";
    case TypeKind::subrange:
        return "subrange";
    case TypeKind::array:
        return "array";
    case TypeKind::struct_:
        return "struct";
    case TypeKind::string:
        return "string";
    case TypeKind::wstring:
        return "wstring";
    case TypeKind::date:
        return "date";
    case TypeKind::tod:
        return "tod";
    case TypeKind::dt:
        return "dt";
    case TypeKind::ref:
        return "ref";
    }
    return "?";
}

} // namespace type_desc_detail

class TypeTable
{
  public:
    TypeId next_type_id() const noexcept
    {
        if (types_.size() > std::numeric_limits<TypeId>::max() - first_load_type_id)
        {
            return invalid_type_id;
        }
        return first_load_type_id + static_cast<TypeId>(types_.size());
    }

    const TypeDesc *get(TypeId id) const noexcept
    {
        if (id >= builtin::bool_ && id <= builtin::last)
        {
            return &type_desc_detail::builtin_types()[id - 1U];
        }
        if (id < first_load_type_id)
        {
            return nullptr;
        }
        const std::uint64_t index = id - first_load_type_id;
        return index < types_.size() ? &types_[static_cast<std::size_t>(index)] : nullptr;
    }

    TypeError find(std::string_view name, TypeId &out) const noexcept
    {
        out = invalid_type_id;
        for (TypeId id = builtin::bool_; id <= builtin::last; ++id)
        {
            if (type_desc_detail::ascii_equal(get(id)->name, name))
            {
                out = id;
                return TypeError::ok;
            }
        }
        for (const TypeDesc &desc : types_)
        {
            if (type_desc_detail::ascii_equal(desc.name, name))
            {
                out = desc.id;
                return TypeError::ok;
            }
        }
        return TypeError::not_found;
    }

    TypeError add_enum(std::string_view name, TypeId base, const std::vector<EnumItem> &items,
                       TypeId &out)
    {
        out = invalid_type_id;
        const TypeDesc *base_desc = get(base);
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        if (!integer_base(base_desc))
        {
            return TypeError::invalid_integer_base;
        }
        TypeDesc desc;
        desc.kind = TypeKind::enum_;
        desc.name.assign(name.data(), name.size());
        desc.size = base_desc->size;
        desc.alignment = base_desc->alignment;
        desc.integer_sign = base_desc->integer_sign;
        desc.integer_width = base_desc->integer_width;
        desc.enum_base = base;
        desc.enum_items = items;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (!type_desc_detail::identifier(items[i].name))
            {
                return TypeError::invalid_name;
            }
            if (!type_desc_detail::value_fits(*base_desc, items[i].value))
            {
                return items[i].value.sign == base_desc->integer_sign
                           ? TypeError::value_out_of_range
                           : TypeError::integer_sign_mismatch;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (type_desc_detail::ascii_equal(items[j].name, items[i].name))
                {
                    return TypeError::duplicate_member;
                }
            }
        }
        return commit(std::move(desc), out);
    }

    TypeError add_subrange(std::string_view name, TypeId base, IntegerValue lower,
                           IntegerValue upper, TypeId &out)
    {
        out = invalid_type_id;
        const TypeDesc *base_desc = get(base);
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        if (!integer_base(base_desc))
        {
            return TypeError::invalid_integer_base;
        }
        if (lower.sign != base_desc->integer_sign || upper.sign != base_desc->integer_sign)
        {
            return TypeError::integer_sign_mismatch;
        }
        if (!type_desc_detail::value_fits(*base_desc, lower) ||
            !type_desc_detail::value_fits(*base_desc, upper))
        {
            return TypeError::value_out_of_range;
        }
        if (type_desc_detail::less(upper, lower))
        {
            return TypeError::invalid_bounds;
        }
        TypeDesc desc;
        desc.kind = TypeKind::subrange;
        desc.name.assign(name.data(), name.size());
        desc.size = base_desc->size;
        desc.alignment = base_desc->alignment;
        desc.integer_sign = base_desc->integer_sign;
        desc.integer_width = base_desc->integer_width;
        desc.subrange = {base, lower, upper};
        return commit(std::move(desc), out);
    }

    TypeError add_array(std::string_view name, TypeId element,
                        const std::vector<ArrayBound> &bounds, TypeId &out)
    {
        out = invalid_type_id;
        const TypeDesc *element_desc = get(element);
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        if (element_desc == nullptr)
        {
            return element == next_type_id() ? TypeError::recursive_type : TypeError::invalid_type;
        }
        if (bounds.empty() || bounds.size() > 3)
        {
            return TypeError::invalid_dimensions;
        }
        TypeDesc desc;
        desc.kind = TypeKind::array;
        desc.name.assign(name.data(), name.size());
        desc.alignment = element_desc->alignment;
        desc.array.element = element;
        desc.array.dimensions.resize(bounds.size());
        std::uint64_t stride = element_desc->size;
        for (std::size_t i = bounds.size(); i-- > 0;)
        {
            if (bounds[i].upper < bounds[i].lower)
            {
                return TypeError::invalid_bounds;
            }
            const std::uint64_t extent = static_cast<std::uint64_t>(bounds[i].upper) -
                                         static_cast<std::uint64_t>(bounds[i].lower) + 1U;
            if (extent == 0)
            {
                return TypeError::size_overflow;
            }
            desc.array.dimensions[i] = {bounds[i].lower, bounds[i].upper, extent, stride};
            if (!type_desc_detail::multiply(stride, extent, stride))
            {
                return TypeError::size_overflow;
            }
        }
        desc.size = stride;
        return commit(std::move(desc), out);
    }

    TypeError add_struct(std::string_view name, const std::vector<StructFieldSpec> &fields,
                         TypeId &out)
    {
        out = invalid_type_id;
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        TypeDesc desc;
        desc.kind = TypeKind::struct_;
        desc.name.assign(name.data(), name.size());
        desc.alignment = 1;
        desc.structure.fields.reserve(fields.size());
        std::uint64_t offset = 0;
        const TypeId self = next_type_id();
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (!type_desc_detail::identifier(fields[i].name))
            {
                return TypeError::invalid_name;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (type_desc_detail::ascii_equal(fields[j].name, fields[i].name))
                {
                    return TypeError::duplicate_member;
                }
            }
            if (fields[i].type == self)
            {
                return TypeError::recursive_type;
            }
            const TypeDesc *field = get(fields[i].type);
            if (field == nullptr)
            {
                return TypeError::invalid_type;
            }
            const std::uint32_t alignment = field->alignment > 8 ? 8 : field->alignment;
            if (!type_desc_detail::align_up(offset, alignment, offset))
            {
                return TypeError::size_overflow;
            }
            desc.structure.fields.push_back({fields[i].name, fields[i].type, offset});
            if (field->size > std::numeric_limits<std::uint64_t>::max() - offset)
            {
                return TypeError::size_overflow;
            }
            offset += field->size;
            if (alignment > desc.alignment)
            {
                desc.alignment = alignment;
            }
        }
        if (!type_desc_detail::align_up(offset, desc.alignment, desc.size))
        {
            return TypeError::size_overflow;
        }
        return commit(std::move(desc), out);
    }

    TypeError add_string(std::string_view name, std::uint64_t capacity, TypeId &out)
    {
        return add_string_impl(name, capacity, false, out);
    }

    TypeError add_wstring(std::string_view name, std::uint64_t capacity, TypeId &out)
    {
        return add_string_impl(name, capacity, true, out);
    }

    TypeError add_ref(std::string_view name, TypeId target, TypeId &out)
    {
        out = invalid_type_id;
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        if (get(target) == nullptr)
        {
            return TypeError::invalid_type;
        }
        TypeDesc desc;
        desc.kind = TypeKind::ref;
        desc.name.assign(name.data(), name.size());
        desc.size = 8;
        desc.alignment = 8;
        desc.reference.target = target;
        return commit(std::move(desc), out);
    }

    TypeError add_opaque_ref(std::string_view name, TypeId &out)
    {
        out = invalid_type_id;
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        TypeDesc desc;
        desc.kind = TypeKind::ref;
        desc.name.assign(name.data(), name.size());
        desc.size = 8;
        desc.alignment = 8;
        desc.reference.target = invalid_type_id;
        return commit(std::move(desc), out);
    }

    TypeError enum_value(TypeId type, std::string_view name, IntegerValue &out) const noexcept
    {
        const TypeDesc *desc = get(type);
        if (desc == nullptr || desc->kind != TypeKind::enum_)
        {
            return TypeError::invalid_type;
        }
        for (const EnumItem &item : desc->enum_items)
        {
            if (type_desc_detail::ascii_equal(item.name, name))
            {
                out = item.value;
                return TypeError::ok;
            }
        }
        return TypeError::not_found;
    }

    TypeError enum_first_name(TypeId type, IntegerValue value,
                              const std::string *&out) const noexcept
    {
        out = nullptr;
        const TypeDesc *desc = get(type);
        if (desc == nullptr || desc->kind != TypeKind::enum_)
        {
            return TypeError::invalid_type;
        }
        for (const EnumItem &item : desc->enum_items)
        {
            if (item.value == value)
            {
                out = &item.name;
                return TypeError::ok;
            }
        }
        return TypeError::not_found;
    }

    TypeError struct_field(TypeId type, std::string_view name,
                           const StructField *&out) const noexcept
    {
        out = nullptr;
        const TypeDesc *desc = get(type);
        if (desc == nullptr || desc->kind != TypeKind::struct_)
        {
            return TypeError::invalid_type;
        }
        for (const StructField &field : desc->structure.fields)
        {
            if (type_desc_detail::ascii_equal(field.name, name))
            {
                out = &field;
                return TypeError::ok;
            }
        }
        return TypeError::not_found;
    }

    TypeError canonical_dump(std::string &out) const
    {
        std::string dump;
        for (TypeId id = builtin::bool_; id <= builtin::last; ++id)
        {
            append_dump(*get(id), dump);
        }
        for (const TypeDesc &desc : types_)
        {
            append_dump(desc, dump);
        }
        out.swap(dump);
        return TypeError::ok;
    }

  private:
    static bool integer_base(const TypeDesc *desc)
    {
        return desc != nullptr && desc->kind == TypeKind::elementary &&
               desc->integer_sign != IntegerSign::none;
    }

    TypeError validate_new_name(std::string_view name) const noexcept
    {
        if (!type_desc_detail::identifier(name))
        {
            return TypeError::invalid_name;
        }
        for (TypeId id = builtin::bool_; id <= builtin::last; ++id)
        {
            if (type_desc_detail::ascii_equal(get(id)->name, name))
            {
                return TypeError::duplicate_type;
            }
        }
        for (const TypeDesc &desc : types_)
        {
            if (type_desc_detail::ascii_equal(desc.name, name))
            {
                return TypeError::duplicate_type;
            }
        }
        return TypeError::ok;
    }

    TypeError commit(TypeDesc &&desc, TypeId &out)
    {
        const TypeId id = next_type_id();
        if (id == invalid_type_id)
        {
            return TypeError::size_overflow;
        }
        desc.id = id;
        types_.push_back(std::move(desc));
        out = id;
        return TypeError::ok;
    }

    TypeError add_string_impl(std::string_view name, std::uint64_t capacity, bool wide, TypeId &out)
    {
        out = invalid_type_id;
        TypeError error = validate_new_name(name);
        if (error != TypeError::ok)
        {
            return error;
        }
        if (capacity == 0)
        {
            return TypeError::invalid_capacity;
        }
        std::uint64_t payload = 0;
        const std::uint64_t width = wide ? 4U : 1U;
        if (capacity > std::numeric_limits<std::uint32_t>::max() ||
            !type_desc_detail::multiply(capacity, width, payload) ||
            payload > std::numeric_limits<std::uint64_t>::max() - 4U)
        {
            return TypeError::size_overflow;
        }
        TypeDesc desc;
        desc.kind = wide ? TypeKind::wstring : TypeKind::string;
        desc.name.assign(name.data(), name.size());
        desc.size = 4U + payload;
        desc.alignment = 4;
        desc.string.capacity = capacity;
        return commit(std::move(desc), out);
    }

    static void append_dump(const TypeDesc &desc, std::string &out)
    {
        using namespace type_desc_detail;
        out += "type ";
        append_u64(out, desc.id);
        out += ' ';
        out += kind_name(desc.kind);
        out += ' ';
        out += desc.name;
        out += " size=";
        append_u64(out, desc.size);
        out += " align=";
        append_u64(out, desc.alignment);
        switch (desc.kind)
        {
        case TypeKind::enum_:
            out += " base=";
            append_u64(out, desc.enum_base);
            out += '\n';
            for (const EnumItem &item : desc.enum_items)
            {
                out += "item ";
                out += item.name;
                out += '=';
                append_integer(out, item.value);
                out += '\n';
            }
            return;
        case TypeKind::subrange:
            out += " base=";
            append_u64(out, desc.subrange.base);
            out += " lower=";
            append_integer(out, desc.subrange.lower);
            out += " upper=";
            append_integer(out, desc.subrange.upper);
            break;
        case TypeKind::array:
            out += " element=";
            append_u64(out, desc.array.element);
            out += '\n';
            for (const ArrayDimension &dimension : desc.array.dimensions)
            {
                out += "dim ";
                append_i64(out, dimension.lower);
                out += ':';
                append_i64(out, dimension.upper);
                out += " extent=";
                append_u64(out, dimension.extent);
                out += " stride=";
                append_u64(out, dimension.stride);
                out += '\n';
            }
            return;
        case TypeKind::struct_:
            out += '\n';
            for (const StructField &field : desc.structure.fields)
            {
                out += "field ";
                out += field.name;
                out += " type=";
                append_u64(out, field.type);
                out += " offset=";
                append_u64(out, field.offset);
                out += '\n';
            }
            return;
        case TypeKind::string:
        case TypeKind::wstring:
            out += " capacity=";
            append_u64(out, desc.string.capacity);
            break;
        case TypeKind::ref:
            out += " target=";
            append_u64(out, desc.reference.target);
            break;
        default:
            break;
        }
        out += '\n';
    }

    std::deque<TypeDesc> types_;
};

} // namespace plcopen::core::st

#include "st/generated/st_binding_types.h"
