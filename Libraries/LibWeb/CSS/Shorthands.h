/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/RefPtr.h>
#include <LibWeb/CSS/Descriptor.h>
#include <LibWeb/Forward.h>

namespace Web::CSS {

bool is_shorthand(AtRuleID, DescriptorID);
void for_each_expanded_longhand(AtRuleID, DescriptorID, RefPtr<CSSStyleValue const>, Function<void(DescriptorID, RefPtr<CSSStyleValue const>)>);
Vector<DescriptorID> shorthands_for_descriptor(AtRuleID, DescriptorID);

RefPtr<CSSStyleValue> construct_shorthand(AtRuleID, DescriptorID, ReadonlySpan<Descriptor> longhands);

}
