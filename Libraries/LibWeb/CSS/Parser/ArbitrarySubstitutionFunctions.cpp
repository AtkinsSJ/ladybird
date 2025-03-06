/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/Parser/ArbitrarySubstitutionFunctions.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/StyleValues/UnresolvedStyleValue.h>
#include <LibWeb/DOM/Element.h>

namespace Web::CSS {

// https://drafts.csswg.org/css-values-5/#resolve-an-attr-function
SubstitutionResult resolve_an_attr_function(DOM::Element const& element, FlyString const& attribute_name, Optional<FlyString> syntax, Optional<Vector<UnresolvedValue>> const& given_fallback)
{
    // 1. Let el be the element that the style containing the attr() function is being applied to.
    //    Let attr name be the attribute name specified in the function.
    //    Let syntax be the <syntax> specified in the function, or null if it was omitted.
    //    Let fallback be the <declaration-value>? argument specified in the function, or the guaranteed-invalid value
    //    if it was omitted.
    auto fallback = given_fallback.value_or_lazy_evaluated([]() {
        return Vector<UnresolvedValue> { GuaranteedInvalidValue {} };
    });

    // 2. If there is no attribute named attr name on el, return the guaranteed-invalid value and fallback.
    //    Otherwise, let attr value be that attribute’s value.
    if (!element.has_attribute(attribute_name)) {
        return {
            .result = Vector<UnresolvedValue> { GuaranteedInvalidValue {} },
            .fallback = move(fallback),
        };
    }
    auto attribute_value = element.get_attribute_value(attribute_name);

    // 3. If syntax is null, return a CSS <string> whose value is attr value.
    if (!syntax.has_value()) {
        return {
            .result = Vector<UnresolvedValue> { Parser::ComponentValue { Parser::Token::create_string(attribute_value) } },
        };
    }

    // FIXME: 4. Parse with a attr value, with syntax and el. Return the result and fallback.
    // AD-HOC: For now we do manual parsing for `raw-string` or `<attr-unit>` based on how we previously did it, and
    //         not according to the current spec, which is still in flux.

    // raw-string: The entire attribute's value as a <string> token.
    if (syntax->equals_ignoring_ascii_case("raw-string"sv)) {
        return {
            .result = Vector<UnresolvedValue> { Parser::ComponentValue { Parser::Token::create_string(attribute_value) } },
        };
    }

    // <attr-unit>: Parse the attribute's value as a number, then produce a <dimension> token of that and the unit.
    auto unit = syntax.release_value();
    auto is_dimension_unit = unit == "%"sv
        || Angle::unit_from_name(unit).has_value()
        || Flex::unit_from_name(unit).has_value()
        || Frequency::unit_from_name(unit).has_value()
        || Length::unit_from_name(unit).has_value()
        || Resolution::unit_from_name(unit).has_value()
        || Time::unit_from_name(unit).has_value();

    if (is_dimension_unit) {
        auto component_value = Parser::Parser::create(Parser::ParsingParams { element.document() }, attribute_value, "utf-8"sv)
                                   .parse_as_component_value();
        if (component_value.has_value() && component_value->is(Parser::Token::Type::Number)) {
            return {
                .result = Vector<UnresolvedValue> { Parser::ComponentValue { Parser::Token::create_dimension(component_value->token().number_value(), move(unit)) } },
            };
        }
    }
    // Fall back on returning an invalid value.
    return {
        .result = Vector<UnresolvedValue> { GuaranteedInvalidValue {} },
        .fallback = move(fallback),
    };
}

// https://drafts.csswg.org/css-variables-2/#resolve-a-var-function
SubstitutionResult resolve_a_var_function(DOM::Element const& element, FlyString const& custom_property_name, Optional<Vector<UnresolvedValue>> const& given_fallback)
{
    // 1. Let result be the value of the custom property named by the function’s first argument, on the element the
    //    function’s property is being applied to.
    Optional<Vector<UnresolvedValue>> result;
    if (auto property = element.custom_properties({}).get(custom_property_name); property.has_value()) {
        auto& input_values = property->value->as_unresolved().values();
        Vector<UnresolvedValue> result_values;
        result_values.ensure_capacity(input_values.size());
        for (auto const& value : input_values)
            result_values.append(value);
        result = move(result_values);
    }

    // 2. Let fallback be the value of the function’s second argument, defaulting to the guaranteed-invalid value if it
    //    doesn’t have a second argument.
    auto fallback = given_fallback.value_or_lazy_evaluated([]() {
        return Vector<UnresolvedValue> { GuaranteedInvalidValue {} };
    });

    // FIXME: 3. If the custom property named by the var()’s first argument is animation-tainted, and the var() is being used
    //    in a property that is not animatable, set result to the guaranteed-invalid value.

    // 4. Return result and fallback.
    return {
        .result = result,
        .fallback = move(fallback),
    };
}

}
