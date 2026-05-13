/*
 * Copyright (c) 2021-2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/MediaQuery.h>
#include <LibWeb/CSS/Ratio.h>
#include <LibWeb/CSS/Serialize.h>
#include <LibWeb/CSS/StyleValues/CalculatedStyleValue.h>
#include <LibWeb/CSS/StyleValues/KeywordStyleValue.h>
#include <LibWeb/CSS/StyleValues/RatioStyleValue.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Dump.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Page/Page.h>

namespace Web::CSS {

enum class QueryValueType : u8 {
    Ident,
    Length,
    Ratio,
    Resolution,
    Integer,
    Unknown,
};

static QueryValueType type_of(QueryValue const& value, ComputationContext const& computation_context)
{
    if (value->is_unresolved())
        return QueryValueType::Unknown;

    auto absolutized_value = value->absolutized(computation_context);
    if (absolutized_value->is_keyword())
        return QueryValueType::Ident;
    if (absolutized_value->is_integer())
        return QueryValueType::Integer;
    if (absolutized_value->is_length())
        return QueryValueType::Length;
    if (absolutized_value->is_ratio())
        return QueryValueType::Ratio;
    if (absolutized_value->is_resolution())
        return QueryValueType::Resolution;
    if (absolutized_value->is_calculated()) {
        auto const& calculated_value = absolutized_value->as_calculated();
        if (calculated_value.resolves_to_number())
            return QueryValueType::Integer;
        if (calculated_value.resolves_to_length())
            return QueryValueType::Length;
        if (calculated_value.resolves_to_resolution())
            return QueryValueType::Resolution;
    }

    return QueryValueType::Unknown;
}

NonnullRefPtr<MediaQuery> MediaQuery::create_not_all()
{
    auto media_query = new MediaQuery;
    media_query->m_negated = true;
    media_query->m_media_type = {
        .name = "all"_fly_string,
        .known_type = KnownMediaType::All,
    };

    return adopt_ref(*media_query);
}

String MediaFeature::to_string() const
{
    // NB: Even though we parse the parentheses as part of <media-in-parens> rather than <media-feature>, we serialize
    //     them as part of <media-feature> to avoid having to create a whole MediaInParens class just for serialization.
    switch (m_type) {
    case Type::IsTrue:
        return MUST(String::formatted("({})", string_from_media_feature_id(m_id)));
    case Type::ExactValue:
        return MUST(String::formatted("({}: {})", string_from_media_feature_id(m_id), value()->to_string(SerializationMode::Normal)));
    case Type::MinValue:
        return MUST(String::formatted("(min-{}: {})", string_from_media_feature_id(m_id), value()->to_string(SerializationMode::Normal)));
    case Type::MaxValue:
        return MUST(String::formatted("(max-{}: {})", string_from_media_feature_id(m_id), value()->to_string(SerializationMode::Normal)));
    case Type::Range: {
        auto& range = this->range();
        StringBuilder builder;
        builder.append('(');
        if (range.left_comparison.has_value())
            builder.appendff("{} {} ", (*range.left_value)->to_string(SerializationMode::Normal), string_from_query_comparison(*range.left_comparison));
        builder.append(string_from_media_feature_id(m_id));
        if (range.right_comparison.has_value())
            builder.appendff(" {} {}", string_from_query_comparison(*range.right_comparison), (*range.right_value)->to_string(SerializationMode::Normal));
        builder.append(')');

        return builder.to_string_without_validation();
    }
    }

    VERIFY_NOT_REACHED();
}

MatchResult MediaFeature::evaluate(BooleanExpressionEvaluationContext const& context) const
{
    auto const& document = context.document;
    VERIFY(document);

    // FIXME: In some cases (e.g. when parsing HTML using DOMParser::parse_from_string()) a document may not be associated with a window -
    //        for now we just return false but perhaps there are some media queries we should still attempt to resolve.
    if (!document->window())
        return MatchResult::False;

    auto maybe_queried_value = document->window()->query_media_feature(m_id);
    if (!maybe_queried_value.has_value())
        return MatchResult::False;
    auto queried_value = maybe_queried_value.release_value();

    ComputationContext computation_context {
        .length_resolution_context = Length::ResolutionContext::for_document(*document),
    };
    switch (m_type) {
    case Type::IsTrue: {
        QueryValue const& value = queried_value;
        auto absolutized_queried_value = value->absolutized(computation_context);
        switch (type_of(queried_value, computation_context)) {
        case QueryValueType::Integer:
            return as_match_result(int_from_style_value(absolutized_queried_value) != 0);
        case QueryValueType::Length: {
            auto length = Length::from_style_value(absolutized_queried_value, {});
            return as_match_result(length.raw_value() != 0);
        }
        // FIXME: I couldn't figure out from the spec how ratios should be evaluated in a boolean context.
        case QueryValueType::Ratio:
            return as_match_result(!absolutized_queried_value->as_ratio().resolved().is_degenerate());
        case QueryValueType::Resolution:
            return as_match_result(Resolution::from_style_value(absolutized_queried_value).to_dots_per_pixel() != 0);
        case QueryValueType::Ident:
            if (media_feature_keyword_is_falsey(m_id, absolutized_queried_value->to_keyword()))
                return MatchResult::False;
            return MatchResult::True;
        case QueryValueType::Unknown:
            return MatchResult::False;
        }
        VERIFY_NOT_REACHED();
    }

    case Type::ExactValue:
        return compare(value(), QueryComparison::Equal, queried_value, computation_context);

    case Type::MinValue:
        return compare(queried_value, QueryComparison::GreaterThanOrEqual, value(), computation_context);

    case Type::MaxValue:
        return compare(queried_value, QueryComparison::LessThanOrEqual, value(), computation_context);

    case Type::Range: {
        auto const& range = this->range();
        if (range.left_comparison.has_value()) {
            if (auto const left_result = compare(*range.left_value, *range.left_comparison, queried_value, computation_context); left_result != MatchResult::True)
                return left_result;
        }

        if (range.right_comparison.has_value()) {
            if (auto const right_result = compare(queried_value, *range.right_comparison, *range.right_value, computation_context); right_result != MatchResult::True)
                return right_result;
        }

        return MatchResult::True;
    }
    }

    VERIFY_NOT_REACHED();
}

MatchResult MediaFeature::compare(QueryValue const& left, QueryComparison comparison, QueryValue const& right, ComputationContext const& computation_context)
{
    auto left_type = type_of(left, computation_context);
    auto right_type = type_of(right, computation_context);

    if (left_type == QueryValueType::Unknown || right_type == QueryValueType::Unknown)
        return MatchResult::Unknown;

    if (left_type != right_type)
        return MatchResult::False;

    auto absolutized_left = left->absolutized(computation_context);
    auto absolutized_right = right->absolutized(computation_context);

    switch (left_type) {
    case QueryValueType::Ident:
        if (comparison == QueryComparison::Equal)
            return as_match_result(absolutized_left->to_keyword() == absolutized_right->to_keyword());
        return MatchResult::False;

    case QueryValueType::Integer:
        switch (comparison) {
        case QueryComparison::Equal:
            return as_match_result(int_from_style_value(absolutized_left) == int_from_style_value(absolutized_right));
        case QueryComparison::LessThan:
            return as_match_result(int_from_style_value(absolutized_left) < int_from_style_value(absolutized_right));
        case QueryComparison::LessThanOrEqual:
            return as_match_result(int_from_style_value(absolutized_left) <= int_from_style_value(absolutized_right));
        case QueryComparison::GreaterThan:
            return as_match_result(int_from_style_value(absolutized_left) > int_from_style_value(absolutized_right));
        case QueryComparison::GreaterThanOrEqual:
            return as_match_result(int_from_style_value(absolutized_left) >= int_from_style_value(absolutized_right));
        }
        VERIFY_NOT_REACHED();

    case QueryValueType::Length: {
        auto left_px = Length::from_style_value(absolutized_left, {}).absolute_length_to_px();
        auto right_px = Length::from_style_value(absolutized_right, {}).absolute_length_to_px();

        switch (comparison) {
        case QueryComparison::Equal:
            return as_match_result(left_px == right_px);
        case QueryComparison::LessThan:
            return as_match_result(left_px < right_px);
        case QueryComparison::LessThanOrEqual:
            return as_match_result(left_px <= right_px);
        case QueryComparison::GreaterThan:
            return as_match_result(left_px > right_px);
        case QueryComparison::GreaterThanOrEqual:
            return as_match_result(left_px >= right_px);
        }
        VERIFY_NOT_REACHED();
    }

    case QueryValueType::Ratio: {
        auto left_decimal = absolutized_left->as_ratio().resolved().value();
        auto right_decimal = absolutized_right->as_ratio().resolved().value();

        switch (comparison) {
        case QueryComparison::Equal:
            return as_match_result(left_decimal == right_decimal);
        case QueryComparison::LessThan:
            return as_match_result(left_decimal < right_decimal);
        case QueryComparison::LessThanOrEqual:
            return as_match_result(left_decimal <= right_decimal);
        case QueryComparison::GreaterThan:
            return as_match_result(left_decimal > right_decimal);
        case QueryComparison::GreaterThanOrEqual:
            return as_match_result(left_decimal >= right_decimal);
        }
        VERIFY_NOT_REACHED();
    }

    case QueryValueType::Resolution: {
        auto left_dppx = Resolution::from_style_value(absolutized_left).to_dots_per_pixel();
        auto right_dppx = Resolution::from_style_value(absolutized_right).to_dots_per_pixel();

        switch (comparison) {
        case QueryComparison::Equal:
            return as_match_result(left_dppx == right_dppx);
        case QueryComparison::LessThan:
            return as_match_result(left_dppx < right_dppx);
        case QueryComparison::LessThanOrEqual:
            return as_match_result(left_dppx <= right_dppx);
        case QueryComparison::GreaterThan:
            return as_match_result(left_dppx > right_dppx);
        case QueryComparison::GreaterThanOrEqual:
            return as_match_result(left_dppx >= right_dppx);
        }
        VERIFY_NOT_REACHED();
    }

    case QueryValueType::Unknown:
        VERIFY_NOT_REACHED();
    }

    VERIFY_NOT_REACHED();
}

void MediaFeature::dump(StringBuilder& builder, int indent_levels) const
{
    indent(builder, indent_levels);
    builder.appendff("MediaFeature: {}\n", to_string());
}

String MediaQuery::to_string() const
{
    StringBuilder builder;

    if (m_negated)
        builder.append("not "sv);

    if (m_negated || m_media_type.known_type != KnownMediaType::All || !m_media_condition) {
        if (m_media_type.known_type.has_value()) {
            builder.append(CSS::to_string(m_media_type.known_type.value()));
        } else {
            builder.append(serialize_an_identifier(m_media_type.name.to_ascii_lowercase()));
        }
        if (m_media_condition)
            builder.append(" and "sv);
    }

    if (m_media_condition) {
        builder.append(m_media_condition->to_string());
    }

    return MUST(builder.to_string());
}

bool MediaQuery::evaluate(DOM::Document const& document)
{
    auto matches_media = [](MediaType const& media) -> MatchResult {
        if (!media.known_type.has_value())
            return MatchResult::False;
        switch (media.known_type.value()) {
        case KnownMediaType::All:
            return MatchResult::True;
        case KnownMediaType::Print:
            // FIXME: Enable for printing, when we have printing!
            return MatchResult::False;
        case KnownMediaType::Screen:
            // FIXME: Disable for printing, when we have printing!
            return MatchResult::True;
        }
        VERIFY_NOT_REACHED();
    };

    MatchResult result = matches_media(m_media_type);

    if ((result != MatchResult::False) && m_media_condition)
        result = result && m_media_condition->evaluate({ .document = document });

    if (m_negated)
        result = negate(result);

    m_matches = result == MatchResult::True;
    return m_matches;
}

void MediaQuery::dump(StringBuilder& builder, int indent_levels) const
{
    dump_indent(builder, indent_levels);
    builder.appendff("Media condition: (matches = {})\n", m_matches);

    dump_indent(builder, indent_levels + 1);
    builder.appendff("Negated: {}\n", m_negated);

    dump_indent(builder, indent_levels + 1);
    builder.appendff("Type: {}\n", m_media_type.name);

    if (m_media_condition) {
        dump_indent(builder, indent_levels + 1);
        builder.append("Condition:\n"sv);
        m_media_condition->dump(builder, indent_levels + 2);
    }
}

// https://www.w3.org/TR/cssom-1/#serialize-a-media-query-list
String serialize_a_media_query_list(Vector<NonnullRefPtr<MediaQuery>> const& media_queries)
{
    // 1. If the media query list is empty, then return the empty string.
    if (media_queries.is_empty())
        return String {};

    // 2. Serialize each media query in the list of media queries, in the same order as they
    // appear in the media query list, and then serialize the list.
    return MUST(String::join(", "sv, media_queries));
}

Optional<MediaQuery::KnownMediaType> media_type_from_string(StringView name)
{
    if (name.equals_ignoring_ascii_case("all"sv))
        return MediaQuery::KnownMediaType::All;
    if (name.equals_ignoring_ascii_case("print"sv))
        return MediaQuery::KnownMediaType::Print;
    if (name.equals_ignoring_ascii_case("screen"sv))
        return MediaQuery::KnownMediaType::Screen;
    return {};
}

StringView to_string(MediaQuery::KnownMediaType media_type)
{
    switch (media_type) {
    case MediaQuery::KnownMediaType::All:
        return "all"sv;
    case MediaQuery::KnownMediaType::Print:
        return "print"sv;
    case MediaQuery::KnownMediaType::Screen:
        return "screen"sv;
    }
    VERIFY_NOT_REACHED();
}

}
