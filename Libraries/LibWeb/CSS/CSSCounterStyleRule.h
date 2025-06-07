/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/CSS/CSSRule.h>

namespace Web::CSS {

class CSSCounterStyleRule final : public CSSRule {
    WEB_PLATFORM_OBJECT(CSSCounterStyleRule, CSSRule);
    GC_DECLARE_ALLOCATOR(CSSCounterStyleRule);

public:
    [[nodiscard]] static GC::Ref<CSSCounterStyleRule> create(JS::Realm&, FlyString name, GC::Ref<CSSDescriptors>);

    virtual ~CSSCounterStyleRule() override;

    bool is_valid() const;

    FlyString const& name() const { return m_name; }
    void set_name(String);

    String system() const;
    WebIDL::ExceptionOr<void> set_system(StringView);

    String symbols() const;
    WebIDL::ExceptionOr<void> set_symbols(StringView);

    String additive_symbols() const;
    WebIDL::ExceptionOr<void> set_additive_symbols(StringView);

    String negative() const;
    WebIDL::ExceptionOr<void> set_negative(StringView);

    String prefix() const;
    WebIDL::ExceptionOr<void> set_prefix(StringView);

    String suffix() const;
    WebIDL::ExceptionOr<void> set_suffix(StringView);

    String range() const;
    WebIDL::ExceptionOr<void> set_range(StringView);

    String pad() const;
    WebIDL::ExceptionOr<void> set_pad(StringView);

    String speak_as() const;
    WebIDL::ExceptionOr<void> set_speak_as(StringView);

    String fallback() const;
    WebIDL::ExceptionOr<void> set_fallback(StringView);

private:
    CSSCounterStyleRule(JS::Realm&, FlyString name, GC::Ref<CSSDescriptors>);

    virtual void initialize(JS::Realm&) override;
    virtual String serialized() const override;
    virtual void visit_edges(Visitor&) override;

    FlyString m_name;
    GC::Ref<CSSDescriptors> m_descriptors;
};

ReadonlySpan<StringView> non_overridable_custom_counter_style_names_and_none();

}
