/*
 * Copyright (c) 2026, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibWeb/CSS/BooleanExpression.h>
#include <LibWeb/CSS/StyleValues/StyleValue.h>

namespace Web::CSS {

using QueryValue = NonnullRefPtr<StyleValue const>;

enum class QueryComparison : u8 {
    Equal,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
};

struct QueryValueRange {
    Optional<QueryValue> left_value {};
    Optional<QueryComparison> left_comparison {};
    Optional<QueryComparison> right_comparison {};
    Optional<QueryValue> right_value {};
};

StringView string_from_query_comparison(QueryComparison);
bool query_comparisons_match(QueryComparison, QueryComparison);

}
