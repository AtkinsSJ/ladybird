/*
 * Copyright (c) 2023-2024, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibWeb/CSS/PropertyID.h>

namespace Web::CSS {

// https://drafts.css-houdini.org/css-typed-om-1/#numeric-typing
// FIXME: Add IDL for this.
class CSSNumericType {
public:
    // https://drafts.css-houdini.org/css-typed-om-1/#cssnumericvalue-base-type
    enum class BaseType {
        Length,
        Angle,
        Time,
        Frequency,
        Resolution,
        Flex,
        Percent,
        __Count,
    };

    static Optional<BaseType> base_type_from_value_type(ValueType);
    static constexpr StringView base_type_name(BaseType base_type)
    {
        switch (base_type) {
        case BaseType::Length:
            return "length"sv;
        case BaseType::Angle:
            return "angle"sv;
        case BaseType::Time:
            return "time"sv;
        case BaseType::Frequency:
            return "frequency"sv;
        case BaseType::Resolution:
            return "resolution"sv;
        case BaseType::Flex:
            return "flex"sv;
        case BaseType::Percent:
            return "percent"sv;
        case BaseType::__Count:
            break;
        }
        VERIFY_NOT_REACHED();
    }

    static Optional<CSSNumericType> create_from_unit(StringView unit);
    CSSNumericType() = default;
    CSSNumericType(BaseType type, i32 power)
    {
        set_exponent(type, power);
    }

    Optional<CSSNumericType> added_to(CSSNumericType const& other) const;
    Optional<CSSNumericType> multiplied_by(CSSNumericType const& other) const;
    CSSNumericType inverted() const;

    bool has_consistent_type_with(CSSNumericType const& other) const;
    Optional<CSSNumericType> consistent_type(CSSNumericType const& other) const;
    Optional<CSSNumericType> made_consistent_with(CSSNumericType const& other) const;

    bool matches_angle() const { return matches_dimension(BaseType::Angle); }
    bool matches_angle_percentage() const { return matches_dimension_percentage(BaseType::Angle); }
    bool matches_flex() const { return matches_dimension(BaseType::Flex); }
    bool matches_frequency() const { return matches_dimension(BaseType::Frequency); }
    bool matches_frequency_percentage() const { return matches_dimension_percentage(BaseType::Frequency); }
    bool matches_length() const { return matches_dimension(BaseType::Length); }
    bool matches_length_percentage() const { return matches_dimension_percentage(BaseType::Length); }
    bool matches_number() const;
    bool matches_number_percentage() const;
    bool matches_percentage() const;
    bool matches_resolution() const { return matches_dimension(BaseType::Resolution); }
    bool matches_resolution_percentage() const { return matches_dimension_percentage(BaseType::Resolution); }
    bool matches_time() const { return matches_dimension(BaseType::Time); }
    bool matches_time_percentage() const { return matches_dimension_percentage(BaseType::Time); }

    bool matches_dimension() const;

    Optional<i32> const& exponent(BaseType type) const { return m_type_exponents[to_underlying(type)]; }
    void set_exponent(BaseType type, i32 exponent) { m_type_exponents[to_underlying(type)] = exponent; }

    Optional<BaseType> const& percent_hint() const { return m_percent_hint; }
    void set_percent_hint(Optional<BaseType> hint) { m_percent_hint = hint; }
    void apply_percent_hint(BaseType hint);

    bool operator==(CSSNumericType const& other) const = default;

    String dump() const;

private:
    bool contains_all_the_non_zero_entries_of_other_with_the_same_value(CSSNumericType const& other) const;
    bool contains_a_key_other_than_percent_with_a_non_zero_value() const;
    enum class SkipIfAlreadyPresent {
        No,
        Yes,
    };
    void copy_all_entries_from(CSSNumericType const& other, SkipIfAlreadyPresent);

    bool matches_dimension(BaseType) const;
    bool matches_dimension_percentage(BaseType) const;

    Array<Optional<i32>, to_underlying(BaseType::__Count)> m_type_exponents;
    Optional<BaseType> m_percent_hint;
};

}

template<>
struct AK::Formatter<Web::CSS::CSSNumericType> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Web::CSS::CSSNumericType const& value)
    {
        return Formatter<StringView>::format(builder, value.dump());
    }
};
