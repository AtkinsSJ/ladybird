/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#![allow(dead_code)]
#![allow(unused_imports)]

mod line_editor;

use line_editor::LineEditor;
use serde_json::{Map, Value, json};
use std::collections::{HashMap, VecDeque};
use std::env;
use std::fmt::{self, Display};
use std::io::{Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver};
use std::thread;

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

static COLOR_ENABLED: AtomicBool = AtomicBool::new(false);

const RESET: &str = "\x1b[0m";
const OUTPUT_COLOR: &str = "\x1b[94m";
const ERROR_COLOR: &str = "\x1b[1;31m";

macro_rules! outputln {
    () => {
        println!()
    };
    ($($arg:tt)*) => {
        println!("{}", command_output(format_args!($($arg)*)))
    };
}

macro_rules! errorln {
    ($($arg:tt)*) => {
        eprintln!("{}", error_output(format_args!($($arg)*)))
    };
}

fn use_color() -> bool {
    COLOR_ENABLED.load(Ordering::Relaxed)
}

fn set_color_enabled(color: bool) {
    COLOR_ENABLED.store(color, Ordering::Relaxed);
}

fn paint(color: &str, text: impl fmt::Display) -> String {
    if use_color() {
        format!("{color}{text}{RESET}")
    } else {
        text.to_string()
    }
}

fn command_output(arguments: fmt::Arguments<'_>) -> String {
    paint(OUTPUT_COLOR, arguments)
}

fn error_output(arguments: fmt::Arguments<'_>) -> String {
    paint(ERROR_COLOR, arguments)
}

struct Options {
    port: u16,
    color: bool,
}

#[derive(Clone, Copy)]
enum Command {
    Attach,
    Box,
    CancelPick,
    Child,
    Children,
    Computed,
    Eval,
    Grid,
    Grids,
    Help,
    Highlight,
    HighlightGrid,
    Html,
    Next,
    OuterHtml,
    Parent,
    Pick,
    Previous,
    Query,
    Quit,
    Raw,
    Rules,
    Select,
    Selected,
    Source,
    Sources,
    Stylesheet,
    Stylesheets,
    Tabs,
    UnhighlightGrid,
}

struct CommandSpec {
    names: &'static [&'static str],
    command: Command,
}

const COMMANDS: &[CommandSpec] = &[
    CommandSpec {
        names: &["attach"],
        command: Command::Attach,
    },
    CommandSpec {
        names: &["box"],
        command: Command::Box,
    },
    CommandSpec {
        names: &["cancel-pick"],
        command: Command::CancelPick,
    },
    CommandSpec {
        names: &["child"],
        command: Command::Child,
    },
    CommandSpec {
        names: &["children"],
        command: Command::Children,
    },
    CommandSpec {
        names: &["computed"],
        command: Command::Computed,
    },
    CommandSpec {
        names: &["eval"],
        command: Command::Eval,
    },
    CommandSpec {
        names: &["grid"],
        command: Command::Grid,
    },
    CommandSpec {
        names: &["grids"],
        command: Command::Grids,
    },
    CommandSpec {
        names: &["help"],
        command: Command::Help,
    },
    CommandSpec {
        names: &["highlight"],
        command: Command::Highlight,
    },
    CommandSpec {
        names: &["highlight-grid"],
        command: Command::HighlightGrid,
    },
    CommandSpec {
        names: &["html"],
        command: Command::Html,
    },
    CommandSpec {
        names: &["next"],
        command: Command::Next,
    },
    CommandSpec {
        names: &["outer-html"],
        command: Command::OuterHtml,
    },
    CommandSpec {
        names: &["parent"],
        command: Command::Parent,
    },
    CommandSpec {
        names: &["pick"],
        command: Command::Pick,
    },
    CommandSpec {
        names: &["previous", "prev"],
        command: Command::Previous,
    },
    CommandSpec {
        names: &["query"],
        command: Command::Query,
    },
    CommandSpec {
        names: &["quit", "q", "exit"],
        command: Command::Quit,
    },
    CommandSpec {
        names: &["raw"],
        command: Command::Raw,
    },
    CommandSpec {
        names: &["rules"],
        command: Command::Rules,
    },
    CommandSpec {
        names: &["select"],
        command: Command::Select,
    },
    CommandSpec {
        names: &["selected"],
        command: Command::Selected,
    },
    CommandSpec {
        names: &["source"],
        command: Command::Source,
    },
    CommandSpec {
        names: &["sources"],
        command: Command::Sources,
    },
    CommandSpec {
        names: &["stylesheet"],
        command: Command::Stylesheet,
    },
    CommandSpec {
        names: &["stylesheets"],
        command: Command::Stylesheets,
    },
    CommandSpec {
        names: &["tabs"],
        command: Command::Tabs,
    },
    CommandSpec {
        names: &["unhighlight-grid"],
        command: Command::UnhighlightGrid,
    },
];

#[derive(Debug)]
struct ProtocolError {
    code: String,
    message: String,
}

impl Display for ProtocolError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for ProtocolError {}

#[derive(Default)]
struct Actors {
    tab: Option<String>,
    watcher: Option<String>,
    frame: Option<String>,
    console: Option<String>,
    thread: Option<String>,
    style_sheets: Option<String>,
    page_style: Option<String>,
    accessibility: Option<String>,
    accessibility_walker: Option<String>,
    inspector: Option<String>,
    walker: Option<String>,
    root_node: Option<String>,
    layout: Option<String>,
    highlighter: Option<String>,
    selection_highlighter: Option<String>,
    grid_highlighters: HashMap<String, String>,
    cookies: Option<String>,
    local_storage: Option<String>,
    session_storage: Option<String>,
    indexed_db: Option<String>,
}

struct SelectedNode {
    actor: String,
    label: String,
    node: Value,
}

struct DevToolsClient {
    stream: TcpStream,
    incoming_messages: Receiver<IncomingMessage>,
    pending_messages: VecDeque<Value>,
    pending_events: VecDeque<Value>,
    resources: HashMap<String, Vec<Value>>,
    known_nodes: HashMap<String, Value>,
    actors: Actors,
    selected_node: Option<SelectedNode>,
    tabs: Vec<Value>,
    sources: Vec<Value>,
    color: bool,
}

enum IncomingMessage {
    Message(Value),
    Error(String),
}

#[derive(Clone, Copy)]
enum EventDisplay {
    Defer,
    Print,
}

impl DevToolsClient {
    fn connect(options: Options) -> Result<Self> {
        let address = ("127.0.0.1", options.port)
            .to_socket_addrs()?
            .next()
            .ok_or("Unable to resolve DevTools address")?;
        let stream = TcpStream::connect(address)?;
        stream.set_nodelay(true)?;
        let incoming_messages = spawn_socket_reader(stream.try_clone()?);

        let mut client = Self {
            stream,
            incoming_messages,
            pending_messages: VecDeque::new(),
            pending_events: VecDeque::new(),
            resources: HashMap::new(),
            known_nodes: HashMap::new(),
            actors: Actors::default(),
            selected_node: None,
            tabs: Vec::new(),
            sources: Vec::new(),
            color: options.color,
        };

        let greeting = client.read_message()?;
        let application_type = greeting
            .get("applicationType")
            .and_then(Value::as_str)
            .unwrap_or("unknown");
        let traits = greeting
            .get("traits")
            .and_then(Value::as_object)
            .map_or(0, serde_json::Map::len);
        outputln!("Connected to DevTools ({application_type}, {traits} traits)");
        client.refresh_tabs()?;
        Ok(client)
    }

    fn update_tabs(&mut self) -> Result<()> {
        let selected_tab = self.actors.tab.clone();
        let response = self.request(json!({
            "to": "root",
            "type": "listTabs",
        }))?;

        self.tabs = response
            .get("tabs")
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default();

        let selected_tab_is_still_available = selected_tab.as_deref().is_some_and(|selected_tab| {
            self.tabs
                .iter()
                .filter_map(|tab| tab.get("actor").and_then(Value::as_str))
                .any(|actor| actor == selected_tab)
        });

        if selected_tab_is_still_available {
            self.actors.tab = selected_tab;
        } else {
            self.actors.tab = self
                .tabs
                .first()
                .and_then(|tab| tab.get("actor"))
                .and_then(Value::as_str)
                .map(String::from);
            self.actors.watcher = None;
            self.clear_frame_actors();
        }

        Ok(())
    }

    fn refresh_tabs(&mut self) -> Result<()> {
        self.update_tabs()?;
        print_tabs(&self.tabs);
        Ok(())
    }

    fn attach_tab(&mut self, index: usize) -> Result<()> {
        let tab = self.tabs.get(index).ok_or_else(|| format!("No tab at index {index}"))?;
        let actor = string_member(tab, "actor")?;

        self.actors.tab = Some(actor);
        self.actors.watcher = None;
        self.clear_frame_actors();
        self.ensure_target()?;

        if let Some(tab) = self.tabs.get(index) {
            outputln!("attached:");
            print_tab(index, tab);
        }

        Ok(())
    }

    fn request(&mut self, message: Value) -> Result<Value> {
        let response_actor = message.get("to").and_then(Value::as_str).unwrap_or("root").to_string();

        self.send_message(&message)?;

        loop {
            let response = self.read_message_from_socket()?;
            if let Some(error) = protocol_error(&response) {
                return Err(error.into());
            }

            if response.get("from").and_then(Value::as_str) == Some(response_actor.as_str())
                && !is_event_packet(&response)
            {
                self.observe_response_nodes(&response);
                return Ok(response);
            }

            self.handle_message(response, EventDisplay::Defer)?;
        }
    }

    fn request_for_selected_node(&mut self, message: Value) -> Result<Value> {
        let response = self.request(message);
        if response
            .as_ref()
            .err()
            .is_some_and(|error| is_unknown_actor_error(error.as_ref()))
        {
            self.clear_frame_actors();
            return Err("Selected node is no longer available".into());
        }

        response
    }

    fn request_for_frame_actor(&mut self, message: Value) -> Result<Value> {
        let response = self.request(message);
        if response
            .as_ref()
            .err()
            .is_some_and(|error| is_unknown_actor_error(error.as_ref()))
        {
            self.clear_frame_actors();
            return Err("Current frame is no longer available; retry the command".into());
        }

        response
    }

    fn read_message(&mut self) -> Result<Value> {
        if let Some(message) = self.pending_messages.pop_front() {
            return Ok(message);
        }

        self.read_message_from_socket()
    }

    fn read_message_from_socket(&mut self) -> Result<Value> {
        match self.incoming_messages.recv()? {
            IncomingMessage::Message(message) => Ok(message),
            IncomingMessage::Error(error) => Err(error.into()),
        }
    }

    fn send_message(&mut self, message: &Value) -> Result<()> {
        let serialized = serde_json::to_string(message)?;
        write!(self.stream, "{}:{serialized}", serialized.len())?;
        self.stream.flush()?;
        Ok(())
    }

    fn drain_events(&mut self) -> Result<()> {
        while let Some(message) = self.pending_events.pop_front() {
            self.print_event(&message);
        }

        self.drain_available_messages(EventDisplay::Print)?;
        Ok(())
    }

    fn drain_available_messages(&mut self, event_display: EventDisplay) -> Result<usize> {
        let mut count = 0;

        loop {
            match self.incoming_messages.try_recv() {
                Ok(IncomingMessage::Message(message)) => {
                    self.handle_message(message, event_display)?;
                    count += 1;
                }
                Ok(IncomingMessage::Error(error)) => return Err(error.into()),
                Err(mpsc::TryRecvError::Empty) => break,
                Err(mpsc::TryRecvError::Disconnected) => return Err("DevTools connection closed".into()),
            }
        }

        Ok(count)
    }

    fn handle_message(&mut self, message: Value, event_display: EventDisplay) -> Result<()> {
        self.observe_message(&message)?;

        if is_event_packet(&message) {
            match event_display {
                EventDisplay::Defer => self.pending_events.push_back(message),
                EventDisplay::Print => self.print_event(&message),
            }
        } else {
            self.pending_messages.push_back(message);
        }

        Ok(())
    }

    fn print_json(&self, value: &Value) {
        outputln!("{}", pretty_json(value, self.color));
    }

    fn print_event(&self, value: &Value) {
        match value.get("type").and_then(Value::as_str) {
            Some("pickerNodePicked") => {
                if let Some(node) = value.get("node").and_then(|node| node.get("node")) {
                    let label = node_label(node).unwrap_or_else(|| "node".to_string());
                    outputln!("picked: {label}");
                } else {
                    outputln!("picked node");
                }
            }
            Some("pickerNodeHovered") => {
                if let Some(node) = value.get("node").and_then(|node| node.get("node")) {
                    let label = node_label(node).unwrap_or_else(|| "node".to_string());
                    outputln!("hover: {label}");
                }
            }
            Some("pickerNodePreviewed") => {
                if let Some(node) = value.get("node").and_then(|node| node.get("node")) {
                    let label = node_label(node).unwrap_or_else(|| "node".to_string());
                    outputln!("preview: {label}");
                }
            }
            Some("pickerNodeCanceled") => outputln!("picker canceled"),
            Some("target-available-form") => {
                if let Some(target) = value.get("target") {
                    print_target(target);
                }
            }
            Some("target-destroyed-form") => outputln!("target destroyed"),
            Some("resources-available-array") => outputln!("resources available"),
            Some("resources-updated-array") => outputln!("resources updated"),
            Some("storesUpdate") => outputln!("storage updated"),
            Some("storesCleared") => outputln!("storage cleared"),
            Some("newMutations") => outputln!("DOM mutated"),
            Some(event_type) => outputln!("event: {event_type}"),
            None => self.print_json(value),
        }
    }

    fn ensure_tab(&mut self) -> Result<String> {
        if self.actors.tab.is_none() {
            self.refresh_tabs()?;
        }

        self.actors
            .tab
            .clone()
            .ok_or_else(|| "No tab actor is available".into())
    }

    fn ensure_watcher(&mut self) -> Result<String> {
        if let Some(watcher) = &self.actors.watcher {
            return Ok(watcher.clone());
        }

        let tab = self.ensure_tab()?;
        let response = self.request(json!({
            "to": tab,
            "type": "getWatcher",
        }))?;
        let watcher = string_member(&response, "actor")?;
        self.actors.watcher = Some(watcher.clone());
        Ok(watcher)
    }

    fn ensure_target(&mut self) -> Result<()> {
        if self.actors.frame.is_some() {
            return Ok(());
        }

        let watcher = self.ensure_watcher()?;
        self.request(json!({
            "to": watcher,
            "type": "watchTargets",
            "targetType": "frame",
        }))?;
        if self.actors.frame.is_some() {
            self.drain_events()?;
            return Ok(());
        }

        loop {
            let message = self.read_message()?;
            if message.get("type").and_then(Value::as_str) != Some("target-available-form") {
                self.handle_message(message, EventDisplay::Defer)?;
                continue;
            }

            let target = object_member(&message, "target")?;
            self.observe_message(&message)?;
            print_target(target);
            return Ok(());
        }
    }

    fn observe_message(&mut self, message: &Value) -> Result<()> {
        self.observe_response_nodes(message);

        match message.get("type").and_then(Value::as_str) {
            Some("target-available-form") => {
                if let Some(target) = message.get("target") {
                    self.observe_target_available(target);
                }
            }
            Some("target-destroyed-form")
                if message
                    .get("target")
                    .and_then(|target| target.get("actor"))
                    .and_then(Value::as_str)
                    == self.actors.frame.as_deref() =>
            {
                self.clear_frame_actors();
            }
            Some("resources-available-array") => {
                for (resource_type, resources) in resource_entries_from_message(message) {
                    if resource_type == "document-event"
                        && resources
                            .iter()
                            .any(|resource| resource.get("name").and_then(Value::as_str) == Some("will-navigate"))
                    {
                        self.clear_frame_actors();
                    }
                    self.resources.insert(resource_type, resources);
                }
            }
            Some("root-destroyed") => {
                self.clear_selected_node();
            }
            Some("pickerNodePicked") => {
                self.select_picker_node(message)?;
            }
            _ => {}
        }

        Ok(())
    }

    fn observe_response_nodes(&mut self, value: &Value) {
        match value {
            Value::Object(object) => {
                if object.contains_key("nodeType")
                    && let Some(actor) = object.get("actor").and_then(Value::as_str)
                {
                    self.known_nodes.insert(actor.to_string(), value.clone());
                }

                for value in object.values() {
                    self.observe_response_nodes(value);
                }
            }
            Value::Array(values) => {
                for value in values {
                    self.observe_response_nodes(value);
                }
            }
            _ => {}
        }
    }

    fn observe_target_available(&mut self, target: &Value) {
        self.clear_frame_actors();
        self.actors.frame = target.get("actor").and_then(Value::as_str).map(String::from);
        self.actors.console = target.get("consoleActor").and_then(Value::as_str).map(String::from);
        self.actors.inspector = target.get("inspectorActor").and_then(Value::as_str).map(String::from);
        self.actors.thread = target.get("threadActor").and_then(Value::as_str).map(String::from);
        self.actors.style_sheets = target.get("styleSheetsActor").and_then(Value::as_str).map(String::from);
        self.actors.accessibility = target
            .get("accessibilityActor")
            .and_then(Value::as_str)
            .map(String::from);
    }

    fn clear_frame_actors(&mut self) {
        self.actors.frame = None;
        self.actors.console = None;
        self.actors.thread = None;
        self.actors.style_sheets = None;
        self.actors.page_style = None;
        self.actors.accessibility = None;
        self.actors.accessibility_walker = None;
        self.actors.inspector = None;
        self.actors.walker = None;
        self.actors.root_node = None;
        self.actors.layout = None;
        self.actors.highlighter = None;
        self.actors.selection_highlighter = None;
        self.actors.grid_highlighters.clear();
        self.selected_node = None;
        self.known_nodes.clear();
        self.sources.clear();
        self.resources.clear();
    }

    fn ensure_target_actor(&mut self, actor: Option<String>, error: &'static str) -> Result<String> {
        self.ensure_target()?;
        actor.ok_or_else(|| error.into())
    }

    fn ensure_inspector(&mut self) -> Result<String> {
        self.ensure_target_actor(self.actors.inspector.clone(), "No inspector actor is available")
    }

    fn ensure_console(&mut self) -> Result<String> {
        self.ensure_target_actor(self.actors.console.clone(), "No console actor is available")
    }

    fn ensure_thread(&mut self) -> Result<String> {
        self.ensure_target_actor(self.actors.thread.clone(), "No thread actor is available")
    }

    fn ensure_style_sheets(&mut self) -> Result<String> {
        self.ensure_target_actor(self.actors.style_sheets.clone(), "No style sheets actor is available")
    }

    fn ensure_accessibility(&mut self) -> Result<String> {
        self.ensure_target_actor(self.actors.accessibility.clone(), "No accessibility actor is available")
    }

    fn ensure_accessibility_walker(&mut self) -> Result<String> {
        if let Some(walker) = &self.actors.accessibility_walker {
            return Ok(walker.clone());
        }

        let accessibility = self.ensure_accessibility()?;
        self.request_for_frame_actor(json!({
            "to": accessibility,
            "type": "bootstrap",
        }))?;
        let response = self.request_for_frame_actor(json!({
            "to": accessibility,
            "type": "getWalker",
        }))?;
        let walker = object_member(&response, "walker")?;
        let actor = string_member(walker, "actor")?;
        self.actors.accessibility_walker = Some(actor.clone());
        Ok(actor)
    }

    fn ensure_walker(&mut self) -> Result<String> {
        if let Some(walker) = &self.actors.walker {
            return Ok(walker.clone());
        }

        let inspector = self.ensure_inspector()?;
        let response = self.request_for_frame_actor(json!({
            "to": inspector,
            "type": "getWalker",
        }))?;
        let walker = object_member(&response, "walker")?;
        self.actors.walker = Some(string_member(walker, "actor")?);
        self.actors.root_node = object_member(walker, "root")
            .and_then(|root| string_member(root, "actor"))
            .ok();
        if let Some(root) = walker.get("root") {
            outputln!(
                "document: {}",
                node_label(root).unwrap_or_else(|| "#document".to_string())
            );
        }

        self.actors
            .walker
            .clone()
            .ok_or_else(|| "No walker actor is available".into())
    }

    fn ensure_root_node(&mut self) -> Result<String> {
        self.ensure_walker()?;
        self.actors
            .root_node
            .clone()
            .ok_or_else(|| "No root DOM node actor is available".into())
    }

    fn ensure_layout(&mut self) -> Result<String> {
        if let Some(layout) = &self.actors.layout {
            return Ok(layout.clone());
        }

        let walker = self.ensure_walker()?;
        let response = self.request_for_frame_actor(json!({
            "to": walker,
            "type": "getLayoutInspector",
        }))?;
        let actor = object_member(&response, "actor")?;
        let layout = string_member(actor, "actor")?;
        self.actors.layout = Some(layout.clone());
        Ok(layout)
    }

    fn ensure_page_style(&mut self) -> Result<String> {
        if let Some(page_style) = &self.actors.page_style {
            return Ok(page_style.clone());
        }

        let inspector = self.ensure_inspector()?;
        let response = self.request_for_frame_actor(json!({
            "to": inspector,
            "type": "getPageStyle",
        }))?;
        let style = object_member(&response, "pageStyle")?;
        let page_style = string_member(style, "actor")?;
        self.actors.page_style = Some(page_style.clone());
        Ok(page_style)
    }

    fn ensure_highlighter(&mut self, highlighter_type: &str) -> Result<String> {
        let actor = self.get_highlighter_by_type(highlighter_type)?;
        self.actors.highlighter = Some(actor.clone());
        Ok(actor)
    }

    fn ensure_selection_highlighter(&mut self) -> Result<String> {
        if let Some(highlighter) = &self.actors.selection_highlighter {
            return Ok(highlighter.clone());
        }

        let actor = self.get_highlighter_by_type("BoxModelHighlighter")?;
        self.actors.selection_highlighter = Some(actor.clone());
        Ok(actor)
    }

    fn ensure_grid_highlighter(&mut self, node: &str) -> Result<String> {
        if let Some(highlighter) = self.actors.grid_highlighters.get(node) {
            return Ok(highlighter.clone());
        }

        let actor = self.get_highlighter_by_type("CssGridHighlighter")?;
        self.actors.grid_highlighters.insert(node.to_string(), actor.clone());
        Ok(actor)
    }

    fn get_highlighter_by_type(&mut self, highlighter_type: &str) -> Result<String> {
        let inspector = self.ensure_inspector()?;
        let response = self.request_for_frame_actor(json!({
            "to": inspector,
            "type": "getHighlighterByType",
            "typeName": highlighter_type,
        }))?;
        let highlighter = object_member(&response, "highlighter")?;
        string_member(highlighter, "actor")
    }

    fn nodes_for_selector(&mut self, selector: &str) -> Result<Vec<Value>> {
        self.ensure_walker()?;
        if selector == ":root" || selector == "root" {
            let root = self.ensure_root_node()?;
            let Some(node) = self.known_nodes.get(&root).cloned() else {
                return Err("Root node is not cached".into());
            };
            return Ok(vec![node]);
        }

        let walker = self.ensure_walker()?;
        let root = self.ensure_root_node()?;
        let response = self.request_for_frame_actor(json!({
            "to": walker,
            "type": "querySelectorAll",
            "node": root,
            "selector": selector,
        }))?;
        let list = object_member(&response, "list")?;
        let actor = string_member(list, "actor")?;
        let length = list.get("length").and_then(Value::as_u64).unwrap_or_default();
        let response = self.request_for_frame_actor(json!({
            "to": actor,
            "type": "items",
            "start": 0,
            "end": length,
        }))?;
        self.request_for_frame_actor(json!({
            "to": actor,
            "type": "release",
        }))?;

        Ok(response
            .get("nodes")
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default())
    }

    fn selected_actor(&self) -> Result<String> {
        self.selected_node
            .as_ref()
            .map(|node| node.actor.clone())
            .ok_or_else(|| "No selected node; run `select <selector>` first".into())
    }

    fn selected_label(&self) -> String {
        self.selected_node
            .as_ref()
            .map(|node| node.label.clone())
            .unwrap_or_else(|| "selected node".to_string())
    }

    fn select_node(&mut self, node: Value) -> Result<()> {
        let actor = string_member(&node, "actor")?;
        let label = node_label(&node).unwrap_or_else(|| actor.clone());
        self.hide_selected_node()?;

        let highlighter = self.ensure_selection_highlighter()?;
        self.request_for_selected_node(json!({
            "to": highlighter,
            "type": "show",
            "node": actor,
        }))?;

        self.known_nodes.insert(actor.clone(), node.clone());
        self.selected_node = Some(SelectedNode { actor, label, node });
        Ok(())
    }

    fn hide_selected_node(&mut self) -> Result<()> {
        if self.selected_node.is_none() {
            return Ok(());
        }

        if let Some(highlighter) = self.actors.selection_highlighter.clone() {
            self.request_for_frame_actor(json!({
                "to": highlighter,
                "type": "hide",
            }))?;
        }

        self.selected_node = None;
        Ok(())
    }

    fn clear_selected_node(&mut self) {
        self.selected_node = None;
    }

    fn select_picker_node(&mut self, message: &Value) -> Result<()> {
        let disconnected_node = object_member(message, "node")?;
        let node = object_member(disconnected_node, "node")?;
        self.select_node(node.clone())
    }

    fn prompt(&self) -> String {
        let frame = self.actors.frame.as_deref().unwrap_or("-");
        let node = self
            .selected_node
            .as_ref()
            .map(|node| node.label.as_str())
            .unwrap_or("-");
        format!("devtools frame={frame} node={node}> ")
    }

    fn watch_resource(&mut self, resource_type: &str) -> Result<Value> {
        let watcher = self.ensure_watcher()?;
        self.request(json!({
            "to": watcher,
            "type": "watchResources",
            "resourceTypes": [resource_type],
        }))?;

        let resource = self.read_resource(resource_type)?;
        if let Ok(actor) = string_member(&resource, "actor") {
            match resource_type {
                "cookies" => self.actors.cookies = Some(actor),
                "local-storage" => self.actors.local_storage = Some(actor),
                "session-storage" => self.actors.session_storage = Some(actor),
                "indexed-db" => self.actors.indexed_db = Some(actor),
                _ => {}
            }
        }
        Ok(resource)
    }

    fn read_resource(&mut self, resource_type: &str) -> Result<Value> {
        loop {
            if let Some(resource) = self.take_pending_resource(resource_type) {
                return Ok(resource);
            }

            let message = self.read_message()?;
            self.handle_message(message, EventDisplay::Defer)?;
        }
    }

    fn take_pending_resource(&mut self, resource_type: &str) -> Option<Value> {
        self.resources
            .get(resource_type)
            .and_then(|resources| resources.first())
            .cloned()
    }

    fn resources_for_type(&self, resource_type: &str) -> Vec<Value> {
        self.resources.get(resource_type).cloned().unwrap_or_default()
    }
}

fn spawn_socket_reader(mut stream: TcpStream) -> Receiver<IncomingMessage> {
    let (sender, receiver) = mpsc::channel();
    thread::spawn(move || {
        loop {
            match read_message_from_socket(&mut stream) {
                Ok(message) => {
                    if sender.send(IncomingMessage::Message(message)).is_err() {
                        break;
                    }
                }
                Err(error) => {
                    let _ = sender.send(IncomingMessage::Error(error.to_string()));
                    break;
                }
            }
        }
    });

    receiver
}

fn read_message_from_socket(stream: &mut TcpStream) -> Result<Value> {
    let mut length = Vec::new();
    loop {
        let mut byte = [0_u8; 1];
        stream.read_exact(&mut byte)?;
        if byte[0] == b':' {
            break;
        }
        length.push(byte[0]);
    }

    let length = std::str::from_utf8(&length)?.parse::<usize>()?;
    let mut payload = vec![0_u8; length];
    stream.read_exact(&mut payload)?;

    Ok(serde_json::from_slice(&payload)?)
}

fn resource_entries_from_message(message: &Value) -> Vec<(String, Vec<Value>)> {
    if message.get("type").and_then(Value::as_str) != Some("resources-available-array") {
        return Vec::new();
    }

    let Some(array) = message.get("array").and_then(Value::as_array) else {
        return Vec::new();
    };
    let mut entries = Vec::new();

    for entry in array {
        let Some(entry) = entry.as_array() else {
            continue;
        };
        let Some(resource_type) = entry.first().and_then(Value::as_str) else {
            continue;
        };

        let Some(resources) = entry.get(1).and_then(Value::as_array) else {
            continue;
        };
        entries.push((resource_type.to_string(), resources.clone()));
    }

    entries
}

fn is_event_packet(packet: &Value) -> bool {
    let Some(packet_type) = packet.get("type").and_then(Value::as_str) else {
        return false;
    };

    matches!(
        packet_type,
        "frameUpdate"
            | "newMutations"
            | "pickerNodeCanceled"
            | "pickerNodeHovered"
            | "pickerNodePicked"
            | "pickerNodePreviewed"
            | "resources-available-array"
            | "resources-updated-array"
            | "root-available"
            | "root-destroyed"
            | "storesCleared"
            | "storesUpdate"
            | "tabListChanged"
            | "target-available-form"
            | "target-destroyed-form"
    )
}

fn protocol_error(packet: &Value) -> Option<ProtocolError> {
    let code = packet.get("error").and_then(Value::as_str)?;
    let message = packet
        .get("message")
        .and_then(Value::as_str)
        .unwrap_or("DevTools protocol error");
    Some(ProtocolError {
        code: code.to_string(),
        message: message.to_string(),
    })
}

fn is_unknown_actor_error(error: &(dyn std::error::Error + 'static)) -> bool {
    error
        .downcast_ref::<ProtocolError>()
        .is_some_and(|error| error.code == "unknownActor")
}

fn print_tab(index: usize, tab: &Value) {
    let actor = tab.get("actor").and_then(Value::as_str).unwrap_or("");
    let title = tab.get("title").and_then(Value::as_str).unwrap_or("");
    let url = tab.get("url").and_then(Value::as_str).unwrap_or("");
    outputln!("{index}: {actor} {title} {url}");
}

fn print_tabs(tabs: &[Value]) {
    if tabs.is_empty() {
        outputln!("No tabs");
        return;
    }

    for (index, tab) in tabs.iter().enumerate() {
        print_tab(index, tab);
    }
}

fn print_target(target: &Value) {
    let actor = target.get("actor").and_then(Value::as_str).unwrap_or("-");
    let title = target.get("title").and_then(Value::as_str).unwrap_or("");
    let url = target.get("url").and_then(Value::as_str).unwrap_or("");
    outputln!("target: {actor} {title} {url}");
}

fn node_actor(node: &Value) -> Option<&str> {
    node.get("actor").and_then(Value::as_str)
}

fn node_parent_actor(node: &Value) -> Option<&str> {
    node.get("parent").and_then(Value::as_str)
}

fn node_child_count(node: &Value) -> usize {
    node.get("numChildren")
        .and_then(Value::as_u64)
        .unwrap_or_default()
        .try_into()
        .unwrap_or_default()
}

fn attr_value<'a>(node: &'a Value, name: &str) -> Option<&'a str> {
    node.get("attrs").and_then(Value::as_array).and_then(|attrs| {
        attrs.iter().find_map(|attr| {
            if attr.get("name").and_then(Value::as_str) == Some(name) {
                attr.get("value").and_then(Value::as_str)
            } else {
                None
            }
        })
    })
}

fn node_attributes_summary(node: &Value) -> String {
    let Some(attrs) = node.get("attrs").and_then(Value::as_array) else {
        return String::new();
    };

    let mut parts = Vec::new();
    for attr in attrs {
        let Some(name) = attr.get("name").and_then(Value::as_str) else {
            continue;
        };
        let Some(value) = attr.get("value").and_then(Value::as_str) else {
            continue;
        };
        parts.push(format!("{name}=\"{value}\""));
    }

    if parts.is_empty() {
        String::new()
    } else {
        format!(" {}", parts.join(" "))
    }
}

fn print_node_summary(prefix: &str, node: &Value) {
    let label = node_label(node).unwrap_or_else(|| node_actor(node).unwrap_or("-").to_string());
    let actor = node_actor(node).unwrap_or("-");
    let child_count = node_child_count(node);
    let attributes = node_attributes_summary(node);
    outputln!("{prefix}{label}{attributes} ({actor}, {child_count} children)");
    if let Some(value) = node.get("nodeValue").and_then(Value::as_str)
        && !value.trim().is_empty()
    {
        outputln!("    {value}");
    }
}

fn print_node_list(nodes: &[Value]) {
    if nodes.is_empty() {
        outputln!("No child nodes");
        return;
    }

    for (index, node) in nodes.iter().enumerate() {
        print_node_summary(&format!("{index}: "), node);
    }
}

fn compact_json(value: &Value) -> String {
    serde_json::to_string(value).unwrap_or_else(|_| "<invalid json>".to_string())
}

fn pretty_json(value: &Value, color: bool) -> String {
    if color {
        let mut output = String::new();
        write_colored_json(value, 0, &mut output);
        return output;
    }

    serde_json::to_string_pretty(value).unwrap_or_else(|_| "<invalid json>".to_string())
}

fn write_colored_json(value: &Value, indent: usize, output: &mut String) {
    const RESET: &str = "\x1b[0m";
    const KEY: &str = "\x1b[36m";
    const STRING: &str = "\x1b[32m";
    const NUMBER: &str = "\x1b[35m";
    const LITERAL: &str = "\x1b[33m";
    const NULL: &str = "\x1b[90m";

    match value {
        Value::Null => output.push_str(&format!("{NULL}null{RESET}")),
        Value::Bool(value) => output.push_str(&format!("{LITERAL}{value}{RESET}")),
        Value::Number(value) => output.push_str(&format!("{NUMBER}{value}{RESET}")),
        Value::String(value) => {
            let escaped = serde_json::to_string(value).unwrap_or_else(|_| "\"<invalid string>\"".to_string());
            output.push_str(&format!("{STRING}{escaped}{RESET}"));
        }
        Value::Array(values) => {
            if values.is_empty() {
                output.push_str("[]");
                return;
            }

            output.push('[');
            for (index, value) in values.iter().enumerate() {
                output.push('\n');
                output.push_str(&" ".repeat(indent + 2));
                write_colored_json(value, indent + 2, output);
                if index + 1 != values.len() {
                    output.push(',');
                }
            }
            output.push('\n');
            output.push_str(&" ".repeat(indent));
            output.push(']');
        }
        Value::Object(object) => {
            if object.is_empty() {
                output.push_str("{}");
                return;
            }

            output.push('{');
            for (index, (key, value)) in object.iter().enumerate() {
                output.push('\n');
                output.push_str(&" ".repeat(indent + 2));
                let escaped_key = serde_json::to_string(key).unwrap_or_else(|_| "\"<invalid key>\"".to_string());
                output.push_str(&format!("{KEY}{escaped_key}{RESET}: "));
                write_colored_json(value, indent + 2, output);
                if index + 1 != object.len() {
                    output.push(',');
                }
            }
            output.push('\n');
            output.push_str(&" ".repeat(indent));
            output.push('}');
        }
    }
}

fn parse_options() -> Result<Options> {
    let mut args = env::args().skip(1);
    let mut port = None;
    let mut color = env::var_os("NO_COLOR").is_none() && env::var_os("DEVTOOLS_CLI_NO_COLOR").is_none();

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-p" | "--port" => {
                let Some(value) = args.next() else {
                    return Err("Missing value for --port".into());
                };
                port = Some(value.parse()?);
            }
            "--no-color" | "--no-colour" => {
                color = false;
            }
            "-h" | "--help" => {
                print_usage();
                std::process::exit(0);
            }
            _ => return Err(format!("Unrecognized argument: {arg}").into()),
        }
    }

    Ok(Options {
        port: port.unwrap_or(6000),
        color,
    })
}

fn command_for_name(name: &str) -> Option<Command> {
    COMMANDS
        .iter()
        .find(|spec| spec.names.contains(&name))
        .map(|spec| spec.command)
}

fn command_names() -> Vec<&'static str> {
    COMMANDS.iter().flat_map(|spec| spec.names.iter().copied()).collect()
}

fn prompt_for_index(prompt: &str, color: bool) -> Result<Option<usize>> {
    let mut editor = LineEditor::new(color);
    let Some(line) = editor.read_selection_line(prompt)? else {
        outputln!("canceled");
        return Ok(None);
    };

    let line = line.trim();
    if line.is_empty() {
        outputln!("canceled");
        return Ok(None);
    }

    Ok(Some(
        line.parse::<usize>()
            .map_err(|_| "selection expects a zero-based index")?,
    ))
}

fn print_usage() {
    outputln!("Usage: devtools-cli [--port PORT] [--no-color]");
}

fn print_help() {
    outputln!("Commands:");
    outputln!("  Session:");
    outputln!("    help                 Show this help");
    outputln!("    tabs                 Refresh and print the tab list");
    outputln!("    attach [index]       Attach to a tab");
    outputln!("    raw <json>           Send a raw protocol request and print the response");
    outputln!("    quit                 Exit");
    outputln!();
    outputln!("  DOM:");
    outputln!("    select <selector>    Select and highlight a DOM node");
    outputln!("    selected             Print the selected node");
    outputln!("    query <selector>     Print matching DOM nodes");
    outputln!("    children             List children of the selected node");
    outputln!("    child <index>        Select a child of the selected node");
    outputln!("    parent               Select the parent of the selected node");
    outputln!("    next                 Select the next sibling");
    outputln!("    previous             Select the previous sibling");
    outputln!("    html                 Print the selected node's inner HTML");
    outputln!("    outer-html           Print the selected node's outer HTML");
    outputln!("    highlight            Highlight the selected node");
    outputln!("    pick                 Start node picker mode");
    outputln!("    cancel-pick          Cancel node picker mode");
    outputln!();
    outputln!("  Style and box model:");
    outputln!("    computed [props...]  Print computed style, optionally filtered");
    outputln!("    rules [props...]     Print applied style rules, optionally filtered");
    outputln!("    box [props...]       Print box model data, optionally filtered");
    outputln!();
    outputln!("  Style sheets:");
    outputln!("    stylesheets          List style sheet resources");
    outputln!("    stylesheet <id>      Fetch style sheet text by resource id");
    outputln!();
    outputln!("  JavaScript sources:");
    outputln!("    sources              List JavaScript sources");
    outputln!("    source <index|actor> Fetch JavaScript source text");
    outputln!();
    outputln!("  Console:");
    outputln!("    eval <javascript>    Evaluate JavaScript in the page");
    outputln!();
    outputln!("  Grid layout:");
    outputln!("    grid                 Inspect the selected grid container");
    outputln!("    grids                List grid containers and select one");
    outputln!("    highlight-grid [opts] Highlight the selected grid container");
    outputln!("                         Options: --extend-lines --line-numbers");
    outputln!("                                  --area-names --track-sizes --color <value>");
    outputln!("    unhighlight-grid [--all] Hide selected or all grid highlights");
}

fn raw_request(client: &mut DevToolsClient, json_text: &str) -> Result<()> {
    let value = serde_json::from_str::<Value>(json_text)?;
    if !value.is_object() {
        return Err("raw expects a JSON object".into());
    }

    let response = client.request(value)?;
    client.print_json(&response);
    Ok(())
}

fn string_member(object: &Value, key: &str) -> Result<String> {
    object
        .get(key)
        .and_then(Value::as_str)
        .map(String::from)
        .ok_or_else(|| format!("Missing string member `{key}`").into())
}

fn object_member<'a>(object: &'a Value, key: &str) -> Result<&'a Value> {
    let value = object
        .get(key)
        .ok_or_else(|| format!("Missing object member `{key}`"))?;
    if !value.is_object() {
        return Err(format!("Member `{key}` is not an object").into());
    }
    Ok(value)
}

fn node_label(node: &Value) -> Option<String> {
    let name = node
        .get("displayName")
        .or_else(|| node.get("nodeName"))
        .and_then(Value::as_str)?;

    let id = attr_value(node, "id");
    let class = attr_value(node, "class");

    Some(match (id, class) {
        (Some(id), _) if !id.is_empty() => format!("{name}#{id}"),
        (_, Some(class)) if !class.is_empty() => {
            let class = class.split_whitespace().collect::<Vec<_>>().join(".");
            format!("{name}.{class}")
        }
        _ => name.to_string(),
    })
}

fn wildcard_matches(pattern: &str, text: &str) -> bool {
    let pattern = pattern.as_bytes();
    let text = text.as_bytes();
    let mut pattern_index = 0;
    let mut text_index = 0;
    let mut star_index = None;
    let mut star_text_index = 0;

    while text_index < text.len() {
        if pattern_index < pattern.len() && pattern[pattern_index] == text[text_index] {
            pattern_index += 1;
            text_index += 1;
        } else if pattern_index < pattern.len() && pattern[pattern_index] == b'*' {
            star_index = Some(pattern_index);
            pattern_index += 1;
            star_text_index = text_index;
        } else if let Some(previous_star_index) = star_index {
            pattern_index = previous_star_index + 1;
            star_text_index += 1;
            text_index = star_text_index;
        } else {
            return false;
        }
    }

    while pattern_index < pattern.len() && pattern[pattern_index] == b'*' {
        pattern_index += 1;
    }

    pattern_index == pattern.len()
}

fn filter_contains(filters: &[String], name: &str) -> bool {
    filters.is_empty() || filters.iter().any(|filter| wildcard_matches(filter, name))
}

fn split_filters(text: &str) -> Vec<String> {
    text.split_whitespace().map(String::from).collect()
}

fn css_property_value(value: &Value) -> Option<String> {
    if let Some(object) = value.as_object() {
        return object
            .get("value")
            .and_then(Value::as_str)
            .map(String::from)
            .or_else(|| object.get("value").map(compact_json));
    }

    value.as_str().map(String::from).or_else(|| Some(compact_json(value)))
}

fn print_property_block(title: &str, properties: &Value, filters: &[String]) {
    outputln!("{title}:");
    let Some(object) = properties.as_object() else {
        outputln!("    <no properties>");
        return;
    };

    let mut entries = object.iter().collect::<Vec<_>>();
    entries.sort_by_key(|(name, _)| *name);

    let mut printed = 0;
    for (name, value) in entries {
        if name == "from" {
            continue;
        }
        if !filter_contains(filters, name) {
            continue;
        }

        if let Some(value) = css_property_value(value) {
            outputln!("    {name}: {value};");
            printed += 1;
        }
    }

    if printed == 0 {
        outputln!("    <no matching properties>");
    }
}

fn declaration_name(declaration: &Value) -> Option<&str> {
    declaration.get("name").and_then(Value::as_str)
}

fn print_declaration(declaration: &Value) {
    let name = declaration_name(declaration).unwrap_or("<unknown>");
    let value = declaration.get("value").and_then(Value::as_str).unwrap_or("");
    let priority = declaration.get("priority").and_then(Value::as_str).unwrap_or("");
    let valid = declaration.get("isValid").and_then(Value::as_bool).unwrap_or(true);

    if priority.is_empty() {
        outputln!("        {name}: {value};{}", if valid { "" } else { " /* invalid */" });
    } else {
        outputln!(
            "        {name}: {value} !{priority};{}",
            if valid { "" } else { " /* invalid */" }
        );
    }
}

fn print_rule_header(entry: &Value) {
    let Some(rule) = entry.get("rule") else {
        outputln!("    <missing rule>");
        return;
    };

    let selectors = rule
        .get("selectors")
        .and_then(Value::as_array)
        .map(|selectors| {
            selectors
                .iter()
                .filter_map(Value::as_str)
                .collect::<Vec<_>>()
                .join(", ")
        })
        .filter(|selectors| !selectors.is_empty())
        .or_else(|| rule.get("authoredText").and_then(Value::as_str).map(String::from))
        .unwrap_or_else(|| "<inline style>".to_string());

    let system = if entry.get("isSystem").and_then(Value::as_bool).unwrap_or(false) {
        " [user-agent]"
    } else {
        ""
    };

    let source = match (
        rule.get("parentStyleSheet").and_then(Value::as_str),
        rule.get("line").and_then(Value::as_i64),
        rule.get("column").and_then(Value::as_i64),
    ) {
        (Some(sheet), Some(line), Some(column)) => format!(" ({sheet}:{line}:{column})"),
        _ => String::new(),
    };

    outputln!("    {selectors}{system}{source}");
}

fn print_applied_rules(title: &str, response: &Value, filters: &[String]) {
    outputln!("{title}:");
    let Some(entries) = response.get("entries").and_then(Value::as_array) else {
        outputln!("    <no applied rules>");
        return;
    };

    if entries.is_empty() {
        outputln!("    <no applied rules>");
        return;
    }

    for entry in entries {
        print_rule_header(entry);

        let declarations = entry
            .get("rule")
            .and_then(|rule| rule.get("declarations"))
            .and_then(Value::as_array);
        let Some(declarations) = declarations else {
            continue;
        };

        let mut printed = 0;
        for declaration in declarations {
            let Some(name) = declaration_name(declaration) else {
                continue;
            };
            if !filter_contains(filters, name) {
                continue;
            }
            print_declaration(declaration);
            printed += 1;
        }

        if printed == 0 && !filters.is_empty() {
            outputln!("        <no matching declarations>");
        }
    }
}

fn print_layout_object(name: &str, object: Option<&Value>) {
    let Some(object) = object else {
        outputln!("No {name}");
        return;
    };
    if object.is_null() {
        outputln!("No {name}");
        return;
    }

    outputln!("{name}:");
    let Some(fields) = object.as_object() else {
        outputln!("    {}", compact_json(object));
        return;
    };

    let mut entries = fields.iter().collect::<Vec<_>>();
    entries.sort_by_key(|(name, _)| *name);

    for (key, value) in entries {
        if value.is_array() || value.is_object() {
            let size = value
                .as_array()
                .map_or_else(|| value.as_object().map_or(0, |object| object.len()), Vec::len);
            outputln!("    {key}: {size} entries");
        } else if !value.is_null() {
            outputln!(
                "    {key}: {}",
                value.as_str().map_or_else(|| compact_json(value), String::from)
            );
        }
    }
}

fn print_evaluation_result(message: &Value) {
    if let Some(exception) = message.get("exception").filter(|exception| !exception.is_null()) {
        outputln!("exception: {}", compact_json(exception));
        return;
    }

    if let Some(result) = message.get("result") {
        if let Some(value) = result.get("value") {
            outputln!(
                "result: {}",
                value.as_str().map_or_else(|| compact_json(value), String::from)
            );
        } else {
            outputln!("result: {}", compact_json(result));
        }
    } else {
        outputln!("result: <empty>");
    }
}

fn ensure_no_arguments(arguments: &str, command: &str) -> Result<()> {
    if arguments.is_empty() {
        Ok(())
    } else {
        Err(format!("{command} does not take arguments; select a node first").into())
    }
}

fn selected_children(client: &mut DevToolsClient) -> Result<Vec<Value>> {
    let node = client.selected_actor()?;
    let walker = client.ensure_walker()?;
    let response = client.request_for_selected_node(json!({
        "to": walker,
        "type": "children",
        "node": node,
    }))?;
    Ok(response
        .get("nodes")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default())
}

fn print_selected_node(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    ensure_no_arguments(arguments, "selected")?;
    let Some(node) = client.selected_node.as_ref() else {
        return Err("No selected node; run `select <selector>` first".into());
    };

    print_node_summary("", &node.node);
    Ok(())
}

fn query_nodes(client: &mut DevToolsClient, selector: &str) -> Result<()> {
    if selector.is_empty() {
        return Err("query expects a selector".into());
    }

    let nodes = client.nodes_for_selector(selector)?;
    if nodes.is_empty() {
        outputln!("No matching nodes");
    } else {
        print_node_list(&nodes);
    }
    Ok(())
}

fn list_children(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    ensure_no_arguments(arguments, "children")?;
    let children = selected_children(client)?;
    print_node_list(&children);
    Ok(())
}

fn select_child(client: &mut DevToolsClient, index_text: &str) -> Result<()> {
    let index = index_text
        .parse::<usize>()
        .map_err(|_| "child expects a zero-based child index")?;
    let children = selected_children(client)?;
    let child = children
        .get(index)
        .cloned()
        .ok_or_else(|| format!("No child at index {index}"))?;
    client.select_node(child)?;
    outputln!("selected: {}", client.selected_label());
    Ok(())
}

fn select_parent(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    ensure_no_arguments(arguments, "parent")?;
    let Some(selected) = client.selected_node.as_ref() else {
        return Err("No selected node; run `select <selector>` first".into());
    };
    let parent_actor = node_parent_actor(&selected.node)
        .ok_or("Selected node does not have a parent")?
        .to_string();
    let parent = client
        .known_nodes
        .get(&parent_actor)
        .cloned()
        .unwrap_or_else(|| json!({ "actor": parent_actor, "displayName": parent_actor }));
    client.select_node(parent)?;
    outputln!("selected: {}", client.selected_label());
    Ok(())
}

fn select_sibling(client: &mut DevToolsClient, direction: isize) -> Result<()> {
    let Some(selected) = client.selected_node.as_ref() else {
        return Err("No selected node; run `select <selector>` first".into());
    };
    let current_actor = selected.actor.clone();
    let parent_actor = node_parent_actor(&selected.node)
        .ok_or("Selected node does not have a parent")?
        .to_string();
    let walker = client.ensure_walker()?;
    let response = client.request_for_selected_node(json!({
        "to": walker,
        "type": "children",
        "node": parent_actor,
    }))?;
    let siblings = response
        .get("nodes")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    let current_index = siblings
        .iter()
        .position(|node| node_actor(node) == Some(current_actor.as_str()))
        .ok_or("Selected node was not found in its parent")?;
    let target_index = current_index as isize + direction;
    if target_index < 0 || target_index >= siblings.len() as isize {
        return Err("No sibling in that direction".into());
    }
    client.select_node(siblings[target_index as usize].clone())?;
    outputln!("selected: {}", client.selected_label());
    Ok(())
}

fn node_html(client: &mut DevToolsClient, arguments: &str, command: &str, request_type: &str) -> Result<()> {
    ensure_no_arguments(arguments, command)?;
    let node = client.selected_actor()?;
    let walker = client.ensure_walker()?;
    let response = client.request_for_selected_node(json!({
        "to": walker,
        "type": request_type,
        "node": node,
    }))?;
    outputln!("{}", response.get("value").and_then(Value::as_str).unwrap_or(""));
    Ok(())
}

fn highlight_node(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    ensure_no_arguments(arguments, "highlight")?;
    let node = client.selected_actor()?;
    let highlighter = client.ensure_highlighter("BoxModelHighlighter")?;
    client.request_for_selected_node(json!({
        "to": highlighter,
        "type": "show",
        "node": node,
    }))?;
    outputln!("highlighted: {}", client.selected_label());
    Ok(())
}

fn parse_grid_highlight_options(arguments: &str) -> Result<Map<String, Value>> {
    let mut options = Map::new();
    let mut arguments = arguments.split_whitespace();

    while let Some(argument) = arguments.next() {
        if argument == "--color" {
            let Some(color) = arguments.next() else {
                return Err("highlight-grid --color requires a value".into());
            };
            options.insert("color".to_string(), Value::String(color.to_string()));
            continue;
        }

        let (name, value) = match argument {
            "--area-names" => ("showGridAreasOverlay", true),
            "--no-area-names" => ("showGridAreasOverlay", false),
            "--line-numbers" => ("showGridLineNumbers", true),
            "--no-line-numbers" => ("showGridLineNumbers", false),
            "--extend-lines" => ("showInfiniteLines", true),
            "--no-extend-lines" => ("showInfiniteLines", false),
            "--track-sizes" => ("showGridTrackSizes", true),
            "--no-track-sizes" => ("showGridTrackSizes", false),
            _ => return Err(format!("Unknown highlight-grid option: {argument}").into()),
        };

        options.insert(name.to_string(), Value::Bool(value));
    }

    Ok(options)
}

fn highlight_grid(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    let options = parse_grid_highlight_options(arguments)?;
    let node = client.selected_actor()?;
    let highlighter = client.ensure_grid_highlighter(&node)?;
    let mut request = json!({
        "to": highlighter,
        "type": "show",
        "node": node,
    });
    if !options.is_empty() {
        request["options"] = Value::Object(options);
    }
    client.request_for_selected_node(request)?;
    outputln!("highlighted grid: {}", client.selected_label());
    Ok(())
}

fn release_grid_highlighter(client: &mut DevToolsClient, highlighter: String) -> Result<()> {
    client.request_for_frame_actor(json!({
        "to": highlighter,
        "type": "release",
    }))?;
    Ok(())
}

fn unhighlight_grid(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    if arguments == "--all" {
        if client.actors.grid_highlighters.is_empty() {
            outputln!("No grid highlights");
            return Ok(());
        }

        let highlighters = std::mem::take(&mut client.actors.grid_highlighters);
        let count = highlighters.len();
        for highlighter in highlighters.into_values() {
            release_grid_highlighter(client, highlighter)?;
        }
        outputln!("hidden {count} grid highlight(s)");
        return Ok(());
    }

    if !arguments.is_empty() {
        return Err("unhighlight-grid only accepts --all".into());
    }

    let node = client.selected_actor()?;
    let Some(highlighter) = client.actors.grid_highlighters.remove(&node) else {
        outputln!("No grid highlight for {}", client.selected_label());
        return Ok(());
    };

    release_grid_highlighter(client, highlighter)?;
    outputln!("grid highlight hidden: {}", client.selected_label());
    Ok(())
}

fn inspect_grid(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    ensure_no_arguments(arguments, "grid")?;
    let layout = client.ensure_layout()?;
    let node = client.selected_actor()?;
    let response = client.request_for_selected_node(json!({
        "to": layout,
        "type": "getCurrentGrid",
        "node": node,
    }))?;
    print_layout_object("grid", response.get("grid"));
    Ok(())
}

fn grid_container_node(client: &mut DevToolsClient, grid: &Value) -> Result<Value> {
    if let Some(container_node_actor) = grid.get("containerNodeActorID").and_then(Value::as_str)
        && let Some(node) = client.known_nodes.get(container_node_actor)
    {
        return Ok(node.clone());
    }

    let walker = client.ensure_walker()?;
    let grid_actor = string_member(grid, "actor")?;
    let response = client.request_for_frame_actor(json!({
        "to": walker,
        "type": "getNodeFromActor",
        "actorID": grid_actor,
        "path": ["containerEl"],
    }))?;
    let disconnected_node = object_member(&response, "node")?;
    Ok(object_member(disconnected_node, "node")?.clone())
}

fn list_grids(client: &mut DevToolsClient, arguments: &str) -> Result<Vec<Value>> {
    ensure_no_arguments(arguments, "grids")?;
    let layout = client.ensure_layout()?;
    let root = client.ensure_root_node()?;
    let response = client.request_for_frame_actor(json!({
        "to": layout,
        "type": "getGrids",
        "rootNode": root,
    }))?;
    let grids = response
        .get("grids")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    if grids.is_empty() {
        outputln!("No grids");
    } else {
        for (index, grid) in grids.iter().enumerate() {
            outputln!("{index}:");
            print_layout_object("grid", Some(grid));
        }
    }
    Ok(grids)
}

fn select_grid(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    let grids = list_grids(client, arguments)?;
    if grids.is_empty() {
        return Ok(());
    }

    let grid = if grids.len() == 1 {
        &grids[0]
    } else {
        let Some(index) = prompt_for_index("grid index> ", client.color)? else {
            return Ok(());
        };
        grids.get(index).ok_or_else(|| format!("No grid at index {index}"))?
    };

    let node = grid_container_node(client, grid)?;
    client.select_node(node)?;
    outputln!("selected: {}", client.selected_label());
    Ok(())
}

fn picker_command(client: &mut DevToolsClient, request_type: &str) -> Result<()> {
    let walker = client.ensure_walker()?;
    client.request_for_frame_actor(json!({
        "to": walker,
        "type": request_type,
    }))?;

    if request_type != "pick" {
        outputln!("picker canceled");
        return Ok(());
    }

    outputln!("Picker active. Click a node in Ladybird.");
    loop {
        let message = client.read_message()?;
        let packet_type = message.get("type").and_then(Value::as_str).map(String::from);
        client.handle_message(message, EventDisplay::Print)?;
        if let Some("pickerNodePicked" | "pickerNodeCanceled") = packet_type.as_deref() {
            return Ok(());
        }
    }
}

fn style_command(client: &mut DevToolsClient, arguments: &str, request_type: &str) -> Result<()> {
    let filters = split_filters(arguments);
    let node = client.selected_actor()?;
    let page_style = client.ensure_page_style()?;
    let mut request = json!({
        "to": page_style,
        "type": request_type,
        "node": node,
    });

    if request_type == "getApplied" {
        request["inherited"] = json!(true);
        request["matchedSelectors"] = json!(true);
    }

    let response = client.request_for_selected_node(request)?;
    match request_type {
        "getComputed" => {
            let computed = response
                .get("computed")
                .ok_or("Computed style response is missing `computed`")?;
            print_property_block(&client.selected_label(), computed, &filters);
        }
        "getLayout" => print_property_block(&client.selected_label(), &response, &filters),
        "getApplied" => print_applied_rules(&client.selected_label(), &response, &filters),
        _ => client.print_json(&response),
    }
    Ok(())
}

fn select_node(client: &mut DevToolsClient, selector: &str) -> Result<()> {
    if selector.is_empty() {
        return Err("select expects a selector".into());
    }

    let nodes = client.nodes_for_selector(selector)?;
    if nodes.is_empty() {
        return Err(format!("No nodes match `{selector}`").into());
    }

    let node = if nodes.len() == 1 {
        nodes[0].clone()
    } else {
        print_node_list(&nodes);
        let Some(index) = prompt_for_index("select index> ", client.color)? else {
            return Ok(());
        };
        nodes
            .get(index)
            .cloned()
            .ok_or_else(|| format!("No matching node at index {index}"))?
    };

    client.select_node(node)?;
    outputln!("selected: {}", client.selected_label());
    Ok(())
}

fn attach_command(client: &mut DevToolsClient, arguments: &str) -> Result<()> {
    client.update_tabs()?;

    let index = if arguments.is_empty() {
        print_tabs(&client.tabs);
        let Some(index) = prompt_for_index("tab index> ", client.color)? else {
            return Ok(());
        };
        index
    } else {
        arguments
            .parse::<usize>()
            .map_err(|_| "attach expects a zero-based tab index")?
    };

    client.attach_tab(index)
}

fn stylesheet_text(client: &mut DevToolsClient, resource_id: &str) -> Result<()> {
    let style_sheets = client.ensure_style_sheets()?;
    let response = client.request_for_frame_actor(json!({
        "to": style_sheets,
        "type": "getText",
        "resourceId": resource_id,
    }))?;
    outputln!("{}", response.get("text").and_then(Value::as_str).unwrap_or(""));
    Ok(())
}

fn list_stylesheets(client: &mut DevToolsClient) -> Result<()> {
    client.ensure_style_sheets()?;
    if client.resources_for_type("stylesheet").is_empty() {
        client.read_resource("stylesheet")?;
    }

    let style_sheets = client.resources_for_type("stylesheet");
    if style_sheets.is_empty() {
        outputln!("No stylesheets");
        return Ok(());
    }

    for style_sheet in style_sheets {
        let id = style_sheet
            .get("resourceId")
            .or_else(|| style_sheet.get("styleSheetId"))
            .and_then(Value::as_str)
            .unwrap_or("-");
        let url = style_sheet
            .get("href")
            .or_else(|| style_sheet.get("url"))
            .and_then(Value::as_str)
            .unwrap_or("<inline>");
        outputln!("{id}: {url}");
    }
    Ok(())
}

fn list_sources(client: &mut DevToolsClient) -> Result<()> {
    let thread = client.ensure_thread()?;
    let response = client.request_for_frame_actor(json!({
        "to": thread,
        "type": "sources",
    }))?;
    client.sources = response
        .get("sources")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    if client.sources.is_empty() {
        outputln!("No JavaScript sources");
    } else {
        for (index, source) in client.sources.iter().enumerate() {
            let actor = source.get("actor").and_then(Value::as_str).unwrap_or("-");
            let url = source.get("url").and_then(Value::as_str).unwrap_or("<inline>");
            let length = source.get("sourceLength").and_then(Value::as_i64).unwrap_or_default();
            outputln!("{index}: {actor} {url} ({length} bytes)");
        }
    }
    Ok(())
}

fn source_text(client: &mut DevToolsClient, id: &str) -> Result<()> {
    let actor = if let Ok(index) = id.parse::<usize>() {
        client
            .sources
            .get(index)
            .and_then(|source| source.get("actor"))
            .and_then(Value::as_str)
            .map(String::from)
            .ok_or_else(|| format!("No source at index {index}"))?
    } else {
        id.to_string()
    };

    let response = client.request_for_frame_actor(json!({
        "to": actor,
        "type": "source",
    }))?;
    outputln!("{}", response.get("source").and_then(Value::as_str).unwrap_or(""));
    Ok(())
}

fn evaluate_javascript(client: &mut DevToolsClient, text: &str) -> Result<()> {
    let console = client.ensure_console()?;
    let response = client.request_for_frame_actor(json!({
        "to": console,
        "type": "evaluateJSAsync",
        "text": text,
    }))?;
    let result_id = string_member(&response, "resultID")?;
    outputln!("evaluation started: {result_id}");

    loop {
        let message = client.read_message()?;
        if message.get("from").and_then(Value::as_str) == Some(console.as_str())
            && message.get("type").and_then(Value::as_str) == Some("evaluationResult")
            && message.get("resultID").and_then(Value::as_str) == Some(result_id.as_str())
        {
            print_evaluation_result(&message);
            return Ok(());
        }
        client.handle_message(message, EventDisplay::Defer)?;
    }
}

fn run_repl(mut client: DevToolsClient) -> Result<()> {
    print_help();
    let command_names = command_names();
    let mut editor = LineEditor::new(client.color);

    loop {
        let Some(line) = editor.read_command_line(&client.prompt(), &command_names)? else {
            break;
        };

        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        let mut parts = line.splitn(2, char::is_whitespace);
        let command = parts.next().unwrap_or_default();
        let rest = parts.next().unwrap_or_default().trim();

        client.drain_available_messages(EventDisplay::Defer)?;

        let result = match command_for_name(command) {
            Some(Command::Help) => {
                print_help();
                Ok(())
            }
            Some(Command::Tabs) => client.refresh_tabs(),
            Some(Command::Attach) => attach_command(&mut client, rest),
            Some(Command::Select) => select_node(&mut client, rest),
            Some(Command::Selected) => print_selected_node(&mut client, rest),
            Some(Command::Query) => query_nodes(&mut client, rest),
            Some(Command::Children) => list_children(&mut client, rest),
            Some(Command::Child) => select_child(&mut client, rest),
            Some(Command::Parent) => select_parent(&mut client, rest),
            Some(Command::Next) => {
                ensure_no_arguments(rest, "next")?;
                select_sibling(&mut client, 1)
            }
            Some(Command::Previous) => {
                ensure_no_arguments(rest, "previous")?;
                select_sibling(&mut client, -1)
            }
            Some(Command::Html) => node_html(&mut client, rest, "html", "innerHTML"),
            Some(Command::OuterHtml) => node_html(&mut client, rest, "outer-html", "outerHTML"),
            Some(Command::Highlight) => highlight_node(&mut client, rest),
            Some(Command::Pick) => picker_command(&mut client, "pick"),
            Some(Command::CancelPick) => picker_command(&mut client, "cancelPick"),
            Some(Command::Computed) => style_command(&mut client, rest, "getComputed"),
            Some(Command::Rules) => style_command(&mut client, rest, "getApplied"),
            Some(Command::Box) => style_command(&mut client, rest, "getLayout"),
            Some(Command::Stylesheets) => list_stylesheets(&mut client),
            Some(Command::Stylesheet) => stylesheet_text(&mut client, rest),
            Some(Command::Sources) => list_sources(&mut client),
            Some(Command::Source) => source_text(&mut client, rest),
            Some(Command::Eval) => evaluate_javascript(&mut client, rest),
            Some(Command::Grid) => inspect_grid(&mut client, rest),
            Some(Command::Grids) => select_grid(&mut client, rest),
            Some(Command::HighlightGrid) => highlight_grid(&mut client, rest),
            Some(Command::UnhighlightGrid) => unhighlight_grid(&mut client, rest),
            Some(Command::Raw) => raw_request(&mut client, rest),
            Some(Command::Quit) => break,
            None => Err(format!("Unknown command: {command}").into()),
        };

        if let Err(error) = result {
            errorln!("error: {error}");
        }

        client.drain_available_messages(EventDisplay::Print)?;
    }

    Ok(())
}

fn main() -> Result<()> {
    let options = parse_options()?;
    set_color_enabled(options.color);
    let client = DevToolsClient::connect(options)?;
    run_repl(client)
}
