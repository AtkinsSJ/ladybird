/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/CSSCounterStyleRulePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CSS/CSSCounterStyleRule.h>
#include <LibWeb/CSS/CSSDescriptors.h>
#include <LibWeb/CSS/Serialize.h>

namespace Web::CSS {

GC_DEFINE_ALLOCATOR(CSSCounterStyleRule);

GC::Ref<CSSCounterStyleRule> CSSCounterStyleRule::create(JS::Realm& realm, FlyString name, GC::Ref<CSSDescriptors> descriptors)
{
    return realm.create<CSSCounterStyleRule>(realm, name, descriptors);
}

CSSCounterStyleRule::CSSCounterStyleRule(JS::Realm& realm, FlyString name, GC::Ref<CSSDescriptors> descriptors)
    : CSSRule(realm, Type::CounterStyle)
    , m_name(move(name))
    , m_descriptors(descriptors)
{
}

CSSCounterStyleRule::~CSSCounterStyleRule() = default;

void CSSCounterStyleRule::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CSSCounterStyleRule);
    Base::initialize(realm);
}

bool CSSCounterStyleRule::is_valid() const
{
    // The @counter-style rule must have a valid symbols descriptor if the counter system is cyclic, numeric,
    // alphabetic, symbolic, or fixed, or a valid additive-symbols descriptor if the counter system is additive;
    // otherwise, the @counter-style does not define a counter style (but is still a valid at-rule).
    // https://drafts.csswg.org/css-counter-styles-3/#counter-style-symbols
    // FIXME: Implement this.

    return false;
}

String CSSCounterStyleRule::serialized() const
{
    // AD-HOC: There is no spec for the serialization of CSSCounterStyleRule.
    auto& descriptors = *m_descriptors;

    StringBuilder builder;

    builder.append("@counter-style "sv);
    builder.append(serialize_an_identifier(m_name));
    builder.append(" { "sv);

    if (descriptors.length() > 0) {
        builder.append(descriptors.serialized());
        builder.append(" "sv);
    }
    builder.append("}"sv);

    return builder.to_string_without_validation();
}

void CSSCounterStyleRule::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_descriptors);
}

// https://drafts.csswg.org/css-counter-styles-3/#dom-csscounterstylerule-name
void CSSCounterStyleRule::set_name(String name)
{
    // On setting the name attribute, run the following steps:

    // 1. If the value is an ASCII case-insensitive match for "none" or one of the non-overridable counter-style names,
    //    do nothing and return.
    for (auto& disallowed_name : non_overridable_custom_counter_style_names_and_none()) {
        if (name.equals_ignoring_ascii_case(disallowed_name))
            return;
    }

    // 2. If the value is an ASCII case-insensitive match for any of the predefined counter styles, lowercase it.
    // FIXME: We don't have any predefined ones yet!

    // 3. Replace the associated rule’s name with an identifier equal to the value.
    m_name = move(name);
}

String CSSCounterStyleRule::system() const
{
    return m_descriptors->get_property_value("system"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_system(StringView value)
{
    return m_descriptors->set_property("system"sv, value, ""sv);
}

String CSSCounterStyleRule::symbols() const
{
    return m_descriptors->get_property_value("symbols"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_symbols(StringView value)
{
    return m_descriptors->set_property("symbols"sv, value, ""sv);
}

String CSSCounterStyleRule::additive_symbols() const
{
    return m_descriptors->get_property_value("additive-symbols"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_additive_symbols(StringView value)
{
    return m_descriptors->set_property("additive-symbols"sv, value, ""sv);
}

String CSSCounterStyleRule::negative() const
{
    return m_descriptors->get_property_value("negative"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_negative(StringView value)
{
    return m_descriptors->set_property("negative"sv, value, ""sv);
}

String CSSCounterStyleRule::prefix() const
{
    return m_descriptors->get_property_value("prefix"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_prefix(StringView value)
{
    return m_descriptors->set_property("prefix"sv, value, ""sv);
}

String CSSCounterStyleRule::suffix() const
{
    return m_descriptors->get_property_value("suffix"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_suffix(StringView value)
{
    return m_descriptors->set_property("suffix"sv, value, ""sv);
}

String CSSCounterStyleRule::range() const
{
    return m_descriptors->get_property_value("range"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_range(StringView value)
{
    return m_descriptors->set_property("range"sv, value, ""sv);
}

String CSSCounterStyleRule::pad() const
{
    return m_descriptors->get_property_value("pad"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_pad(StringView value)
{
    return m_descriptors->set_property("pad"sv, value, ""sv);
}

String CSSCounterStyleRule::speak_as() const
{
    return m_descriptors->get_property_value("speak-as"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_speak_as(StringView value)
{
    return m_descriptors->set_property("speak-as"sv, value, ""sv);
}

String CSSCounterStyleRule::fallback() const
{
    return m_descriptors->get_property_value("fallback"sv);
}

WebIDL::ExceptionOr<void> CSSCounterStyleRule::set_fallback(StringView value)
{
    return m_descriptors->set_property("fallback"sv, value, ""sv);
}

// https://drafts.csswg.org/css-counter-styles-3/#non-overridable-counter-style-names
ReadonlySpan<StringView> non_overridable_custom_counter_style_names_and_none()
{
    static Vector const s_names {
        "none"sv,
        "decimal"sv,
        "disc"sv,
        "square"sv,
        "circle"sv,
        "disclosure-open"sv,
        "disclosure-closed"sv,
    };
    return s_names;
}

}
