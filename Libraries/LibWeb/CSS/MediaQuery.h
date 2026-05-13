/*
 * Copyright (c) 2021-2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/FlyString.h>
#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/OwnPtr.h>
#include <AK/RefCounted.h>
#include <LibWeb/CSS/BooleanExpression.h>
#include <LibWeb/CSS/MediaFeatureID.h>
#include <LibWeb/CSS/Parser/ComponentValue.h>
#include <LibWeb/CSS/QueryValue.h>
#include <LibWeb/CSS/StyleValues/ComputationContext.h>

namespace Web::CSS {

// https://www.w3.org/TR/mediaqueries-4/#mq-features
class MediaFeature final : public BooleanExpression {
public:
    // Corresponds to `<mf-boolean>` grammar
    static NonnullOwnPtr<MediaFeature> boolean(MediaFeatureID id)
    {
        return adopt_own(*new MediaFeature(Type::IsTrue, id));
    }

    // Corresponds to `<mf-plain>` grammar
    static NonnullOwnPtr<MediaFeature> plain(MediaFeatureID id, QueryValue value)
    {
        return adopt_own(*new MediaFeature(Type::ExactValue, move(id), move(value)));
    }
    static NonnullOwnPtr<MediaFeature> min(MediaFeatureID id, QueryValue value)
    {
        return adopt_own(*new MediaFeature(Type::MinValue, id, move(value)));
    }
    static NonnullOwnPtr<MediaFeature> max(MediaFeatureID id, QueryValue value)
    {
        return adopt_own(*new MediaFeature(Type::MaxValue, id, move(value)));
    }

    static NonnullOwnPtr<MediaFeature> half_range(QueryValue value, QueryComparison comparison, MediaFeatureID id)
    {
        return adopt_own(*new MediaFeature(Type::Range, id,
            QueryValueRange {
                .left_value = move(value),
                .left_comparison = comparison,
            }));
    }
    static NonnullOwnPtr<MediaFeature> half_range(MediaFeatureID id, QueryComparison comparison, QueryValue value)
    {
        return adopt_own(*new MediaFeature(Type::Range, id,
            QueryValueRange {
                .right_comparison = comparison,
                .right_value = move(value),
            }));
    }

    // Corresponds to `<mf-range>` grammar, with two comparisons
    static NonnullOwnPtr<MediaFeature> range(QueryValue left_value, QueryComparison left_comparison, MediaFeatureID id, QueryComparison right_comparison, QueryValue right_value)
    {
        return adopt_own(*new MediaFeature(Type::Range, id,
            QueryValueRange {
                .left_value = move(left_value),
                .left_comparison = left_comparison,
                .right_comparison = right_comparison,
                .right_value = move(right_value),
            }));
    }

    virtual MatchResult evaluate(BooleanExpressionEvaluationContext const&) const override;
    virtual String to_string() const override;
    virtual void dump(StringBuilder&, int indent_levels = 0) const override;

private:
    enum class Type : u8 {
        IsTrue,
        ExactValue,
        MinValue,
        MaxValue,
        Range,
    };

    MediaFeature(Type type, MediaFeatureID id, Variant<Empty, QueryValue, QueryValueRange> value = {})
        : m_type(type)
        , m_id(move(id))
        , m_value(move(value))
    {
    }

    static MatchResult compare(QueryValue const& left, QueryComparison, QueryValue const& right, ComputationContext const&);

    QueryValue const& value() const { return m_value.get<QueryValue>(); }
    QueryValueRange const& range() const { return m_value.get<QueryValueRange>(); }

    Type m_type;
    MediaFeatureID m_id;
    Variant<Empty, QueryValue, QueryValueRange> m_value {};
};

class MediaQuery : public RefCounted<MediaQuery> {
    friend class Parser::Parser;

public:
    ~MediaQuery() = default;

    // https://www.w3.org/TR/mediaqueries-4/#media-types
    enum class KnownMediaType : u8 {
        All,
        Print,
        Screen,
    };
    struct MediaType {
        FlyString name;
        Optional<KnownMediaType> known_type;
    };

    static NonnullRefPtr<MediaQuery> create_not_all();
    static NonnullRefPtr<MediaQuery> create() { return adopt_ref(*new MediaQuery); }

    bool matches() const { return m_matches; }
    bool evaluate(DOM::Document const&);
    String to_string() const;

    void dump(StringBuilder&, int indent_levels = 0) const;

private:
    MediaQuery() = default;

    // https://www.w3.org/TR/mediaqueries-4/#mq-not
    bool m_negated { false };
    MediaType m_media_type { .name = "all"_fly_string, .known_type = KnownMediaType::All };
    OwnPtr<BooleanExpression> m_media_condition { nullptr };

    // Cached value, updated by evaluate()
    bool m_matches { false };
};

String serialize_a_media_query_list(Vector<NonnullRefPtr<MediaQuery>> const&);

Optional<MediaQuery::KnownMediaType> media_type_from_string(StringView);
StringView to_string(MediaQuery::KnownMediaType);

}

namespace AK {

template<>
struct Formatter<Web::CSS::MediaFeature> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Web::CSS::MediaFeature const& media_feature)
    {
        return Formatter<StringView>::format(builder, media_feature.to_string());
    }
};

template<>
struct Formatter<Web::CSS::MediaQuery> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Web::CSS::MediaQuery const& media_query)
    {
        return Formatter<StringView>::format(builder, media_query.to_string());
    }
};

}
