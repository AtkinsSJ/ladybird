/*
 * Copyright (c) 2026, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/QueryValue.h>

namespace Web::CSS {

StringView string_from_query_comparison(QueryComparison comparison)
{
    switch (comparison) {
    case QueryComparison::Equal:
        return "="sv;
    case QueryComparison::LessThan:
        return "<"sv;
    case QueryComparison::LessThanOrEqual:
        return "<="sv;
    case QueryComparison::GreaterThan:
        return ">"sv;
    case QueryComparison::GreaterThanOrEqual:
        return ">="sv;
    }
    VERIFY_NOT_REACHED();
}

bool query_comparisons_match(QueryComparison a, QueryComparison b)
{
    switch (a) {
    case QueryComparison::Equal:
        return b == QueryComparison::Equal;
    case QueryComparison::LessThan:
    case QueryComparison::LessThanOrEqual:
        return b == QueryComparison::LessThan || b == QueryComparison::LessThanOrEqual;
    case QueryComparison::GreaterThan:
    case QueryComparison::GreaterThanOrEqual:
        return b == QueryComparison::GreaterThan || b == QueryComparison::GreaterThanOrEqual;
    }
    VERIFY_NOT_REACHED();
}

}
