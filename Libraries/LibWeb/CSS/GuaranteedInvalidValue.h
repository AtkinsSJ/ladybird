/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>

namespace Web::CSS {

// https://drafts.csswg.org/css-variables/#guaranteed-invalid-value
class GuaranteedInvalidValue {
public:
    GuaranteedInvalidValue() = default;
    String to_string() const { return {}; }
};

}
