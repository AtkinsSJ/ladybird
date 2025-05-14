/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/CSSStyleValue.h>
#include <LibWeb/CSS/Shorthands.h>
#include <LibWeb/CSS/StyleValues/StyleValueList.h>

namespace Web::CSS {

bool is_shorthand(AtRuleID at_rule, DescriptorID descriptor)
{
    if (at_rule == AtRuleID::Page && descriptor == DescriptorID::Margin)
        return true;

    return false;
}

void for_each_expanded_longhand(AtRuleID at_rule, DescriptorID descriptor, RefPtr<CSSStyleValue const> value, Function<void(DescriptorID, RefPtr<CSSStyleValue const>)> callback)
{
    if (at_rule == AtRuleID::Page && descriptor == DescriptorID::Margin) {
        if (!value) {
            callback(DescriptorID::MarginTop, nullptr);
            callback(DescriptorID::MarginRight, nullptr);
            callback(DescriptorID::MarginBottom, nullptr);
            callback(DescriptorID::MarginLeft, nullptr);
            return;
        }

        if (value->is_value_list()) {
            auto& values = value->as_value_list().values();
            if (values.size() == 4) {
                callback(DescriptorID::MarginTop, values[0]);
                callback(DescriptorID::MarginRight, values[1]);
                callback(DescriptorID::MarginBottom, values[2]);
                callback(DescriptorID::MarginLeft, values[3]);
            } else if (values.size() == 3) {
                callback(DescriptorID::MarginTop, values[0]);
                callback(DescriptorID::MarginRight, values[1]);
                callback(DescriptorID::MarginBottom, values[2]);
                callback(DescriptorID::MarginLeft, values[1]);
            } else if (values.size() == 2) {
                callback(DescriptorID::MarginTop, values[0]);
                callback(DescriptorID::MarginRight, values[1]);
                callback(DescriptorID::MarginBottom, values[0]);
                callback(DescriptorID::MarginLeft, values[1]);
            } else if (values.size() == 1) {
                callback(DescriptorID::MarginTop, values[0]);
                callback(DescriptorID::MarginRight, values[0]);
                callback(DescriptorID::MarginBottom, values[0]);
                callback(DescriptorID::MarginLeft, values[0]);
            }

        } else {
            callback(DescriptorID::MarginTop, *value);
            callback(DescriptorID::MarginRight, *value);
            callback(DescriptorID::MarginBottom, *value);
            callback(DescriptorID::MarginLeft, *value);
        }
    }
}

Vector<DescriptorID> shorthands_for_descriptor(AtRuleID at_rule, DescriptorID descriptor)
{
    if (at_rule == AtRuleID::Page && first_is_one_of(descriptor, DescriptorID::MarginTop, DescriptorID::MarginRight, DescriptorID::MarginBottom, DescriptorID::MarginLeft)) {
        return { DescriptorID::Margin };
    }

    return {};
}

RefPtr<CSSStyleValue> construct_shorthand(AtRuleID at_rule, DescriptorID shorthand, ReadonlySpan<Descriptor> longhands)
{
    if (at_rule == AtRuleID::Page && shorthand == DescriptorID::Margin) {
        auto top = get_property_internal(PropertyID::MarginTop);
        auto right = get_property_internal(PropertyID::MarginRight);
        auto bottom = get_property_internal(PropertyID::MarginBottom);
        auto left = get_property_internal(PropertyID::MarginLeft);
        return style_property_for_sided_shorthand(property_id, top, right, bottom, left);
    }

    return nullptr;
}

}
