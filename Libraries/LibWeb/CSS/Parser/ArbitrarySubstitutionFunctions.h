/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/CSS/GuaranteedInvalidValue.h>
#include <LibWeb/CSS/Parser/Parser.h>

namespace Web::CSS {

using UnresolvedValue = Variant<Parser::ComponentValue, GuaranteedInvalidValue>;

struct [[nodiscard]] SubstitutionResult {
    Optional<Vector<UnresolvedValue>> result;
    Optional<Vector<UnresolvedValue>> fallback;
};

[[maybe_unused]] static SubstitutionResult resolve_an_attr_function(DOM::Element const&, FlyString const& attribute_name, Optional<FlyString> syntax, Optional<Vector<UnresolvedValue>> const& fallback);

[[maybe_unused]] static SubstitutionResult resolve_a_var_function(DOM::Element const&, FlyString const& custom_property_name, Optional<Vector<UnresolvedValue>> const& fallback);

}
