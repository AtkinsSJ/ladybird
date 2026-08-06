/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Debugger.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/VM.h>
#include <LibJS/Script.h>
#include <LibTest/TestCase.h>

TEST_CASE(debugger_statement_pauses_execution)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("debugger; 42;"sv, realm, "debugger.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();

    bool did_pause = false;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        did_pause = true;
        EXPECT_EQ(pause_info.reason, JS::Debugger::PauseReason::DebuggerStatement);
        EXPECT_EQ(pause_info.bytecode_offset, 0u);
        VERIFY(pause_info.source_range.has_value());
        EXPECT_EQ(pause_info.source_range->filename(), "debugger.js"_utf16);
        EXPECT_EQ(pause_info.source_range->start.line, 1u);
        EXPECT_EQ(pause_info.source_range->start.column, 1u);
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT(did_pause);
}

TEST_CASE(debugger_statement_continues_without_pause_callback)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("debugger; 42;"sv, realm);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();

    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
}

TEST_CASE(debugger_pause_reports_the_active_stack)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse(R"(
function outer() {
    inner();
}
function inner() {
    debugger;
}
outer();
)"sv,
        realm, "stack.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        EXPECT(pause_info.stack_trace.size() >= 3);
        EXPECT_EQ(pause_info.stack_trace[0].execution_context->executable.ptr(), pause_info.executable.ptr());
        VERIFY(pause_info.stack_trace[0].source_range.has_value());
        EXPECT_EQ(pause_info.stack_trace[0].source_range->start.line, 6u);

        size_t script_frame_count = 0;
        for (auto const& frame : pause_info.stack_trace) {
            if (frame.execution_context->executable)
                ++script_frame_count;
        }
        EXPECT_EQ(script_frame_count, 3u);
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
}

TEST_CASE(debugger_can_evaluate_in_a_paused_frame)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse(R"(
function answer()
{
    let value = 41;
    debugger;
    return value;
}
answer();
)"sv,
        realm, "evaluate.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        auto* frame = pause_info.stack_trace.first().execution_context;
        VERIFY(frame);

        auto result = vm->debugger()->evaluate_in_frame(*frame, "value + 1"sv);
        VERIFY(!result.is_error());
        EXPECT_EQ(result.release_value().as_i32(), 42);

        result = vm->debugger()->evaluate_in_frame(*frame, "value = 50"sv);
        VERIFY(!result.is_error());
        EXPECT_EQ(result.release_value().as_i32(), 50);
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    VERIFY(!result.is_error());
    EXPECT_EQ(result.release_value().as_i32(), 50);
}

TEST_CASE(debugger_frame_evaluation_exposes_and_updates_parameters)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("function update(value) { debugger; return value; } update(41);"sv, realm, "parameters.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        auto* frame = pause_info.stack_trace.first().execution_context;
        VERIFY(frame);

        auto result = vm->debugger()->evaluate_in_frame(*frame, "value += 1"sv);
        VERIFY(!result.is_error());
        EXPECT_EQ(result.release_value().as_i32(), 42);
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    VERIFY(!result.is_error());
    EXPECT_EQ(result.release_value().as_i32(), 42);
}

TEST_CASE(debugger_frame_evaluation_preserves_const_bindings)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("function read() { const value = 41; debugger; return value; } read();"sv, realm, "const.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        auto* frame = pause_info.stack_trace.first().execution_context;
        VERIFY(frame);
        EXPECT(vm->debugger()->evaluate_in_frame(*frame, "value = 42"sv).is_error());
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    VERIFY(!result.is_error());
    EXPECT_EQ(result.release_value().as_i32(), 41);
}

TEST_CASE(debugger_frame_evaluation_does_not_overwrite_shadowed_locals)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse(R"(
function readOuterValue()
{
    let value = 1;
    {
        let value = 2;
        debugger;
    }
    return value;
}
readOuterValue();
)"sv,
        realm, "shadowed-locals.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        auto* frame = pause_info.stack_trace.first().execution_context;
        VERIFY(frame);

        auto result = vm->debugger()->evaluate_in_frame(*frame, "value"sv);
        VERIFY(!result.is_error());
        EXPECT_EQ(result.release_value().as_i32(), 2);
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    VERIFY(!result.is_error());
    EXPECT_EQ(result.release_value().as_i32(), 1);
}

TEST_CASE(debugger_frame_evaluation_uses_the_active_shadowed_binding)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse(R"(
function readValue()
{
    let value = 1;
    debugger;
    {
        let value = 2;
        debugger;
    }
    debugger;
}
readValue();
)"sv,
        realm, "shadowed-live-ranges.js"sv);
    VERIFY(!script_or_error.is_error());

    Vector<i32> values;
    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        auto* frame = pause_info.stack_trace.first().execution_context;
        VERIFY(frame);
        auto result = vm->debugger()->evaluate_in_frame(*frame, "value"sv);
        VERIFY(!result.is_error());
        values.append(result.release_value().as_i32());
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(values, (Vector<i32> { 1, 2, 1 }));
}

TEST_CASE(breakpoints_resolve_to_source_map_entries)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("breakpoint.js"_utf16, 2));
    EXPECT_EQ(MUST(vm->debugger()->add_breakpoint("breakpoint.js"_utf16, 2)), breakpoint_id);

    auto script_or_error = JS::Script::parse("let first = 1;\nlet second = 2;\n"sv, realm, "breakpoint.js"sv);
    VERIFY(!script_or_error.is_error());
    EXPECT(vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto* executable = script_or_error.value()->cached_executable();
    VERIFY(executable);
    auto source_map_entry = executable->source_map.find_if([](auto const& entry) {
        return entry.line == 2;
    });
    VERIFY(!source_map_entry.is_end());
    EXPECT(executable->has_debugger_breakpoint_at(source_map_entry->bytecode_offset));

    EXPECT(vm->debugger()->remove_breakpoint(breakpoint_id));
    EXPECT(!executable->has_debugger_breakpoint_at(source_map_entry->bytecode_offset));
    EXPECT(!vm->debugger()->remove_breakpoint(breakpoint_id));
}

TEST_CASE(source_specific_breakpoints_do_not_match_other_sources_with_the_same_filename)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto first_script = MUST(JS::Script::parse("let first = 1;\n"sv, realm, "shared.js"sv));
    auto* first_executable = first_script->cached_executable();
    VERIFY(first_executable);

    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint(first_executable->source_code, 1));
    EXPECT(first_executable->has_debugger_breakpoint(breakpoint_id));

    auto second_script = MUST(JS::Script::parse("let second = 2;\n"sv, realm, "shared.js"sv));
    auto* second_executable = second_script->cached_executable();
    VERIFY(second_executable);
    EXPECT(!second_executable->has_debugger_breakpoint(breakpoint_id));
}

TEST_CASE(source_code_reports_breakpoint_positions)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("let first = 1; first++;\nlet second = 2;\n"sv, realm, "positions.js"sv);
    VERIFY(!script_or_error.is_error());

    auto* executable = script_or_error.value()->cached_executable();
    VERIFY(executable);
    auto positions = executable->source_code->breakpoint_positions();
    EXPECT(!positions.is_empty());

    for (size_t index = 1; index < positions.size(); ++index) {
        auto const& previous = positions[index - 1];
        auto const& current = positions[index];
        EXPECT(previous.line < current.line || (previous.line == current.line && previous.column < current.column));
    }

    EXPECT(positions.find_if([](auto const& position) { return position.line == 1 && position.column == 1; }) != positions.end());
    EXPECT(positions.find_if([](auto const& position) { return position.line == 2; }) != positions.end());
}

TEST_CASE(breakpoints_resolve_when_an_existing_executable_runs)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("let value = 1;\n"sv, realm, "existing.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("existing.js"_utf16, 1));
    EXPECT(!vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT(vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto* executable = script_or_error.value()->cached_executable();
    VERIFY(executable);
    vm->disable_debugging();
    EXPECT(!executable->has_debugger_breakpoint(breakpoint_id));
}

TEST_CASE(breakpoints_resolve_when_lazy_functions_are_compiled)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("lazy.js"_utf16, 2));

    auto declaration_or_error = JS::Script::parse("function lazy() {\n    let value = 1;\n    return value;\n}\n"sv, realm, "lazy.js"sv);
    VERIFY(!declaration_or_error.is_error());
    EXPECT(!vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto declaration_result = vm->run(*declaration_or_error.value());
    EXPECT(!declaration_result.is_error());
    EXPECT(!vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto call_or_error = JS::Script::parse("lazy();"_utf16, realm, "caller.js"sv);
    VERIFY(!call_or_error.is_error());
    auto call_result = vm->run(*call_or_error.value());
    EXPECT(!call_result.is_error());
    EXPECT(vm->debugger()->is_breakpoint_resolved(breakpoint_id));
}
TEST_CASE(breakpoints_slide_to_the_next_source_position)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("slide.js"_utf16, 2));

    auto script_or_error = JS::Script::parse("let first = 1;\n\nlet second = 2;\n"sv, realm, "slide.js"sv);
    VERIFY(!script_or_error.is_error());
    EXPECT(vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    auto* executable = script_or_error.value()->cached_executable();
    VERIFY(executable);
    auto source_map_entry = executable->source_map.find_if([](auto const& entry) {
        return entry.line == 3;
    });
    VERIFY(!source_map_entry.is_end());
    EXPECT(executable->has_debugger_breakpoint_at(source_map_entry->bytecode_offset));
}

TEST_CASE(breakpoints_resolve_when_precompiled_functions_are_called_inline)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto declaration_or_error = JS::Script::parse("function target() {\n    return 42;\n}\n"sv, realm, "inline.js"sv);
    VERIFY(!declaration_or_error.is_error());
    auto declaration_result = vm->run(*declaration_or_error.value());
    EXPECT(!declaration_result.is_error());

    auto call_or_error = JS::Script::parse("target();"_utf16, realm, "caller.js"sv);
    VERIFY(!call_or_error.is_error());
    auto call_result = vm->run(*call_or_error.value());
    EXPECT(!call_result.is_error());

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("inline.js"_utf16, 2));
    EXPECT(!vm->debugger()->is_breakpoint_resolved(breakpoint_id));

    call_result = vm->run(*call_or_error.value());
    EXPECT(!call_result.is_error());
    EXPECT(vm->debugger()->is_breakpoint_resolved(breakpoint_id));
}

TEST_CASE(manual_breakpoints_pause_execution)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("manual.js"_utf16, 2));

    size_t pause_count = 0;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        ++pause_count;
        EXPECT_EQ(pause_info.reason, JS::Debugger::PauseReason::Breakpoint);
        VERIFY(pause_info.source_range.has_value());
        EXPECT_EQ(pause_info.source_range->start.line, 2u);
        EXPECT_EQ(pause_info.breakpoint_ids.size(), 1u);
        EXPECT_EQ(pause_info.breakpoint_ids.first(), breakpoint_id);
        vm->debugger()->continue_execution();
    });

    auto script_or_error = JS::Script::parse("var first = 1;\nvar second = 2;\n"sv, realm, "manual.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 1u);

    EXPECT(vm->debugger()->remove_breakpoint(breakpoint_id));
    result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 1u);
}

TEST_CASE(pause_on_next_bytecode_execution_is_one_shot)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    vm->debugger()->request_pause_on_next_bytecode_execution();

    size_t pause_count = 0;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        ++pause_count;
        EXPECT_EQ(pause_info.reason, JS::Debugger::PauseReason::Entry);
        VERIFY(pause_info.source_range.has_value());
        vm->debugger()->continue_execution();
    });

    auto script_or_error = JS::Script::parse("42;"sv, realm, "entry.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 1u);

    result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 1u);
}

TEST_CASE(pause_on_next_bytecode_execution_waits_for_a_callback)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto script_or_error = JS::Script::parse("42;"sv, realm, "entry.js"sv);
    VERIFY(!script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->request_pause_on_next_bytecode_execution();
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());

    size_t pause_count = 0;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const&) {
        ++pause_count;
        vm->debugger()->continue_execution();
    });

    result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 1u);
}

TEST_CASE(nested_execution_does_not_consume_a_pause_request)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    auto outer_script_or_error = JS::Script::parse("let first = 1;\nlet second = 2;\n"sv, realm, "outer.js"sv);
    VERIFY(!outer_script_or_error.is_error());
    auto nested_script_or_error = JS::Script::parse("1 + 1;"sv, realm, "watch.js"sv);
    VERIFY(!nested_script_or_error.is_error());

    vm->enable_debugging();
    vm->debugger()->request_pause_on_next_bytecode_execution();

    size_t pause_count = 0;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const&) {
        ++pause_count;
        if (pause_count == 1) {
            vm->debugger()->request_pause_on_next_bytecode_execution();
            auto nested_result = vm->run(*nested_script_or_error.value());
            EXPECT(!nested_result.is_error());
            EXPECT_EQ(pause_count, 1u);
        }
        vm->debugger()->continue_execution();
    });

    auto result = vm->run(*outer_script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_count, 2u);
}

TEST_CASE(manual_breakpoints_replace_debugger_statement_pauses)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    MUST(vm->debugger()->add_breakpoint("combined.js"_utf16, 1));

    Vector<JS::Debugger::PauseReason> pause_reasons;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        pause_reasons.append(pause_info.reason);
        vm->debugger()->continue_execution();
    });

    auto script_or_error = JS::Script::parse("debugger;"sv, realm, "combined.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());
    EXPECT_EQ(pause_reasons.size(), 1u);
    EXPECT_EQ(pause_reasons[0], JS::Debugger::PauseReason::Breakpoint);
}

TEST_CASE(step_into_pauses_in_a_called_function)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("step-into.js"_utf16, 4));

    Vector<JS::Debugger::PauseInfo> pauses;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        pauses.append(pause_info);
        if (pauses.size() == 1)
            EXPECT(vm->debugger()->remove_breakpoint(breakpoint_id));
        vm->debugger()->continue_execution(pauses.size() == 1 ? JS::Debugger::ResumeMode::StepInto : JS::Debugger::ResumeMode::Continue);
    });

    auto script_or_error = JS::Script::parse(
        "function callee() {\n"
        "    let inside = 1;\n"
        "}\n"
        "callee();\n"
        "let after = 2;\n"sv,
        realm, "step-into.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());

    EXPECT_EQ(pauses.size(), 2u);
    EXPECT_EQ(pauses[0].reason, JS::Debugger::PauseReason::Breakpoint);
    EXPECT_EQ(pauses[1].reason, JS::Debugger::PauseReason::Step);
    EXPECT_EQ(pauses[1].source_range->start.line, 2u);
}

TEST_CASE(step_over_does_not_pause_in_called_functions)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("step-over.js"_utf16, 4));

    Vector<JS::Debugger::PauseInfo> pauses;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        pauses.append(pause_info);
        if (pauses.size() == 1)
            EXPECT(vm->debugger()->remove_breakpoint(breakpoint_id));
        vm->debugger()->continue_execution(pauses.size() == 1 ? JS::Debugger::ResumeMode::StepOver : JS::Debugger::ResumeMode::Continue);
    });

    auto script_or_error = JS::Script::parse(
        "function callee() {\n"
        "    let inside = 1;\n"
        "}\n"
        "callee();\n"
        "let after = 2;\n"sv,
        realm, "step-over.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());

    EXPECT_EQ(pauses.size(), 2u);
    EXPECT_EQ(pauses[0].reason, JS::Debugger::PauseReason::Breakpoint);
    EXPECT_EQ(pauses[1].reason, JS::Debugger::PauseReason::Step);
    EXPECT_EQ(pauses[1].source_range->start.line, 5u);
}

TEST_CASE(step_out_pauses_after_the_current_function_returns)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();

    Vector<JS::Debugger::PauseInfo> pauses;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        pauses.append(pause_info);
        vm->debugger()->continue_execution(pauses.size() == 1 ? JS::Debugger::ResumeMode::StepOut : JS::Debugger::ResumeMode::Continue);
    });

    auto script_or_error = JS::Script::parse(
        "function callee() {\n"
        "    debugger;\n"
        "}\n"
        "callee();\n"
        "let after = 2;\n"sv,
        realm, "step-out.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());

    EXPECT_EQ(pauses.size(), 2u);
    VERIFY(pauses.size() == 2);
    EXPECT_EQ(pauses[0].reason, JS::Debugger::PauseReason::DebuggerStatement);
    EXPECT_EQ(pauses[1].reason, JS::Debugger::PauseReason::Step);
    EXPECT_EQ(pauses[1].source_range->start.line, 5u);
}

TEST_CASE(ignored_step_pauses_preserve_the_active_step)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    vm->enable_debugging();
    auto breakpoint_id = MUST(vm->debugger()->add_breakpoint("ignored-step.js"_utf16, 4));

    Vector<JS::Debugger::PauseInfo> pauses;
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const& pause_info) {
        pauses.append(pause_info);
        if (pauses.size() == 1) {
            EXPECT(vm->debugger()->remove_breakpoint(breakpoint_id));
            vm->debugger()->continue_execution(JS::Debugger::ResumeMode::StepInto);
        } else if (pauses.size() == 2) {
            vm->debugger()->continue_execution_preserving_step_state();
        } else {
            vm->debugger()->continue_execution();
        }
    });

    auto script_or_error = JS::Script::parse(
        "function callee() {\n"
        "    let inside = 1;\n"
        "}\n"
        "callee();\n"
        "let after = 2;\n"sv,
        realm, "ignored-step.js"sv);
    VERIFY(!script_or_error.is_error());
    auto result = vm->run(*script_or_error.value());
    EXPECT(!result.is_error());

    EXPECT_EQ(pauses.size(), 3u);
    VERIFY(pauses.size() == 3);
    EXPECT_EQ(pauses[1].reason, JS::Debugger::PauseReason::Step);
    EXPECT_EQ(pauses[1].source_range->start.line, 2u);
    EXPECT_EQ(pauses[2].reason, JS::Debugger::PauseReason::Step);
    EXPECT_EQ(pauses[2].source_range->start.line, 5u);
}

TEST_CASE(step_state_does_not_survive_its_bytecode_execution)
{
    auto vm = JS::VM::create();
    auto root_execution_context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    auto& realm = *root_execution_context->realm;

    size_t pause_count = 0;
    vm->enable_debugging();
    vm->debugger()->set_pause_callback([&](JS::Debugger::PauseInfo const&) {
        ++pause_count;
        vm->debugger()->continue_execution(JS::Debugger::ResumeMode::StepInto);
    });

    auto first_script = JS::Script::parse("debugger;"sv, realm, "first.js"sv).release_value();
    EXPECT(!vm->run(*first_script).is_error());
    EXPECT_EQ(pause_count, 1u);

    auto second_script = JS::Script::parse("let later = 1;"sv, realm, "second.js"sv).release_value();
    EXPECT(!vm->run(*second_script).is_error());
    EXPECT_EQ(pause_count, 1u);
}
