/*
 * Copyright (c) 2024-2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Parser.h>
#include <LibWeb/CSS/CSSStyleSheet.h>
#include <LibWeb/CSS/Fetch.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/HTML/SharedResourceRequest.h>

namespace Web::CSS {

// https://drafts.csswg.org/css-values-4/#fetch-a-style-resource
static WebIDL::ExceptionOr<GC::Ref<Fetch::Infrastructure::Request>> fetch_a_style_resource_impl(StyleResourceURL const& url_value, CSSRuleOrDeclaration css_rule_or_declaration, Fetch::Infrastructure::Request::Destination destination, CorsMode cors_mode)
{
    // To fetch a style resource from a url or <url> urlValue, given an CSS rule or a css declaration
    // cssRuleOrDeclaration, a string destination matching a RequestDestination, a "no-cors" or "cors" corsMode, and an
    // algorithm processResponse accepting a response and a null, failure or byte stream:
    auto& vm = css_rule_or_declaration.visit([](auto& it) -> JS::VM& { return it->vm(); });

    // 1. Let parsedUrl be the result of resolving urlValue given cssRuleOrDeclaration. If that failed, return.
    auto parsed_url = resolve_style_resource_url(url_value, css_rule_or_declaration);
    if (!parsed_url.has_value())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::URIError, "Failed to parse URL"sv };

    // 2. Let settingsObject be cssRuleOrDeclaration’s relevant settings object.
    auto& settings_object = HTML::relevant_settings_object(css_rule_or_declaration.visit([](auto& it) -> JS::Object& { return it; }));

    // 3. Let req be a new request whose url is parsedUrl, whose destination is destination, mode is corsMode,
    //    origin is settingsObject’s origin, credentials mode is "same-origin", use-url-credentials flag is set,
    //    client is settingsObject, and whose referrer is "client".
    auto request = Fetch::Infrastructure::Request::create(vm);
    request->set_url(parsed_url.release_value());
    request->set_destination(destination);
    request->set_mode(cors_mode == CorsMode::Cors ? Fetch::Infrastructure::Request::Mode::CORS : Fetch::Infrastructure::Request::Mode::NoCORS);
    request->set_origin(settings_object.origin());
    request->set_credentials_mode(Fetch::Infrastructure::Request::CredentialsMode::SameOrigin);
    request->set_use_url_credentials(true);
    request->set_client(&settings_object);
    request->set_referrer(settings_object.api_base_url());

    // 4. If corsMode is "no-cors", set req’s credentials mode to "include".
    if (cors_mode == CorsMode::NoCors)
        request->set_credentials_mode(Fetch::Infrastructure::Request::CredentialsMode::Include);

    // 5. Apply any URL request modifier steps that apply to this request.
    if (auto const* css_url = url_value.get_pointer<CSS::URL>())
        apply_request_modifiers_from_url_value(*css_url, request);

    // AD-HOC: The spec still uses a `sheet`, so try to get it, using the steps from https://drafts.csswg.org/css-values-4/#style-resource-base-url
    GC::Ptr sheet = [](CSSRuleOrDeclaration css_rule_or_declaration) {
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
        return sheet;
    }(css_rule_or_declaration);

    // 6. If req’s mode is "cors", and sheet is not null, then set req’s referrer to the style resource base URL given cssRuleOrDeclaration. [CSSOM]
    if (request->mode() == Fetch::Infrastructure::Request::Mode::CORS && sheet) {
        request->set_referrer(compute_style_resource_base_url(css_rule_or_declaration));
    }

    // 7. If sheet’s origin-clean flag is set, set req’s initiator type to "css". [CSSOM]
    if (sheet) {
        if (sheet->is_origin_clean())
            request->set_initiator_type(Fetch::Infrastructure::Request::InitiatorType::CSS);
    } else {
        // AD-HOC: If the resource is not associated with a stylesheet, we must still set an initiator type in order
        //         for this resource to be observable through a PerformanceObserver. WPT relies on this.
        request->set_initiator_type(Fetch::Infrastructure::Request::InitiatorType::Script);
    }

    // 8. Fetch req, with processResponseConsumeBody set to processResponse.
    // NB: Implemented by caller.
    return request;
}

// https://drafts.csswg.org/css-values-4/#fetch-a-style-resource
WebIDL::ExceptionOr<GC::Ref<Fetch::Infrastructure::FetchController>> fetch_a_style_resource(StyleResourceURL const& url_value, CSSRuleOrDeclaration css_rule_or_declaration, Fetch::Infrastructure::Request::Destination destination, CorsMode cors_mode, Fetch::Infrastructure::FetchAlgorithms::ProcessResponseConsumeBodyFunction process_response)
{
    auto request = TRY(fetch_a_style_resource_impl(url_value, css_rule_or_declaration, destination, cors_mode));
    auto& settings_object = HTML::relevant_settings_object(css_rule_or_declaration.visit([](auto& it) -> JS::Object& { return it; }));
    auto& vm = settings_object.vm();

    Fetch::Infrastructure::FetchAlgorithms::Input fetch_algorithms_input {};
    fetch_algorithms_input.process_response_consume_body = move(process_response);

    return Fetch::Fetching::fetch(settings_object.realm(), *request, Fetch::Infrastructure::FetchAlgorithms::create(vm, move(fetch_algorithms_input)));
}

// https://drafts.csswg.org/css-images-4/#fetch-an-external-image-for-a-stylesheet
GC::Ptr<HTML::SharedResourceRequest> fetch_an_external_image_for_a_stylesheet(StyleResourceURL const& url_value, GC::Ref<CSSStyleDeclaration> declaration)
{
    // To fetch an external image for a stylesheet, given a <url> url and a CSS style declaration declaration, fetch a
    // style resource given url, with ruleOrDeclaration being declaration, destination "image", CORS mode "no-cors",
    // and processResponse being the following steps given response res and null, failure or a byte stream byteStream:
    // If byteStream is a byte stream, load the image from the byte stream.

    // NB: We can't directly call fetch_a_style_resource() because we want to make use of SharedResourceRequest to
    //     deduplicate image requests.

    auto maybe_request = fetch_a_style_resource_impl(url_value, declaration, Fetch::Infrastructure::Request::Destination::Image, CorsMode::NoCors);
    if (maybe_request.is_error())
        return nullptr;
    auto& request = maybe_request.value();

    auto document = declaration->parent_rule()->parent_style_sheet()->owning_document();
    auto& realm = document->realm();

    auto shared_resource_request = HTML::SharedResourceRequest::get_or_create(realm, document->page(), request->url());
    shared_resource_request->add_callbacks(
        [document, weak_document = document->make_weak_ptr<DOM::Document>()] {
            if (!weak_document)
                return;

            if (auto navigable = document->navigable()) {
                // Once the image has loaded, we need to re-resolve CSS properties that depend on the image's dimensions.
                document->set_needs_to_resolve_paint_only_properties();

                // FIXME: Do less than a full repaint if possible?
                document->set_needs_display();
            }
        },
        nullptr);

    if (shared_resource_request->needs_fetching())
        shared_resource_request->fetch_resource(realm, *request);

    return shared_resource_request;
}

// https://drafts.csswg.org/css-values-5/#apply-request-modifiers-from-url-value
void apply_request_modifiers_from_url_value(URL const& url, GC::Ref<Fetch::Infrastructure::Request> request)
{
    // To apply request modifiers from URL value given a request req and a <url> url, call the URL request modifier
    // steps for url’s <request-url-modifier>s in sequence given req.
    for (auto const& request_url_modifier : url.request_url_modifiers())
        request_url_modifier.modify_request(request);
}

}
