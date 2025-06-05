/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Parser.h>
#include <LibWeb/CSS/CSSRule.h>
#include <LibWeb/CSS/CSSStyleDeclaration.h>
#include <LibWeb/CSS/CSSStyleSheet.h>
#include <LibWeb/CSS/Serialize.h>
#include <LibWeb/CSS/URL.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>

namespace Web::CSS {

URL::URL(String url, Vector<RequestURLModifier> request_url_modifiers)
    : m_url(move(url))
    , m_request_url_modifiers(move(request_url_modifiers))
{
}

// https://drafts.csswg.org/cssom-1/#serialize-a-url
String URL::to_string() const
{
    // To serialize a URL means to create a string represented by "url(", followed by the serialization of the URL as a string, followed by ")".
    StringBuilder builder;
    builder.append("url("sv);
    serialize_a_string(builder, m_url);

    // AD-HOC: Serialize the RequestURLModifiers
    // Spec issue: https://github.com/w3c/csswg-drafts/issues/12057
    for (auto const& modifier : m_request_url_modifiers)
        builder.appendff(" {}", modifier.to_string());

    builder.append(')');

    return builder.to_string_without_validation();
}

bool URL::operator==(URL const&) const = default;

RequestURLModifier RequestURLModifier::create_cross_origin(CrossOriginModifierValue value)
{
    return RequestURLModifier { Type::CrossOrigin, value };
}

RequestURLModifier RequestURLModifier::create_integrity(FlyString value)
{
    return RequestURLModifier { Type::Integrity, move(value) };
}

RequestURLModifier RequestURLModifier::create_referrer_policy(ReferrerPolicyModifierValue value)
{
    return RequestURLModifier { Type::ReferrerPolicy, value };
}

RequestURLModifier::RequestURLModifier(Type type, Value value)
    : m_type(type)
    , m_value(move(value))
{
}

void RequestURLModifier::modify_request(GC::Ref<Fetch::Infrastructure::Request> request) const
{
    switch (m_type) {
    case Type::CrossOrigin: {
        // https://drafts.csswg.org/css-values-5/#typedef-request-url-modifier-crossorigin-modifier
        // The URL request modifier steps for this modifier given request req are:

        // 1. Set req’s mode to "cors".
        request->set_mode(Fetch::Infrastructure::Request::Mode::CORS);

        // 2. If the given value is use-credentials, set req’s credentials mode to "include".
        if (m_value == CrossOriginModifierValue::UseCredentials) {
            request->set_credentials_mode(Fetch::Infrastructure::Request::CredentialsMode::Include);
        }
        // 3. Otherwise, set req’s credentials mode to "same-origin".
        else {
            request->set_credentials_mode(Fetch::Infrastructure::Request::CredentialsMode::SameOrigin);
        }
        break;
    }
    case Type::Integrity: {
        // https://drafts.csswg.org/css-values-5/#typedef-request-url-modifier-integrity-modifier

        // The URL request modifier steps for this modifier given request req are to set request’s integrity metadata to
        // the given <string>.
        request->set_integrity_metadata(m_value.get<FlyString>().to_string());
        break;
    }
    case Type::ReferrerPolicy: {
        // https://drafts.csswg.org/css-values-5/#typedef-request-url-modifier-referrerpolicy-modifier
        // The URL request modifier steps for this modifier given request req are to set request’s referrer policy to the
        // ReferrerPolicy that matches the given value.
        auto referrer_policy = [](ReferrerPolicyModifierValue value) {
            switch (value) {
            case ReferrerPolicyModifierValue::NoReferrer:
                return ReferrerPolicy::ReferrerPolicy::NoReferrer;
            case ReferrerPolicyModifierValue::NoReferrerWhenDowngrade:
                return ReferrerPolicy::ReferrerPolicy::NoReferrerWhenDowngrade;
            case ReferrerPolicyModifierValue::SameOrigin:
                return ReferrerPolicy::ReferrerPolicy::SameOrigin;
            case ReferrerPolicyModifierValue::Origin:
                return ReferrerPolicy::ReferrerPolicy::Origin;
            case ReferrerPolicyModifierValue::StrictOrigin:
                return ReferrerPolicy::ReferrerPolicy::StrictOrigin;
            case ReferrerPolicyModifierValue::OriginWhenCrossOrigin:
                return ReferrerPolicy::ReferrerPolicy::OriginWhenCrossOrigin;
            case ReferrerPolicyModifierValue::StrictOriginWhenCrossOrigin:
                return ReferrerPolicy::ReferrerPolicy::StrictOriginWhenCrossOrigin;
            case ReferrerPolicyModifierValue::UnsafeUrl:
                return ReferrerPolicy::ReferrerPolicy::UnsafeURL;
            }
            VERIFY_NOT_REACHED();
        }(m_value.get<ReferrerPolicyModifierValue>());
        request->set_referrer_policy(referrer_policy);
        break;
    }
    }
}

String RequestURLModifier::to_string() const
{
    switch (m_type) {
    case Type::CrossOrigin:
        return MUST(String::formatted("crossorigin({})", CSS::to_string(m_value.get<CrossOriginModifierValue>())));
    case Type::Integrity:
        return MUST(String::formatted("integrity({})", serialize_a_string(m_value.get<FlyString>())));
    case Type::ReferrerPolicy:
        return MUST(String::formatted("referrerpolicy({})", CSS::to_string(m_value.get<ReferrerPolicyModifierValue>())));
    }
    VERIFY_NOT_REACHED();
}

bool RequestURLModifier::operator==(RequestURLModifier const&) const = default;

// https://drafts.csswg.org/css-values-4/#style-resource-base-url
::URL::URL compute_style_resource_base_url(CSSRuleOrDeclaration css_rule_or_declaration)
{
    // To compute the style resource base URL for a CSS rule or a CSS declaration cssRuleOrDeclaration:

    // 1. Let sheet be null.
    GC::Ptr<CSSStyleSheet> sheet = nullptr;

    // 2. If cssRuleOrDeclaration is a CSSStyleDeclaration whose parentRule is not null, set cssRuleOrDeclaration to cssRuleOrDeclaration’s parentRule.
    if (auto const* maybe_style_declaration = css_rule_or_declaration.get_pointer<GC::Ref<CSSStyleDeclaration>>()) {
        auto style_declaration = *maybe_style_declaration;
        if (style_declaration->parent_rule())
            css_rule_or_declaration = GC::Ref { *style_declaration->parent_rule() };
    }

    // 3. If cssRuleOrDeclaration is a CSSRule, set sheet to cssRuleOrDeclaration’s parentStyleSheet.
    if (auto const* maybe_css_rule = css_rule_or_declaration.get_pointer<GC::Ref<CSSRule>>()) {
        sheet = (*maybe_css_rule)->parent_style_sheet();
    }

    // 4. If sheet is not null:
    if (sheet) {
        // 1. If sheet’s stylesheet base URL is not null, return sheet’s stylesheet base URL.
        if (sheet->base_url().has_value())
            return sheet->base_url().value();

        // 2. If sheet’s location is not null, return sheet’s location.
        if (sheet->location().has_value())
            return sheet->location().value();
    }

    // 5. Return cssRuleOrDeclaration’s relevant settings object’s API base URL.
    auto& relevant_settings_object = HTML::relevant_settings_object(css_rule_or_declaration.visit([](auto& it) -> JS::Object& { return it; }));
    return relevant_settings_object.api_base_url();
}

// https://drafts.csswg.org/css-values-4/#resolve-a-style-resource-url
Optional<::URL::URL> resolve_style_resource_url(StyleResourceURL url_value, CSSRuleOrDeclaration css_rule_or_declaration)
{
    // To resolve a style resource URL from a url or <url> urlValue, and a CSS rule or a CSS declaration cssRuleOrDeclaration:

    // 1. Let base be the style resource base URL given cssRuleOrDeclaration.
    auto base = compute_style_resource_base_url(css_rule_or_declaration);

    // 2. Return the result of the URL parser steps with urlValue’s url and base.
    auto url_string = url_value.visit(
        [](::URL::URL const& url) { return url.to_string(); },
        [](CSS::URL const& url) { return url.url(); });
    return ::URL::Parser::basic_parse(url_string, base);
}

}
