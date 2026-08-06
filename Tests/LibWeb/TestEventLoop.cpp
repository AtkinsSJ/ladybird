/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Platform/FontPlugin.h>

TEST_CASE(nested_pause_handles_keep_the_event_loop_paused)
{
    Web::Platform::FontPlugin font_plugin { false };
    Web::Platform::FontPlugin::install(font_plugin);

    auto realm = Web::Bindings::create_a_principal_javascript_realm();
    URL::URL url;
    auto document = Web::DOM::Document::create(realm, url);
    auto& window = as<Web::HTML::Window>(realm->global_object());
    document->set_window(window);
    window.set_associated_document(document);
    auto& event_loop = Web::HTML::main_thread_event_loop();

    {
        auto outer_pause = event_loop.pause();
        EXPECT(event_loop.execution_paused());

        {
            auto inner_pause = event_loop.pause();
            EXPECT(event_loop.execution_paused());
        }

        EXPECT(event_loop.execution_paused());
    }

    EXPECT(!event_loop.execution_paused());
}
