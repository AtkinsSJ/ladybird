/*
 * Copyright (c) 2018-2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2020-2023, the SerenityOS developers.
 * Copyright (c) 2021-2024, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2022, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/CSSMediaRule.h>
#include <LibWeb/CSS/CSSRuleList.h>
#include <LibWeb/CSS/CSSStyleSheet.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/StyleValues/CSSRGB.h>

namespace Web {

CSS::CSSStyleSheet* parse_css_stylesheet(CSS::Parser::ParsingContext const& context, StringView css, Optional<URL::URL> location)
{
    if (css.is_empty()) {
        auto rule_list = CSS::CSSRuleList::create_empty(context.realm());
        auto media_list = CSS::MediaList::create(context.realm(), {});
        auto style_sheet = CSS::CSSStyleSheet::create(context.realm(), rule_list, media_list, location);
        style_sheet->set_source_text({});
        return style_sheet;
    }
    auto* style_sheet = CSS::Parser::Parser::create(context, css).parse_as_css_stylesheet(location);
    // FIXME: Avoid this copy
    style_sheet->set_source_text(MUST(String::from_utf8(css)));
    return style_sheet;
}

CSS::ElementInlineCSSStyleDeclaration* parse_css_style_attribute(CSS::Parser::ParsingContext const& context, StringView css, DOM::Element& element)
{
    if (css.is_empty())
        return CSS::ElementInlineCSSStyleDeclaration::create(element, {}, {});
    return CSS::Parser::Parser::create(context, css).parse_as_style_attribute(element);
}

RefPtr<CSS::CSSStyleValue> parse_css_value(CSS::Parser::ParsingContext const& context, StringView string, CSS::PropertyID property_id)
{
    if (string.is_empty())
        return nullptr;
    return CSS::Parser::Parser::create(context, string).parse_as_css_value(property_id);
}

CSS::CSSRule* parse_css_rule(CSS::Parser::ParsingContext const& context, StringView css_text)
{
    return CSS::Parser::Parser::create(context, css_text).parse_as_css_rule();
}

Optional<CSS::SelectorList> parse_selector(CSS::Parser::ParsingContext const& context, StringView selector_text)
{
    return CSS::Parser::Parser::create(context, selector_text).parse_as_selector();
}

Optional<CSS::Selector::PseudoElement> parse_pseudo_element_selector(CSS::Parser::ParsingContext const& context, StringView selector_text)
{
    return CSS::Parser::Parser::create(context, selector_text).parse_as_pseudo_element_selector();
}

RefPtr<CSS::MediaQuery> parse_media_query(CSS::Parser::ParsingContext const& context, StringView string)
{
    return CSS::Parser::Parser::create(context, string).parse_as_media_query();
}

Vector<NonnullRefPtr<CSS::MediaQuery>> parse_media_query_list(CSS::Parser::ParsingContext const& context, StringView string)
{
    return CSS::Parser::Parser::create(context, string).parse_as_media_query_list();
}

RefPtr<CSS::Supports> parse_css_supports(CSS::Parser::ParsingContext const& context, StringView string)
{
    if (string.is_empty())
        return {};
    return CSS::Parser::Parser::create(context, string).parse_as_supports();
}

Optional<CSS::StyleProperty> parse_css_supports_condition(CSS::Parser::ParsingContext const& context, StringView string)
{
    if (string.is_empty())
        return {};
    return CSS::Parser::Parser::create(context, string).parse_as_supports_condition();
}

// https://drafts.csswg.org/css-color/#parse-color
RefPtr<CSS::CSSColorValue> parse_a_css_color_value(CSS::Parser::ParsingContext const& context, StringView input, Optional<DOM::Element&> element)
{
    // 1. Parse input as a <color>. If the result is failure, return failure; otherwise, let color be the result.
    auto color = CSS::Parser::Parser::create(context, input).parse_as_css_value(CSS::PropertyID::Color);
    if (!color)
        return {};

    // 2. Let used color be the result of resolving color to a used color.
    //    If the value of other properties on the element a <color> is on is required to do the resolution (such as resolving a currentcolor or system color),
    //    use element if it was passed, or the initial values of the properties if not.
    RefPtr<CSS::CSSColorValue> used_color;
    if (color->is_color()) {
        // FIXME: We should resolve things like calc(), var(), and attr() here.
        used_color = color->as_color();
    } else if (color->is_keyword()) {
        auto layout_node = element.has_value() ? element->layout_node() : OptionalNone {};
        used_color = CSS::CSSRGB::create_from_color(color->to_color(layout_node));
    } else {
        dbgln("Unsupported type parsed in parse_a_css_color_value(): {}", color->to_string());
    }

    // 3. Return used color.
    return used_color;
}

}
