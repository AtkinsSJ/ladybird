/*
 * Copyright (c) 2023, Matthew Olsson <mattco@serenityos.org>
 * Copyright (c) 2026, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Animations/AnimationPlaybackEvent.h>
#include <LibWeb/Bindings/AnimationPlaybackEventPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CSS/CSSNumericValue.h>

namespace Web::Animations {

GC_DEFINE_ALLOCATOR(AnimationPlaybackEvent);

GC::Ref<AnimationPlaybackEvent> AnimationPlaybackEvent::create(JS::Realm& realm, FlyString const& type, AnimationPlaybackEventInit const& event_init)
{
    return realm.create<AnimationPlaybackEvent>(realm, type, event_init);
}

// https://www.w3.org/TR/web-animations-1/#dom-animationplaybackevent-animationplaybackevent
WebIDL::ExceptionOr<GC::Ref<AnimationPlaybackEvent>> AnimationPlaybackEvent::construct_impl(JS::Realm& realm, FlyString const& type, AnimationPlaybackEventInit const& event_init)
{
    return create(realm, type, event_init);
}

static AnimationPlaybackEvent::CSSNumberishInternal optional_numberish_root_to_numberish_internal(Optional<CSS::CSSNumberish> numberish_root)
{
    if (!numberish_root.has_value())
        return Empty {};
    return numberish_root->visit(
        [](GC::Root<CSS::CSSNumericValue> const& root) -> AnimationPlaybackEvent::CSSNumberishInternal { return GC::Ref { *root }; },
        [](auto const& other) -> AnimationPlaybackEvent::CSSNumberishInternal { return other; });
}

AnimationPlaybackEvent::AnimationPlaybackEvent(JS::Realm& realm, FlyString const& type, AnimationPlaybackEventInit const& event_init)
    : DOM::Event(realm, type, event_init)
    , m_current_time(optional_numberish_root_to_numberish_internal(event_init.current_time))
    , m_timeline_time(optional_numberish_root_to_numberish_internal(event_init.timeline_time))
{
}

void AnimationPlaybackEvent::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AnimationPlaybackEvent);
    Base::initialize(realm);
}

void AnimationPlaybackEvent::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);

    auto visit_numberish_internal = [&](auto& numberish_internal) {
        numberish_internal.visit(
            [&](GC::Ref<CSS::CSSNumericValue> const& numeric) { visitor.visit(numeric); },
            [](auto const&) {});
    };
    visit_numberish_internal(m_current_time);
    visit_numberish_internal(m_timeline_time);
}

static NullableCSSNumberish numberish_internal_to_nullable_numberish(AnimationPlaybackEvent::CSSNumberishInternal const& numberish)
{
    return numberish.visit(
        [](GC::Ref<CSS::CSSNumericValue> const& ref) -> NullableCSSNumberish { return GC::Root { *ref }; },
        [](auto const& other) -> NullableCSSNumberish { return other; });
}

NullableCSSNumberish AnimationPlaybackEvent::current_time() const
{
    return numberish_internal_to_nullable_numberish(m_current_time);
}

NullableCSSNumberish AnimationPlaybackEvent::timeline_time() const
{
    return numberish_internal_to_nullable_numberish(m_timeline_time);
}

}
