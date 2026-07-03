/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use crate::Result;
use std::io::{self, IsTerminal, Read, Write};
#[cfg(unix)]
use std::os::fd::AsRawFd;

const RESET: &str = "\x1b[0m";
const PROMPT_COLOR: &str = "\x1b[1;36m";
const INPUT_COLOR: &str = "\x1b[1;37m";

enum ReadLineResult {
    Line(String),
    Canceled,
    Interrupted,
}

pub(crate) struct LineEditor {
    color: bool,
    history: Vec<String>,
    history_index: Option<usize>,
}

impl LineEditor {
    pub(crate) fn new(color: bool) -> Self {
        Self {
            color,
            history: Vec::new(),
            history_index: None,
        }
    }

    pub(crate) fn read_command_line(&mut self, prompt: &str, command_names: &[&str]) -> Result<Option<String>> {
        match self.read_line(prompt, LineMode::Command, command_names)? {
            ReadLineResult::Line(line) => {
                if !line.is_empty() {
                    self.history.push(line.clone());
                }
                Ok(Some(line))
            }
            ReadLineResult::Interrupted => Ok(None),
            ReadLineResult::Canceled => Ok(Some(String::new())),
        }
    }

    pub(crate) fn read_selection_line(&mut self, prompt: &str) -> Result<Option<String>> {
        match self.read_line(prompt, LineMode::Selection, &[])? {
            ReadLineResult::Line(line) => Ok(Some(line)),
            ReadLineResult::Canceled | ReadLineResult::Interrupted => Ok(None),
        }
    }

    fn read_line(&mut self, prompt: &str, mode: LineMode, command_names: &[&str]) -> Result<ReadLineResult> {
        if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
            let mut line = String::new();
            let bytes_read = io::stdin().read_line(&mut line)?;
            if bytes_read == 0 {
                return Ok(ReadLineResult::Interrupted);
            }
            return Ok(ReadLineResult::Line(line.trim_end_matches(['\n', '\r']).to_string()));
        }

        let _terminal_mode = TerminalModeGuard::enable()?;
        let mut input = io::stdin();
        let mut line = String::new();
        let mut cursor = 0;
        let mut saved_line = String::new();
        self.history_index = None;
        redraw_line(prompt, &line, cursor, self.color)?;

        loop {
            let key = read_key(&mut input)?;
            match key {
                Key::Char(character) => {
                    line.insert(cursor, character);
                    cursor += character.len_utf8();
                }
                Key::Enter => {
                    write!(io::stdout(), "\r\n")?;
                    io::stdout().flush()?;
                    return Ok(ReadLineResult::Line(line));
                }
                Key::Backspace => {
                    if let Some(previous) = previous_boundary(&line, cursor) {
                        line.replace_range(previous..cursor, "");
                        cursor = previous;
                    }
                }
                Key::Delete => {
                    if let Some(next) = next_boundary(&line, cursor) {
                        line.replace_range(cursor..next, "");
                    }
                }
                Key::Left => {
                    if let Some(previous) = previous_boundary(&line, cursor) {
                        cursor = previous;
                    }
                }
                Key::Right => {
                    if let Some(next) = next_boundary(&line, cursor) {
                        cursor = next;
                    }
                }
                Key::Home => cursor = 0,
                Key::End => cursor = line.len(),
                Key::Up if mode == LineMode::Command => {
                    if self.history.is_empty() {
                        redraw_line(prompt, &line, cursor, self.color)?;
                        continue;
                    }

                    match self.history_index {
                        Some(0) => {}
                        Some(index) => self.history_index = Some(index - 1),
                        None => {
                            saved_line = line.clone();
                            self.history_index = Some(self.history.len() - 1);
                        }
                    }

                    if let Some(index) = self.history_index {
                        line = self.history[index].clone();
                        cursor = line.len();
                    }
                }
                Key::Down if mode == LineMode::Command => {
                    let Some(index) = self.history_index else {
                        redraw_line(prompt, &line, cursor, self.color)?;
                        continue;
                    };

                    if index + 1 < self.history.len() {
                        self.history_index = Some(index + 1);
                        line = self.history[index + 1].clone();
                    } else {
                        self.history_index = None;
                        line = saved_line.clone();
                    }
                    cursor = line.len();
                }
                Key::Tab if mode == LineMode::Command => {
                    complete_command(prompt, &mut line, &mut cursor, self.color, command_names)?;
                }
                Key::CtrlC => {
                    if mode == LineMode::Selection {
                        write!(io::stdout(), "\r\n")?;
                        io::stdout().flush()?;
                        return Ok(ReadLineResult::Canceled);
                    }
                    if line.is_empty() {
                        write!(io::stdout(), "\r\n")?;
                        io::stdout().flush()?;
                        return Ok(ReadLineResult::Interrupted);
                    }
                    line.clear();
                    cursor = 0;
                }
                Key::Escape if mode == LineMode::Selection => {
                    write!(io::stdout(), "\r\n")?;
                    io::stdout().flush()?;
                    return Ok(ReadLineResult::Canceled);
                }
                Key::CtrlD if line.is_empty() => {
                    write!(io::stdout(), "\r\n")?;
                    io::stdout().flush()?;
                    return Ok(ReadLineResult::Interrupted);
                }
                Key::Unknown | Key::Up | Key::Down | Key::Tab | Key::Escape | Key::CtrlD => {}
            }
            redraw_line(prompt, &line, cursor, self.color)?;
        }
    }
}

#[derive(PartialEq)]
enum LineMode {
    Command,
    Selection,
}

enum Key {
    Backspace,
    Char(char),
    CtrlC,
    CtrlD,
    Delete,
    Down,
    End,
    Enter,
    Escape,
    Home,
    Left,
    Right,
    Tab,
    Unknown,
    Up,
}

#[cfg(unix)]
struct TerminalModeGuard {
    fd: i32,
    original: libc::termios,
}

#[cfg(unix)]
impl TerminalModeGuard {
    fn enable() -> io::Result<Self> {
        let fd = io::stdin().as_raw_fd();
        let mut original = unsafe { std::mem::zeroed::<libc::termios>() };
        if unsafe { libc::tcgetattr(fd, &raw mut original) } != 0 {
            return Err(io::Error::last_os_error());
        }

        let mut raw = original;
        raw.c_iflag &= !(libc::BRKINT | libc::ICRNL | libc::INPCK | libc::ISTRIP | libc::IXON);
        raw.c_oflag &= !libc::OPOST;
        raw.c_cflag |= libc::CS8;
        raw.c_lflag &= !(libc::ECHO | libc::ICANON | libc::IEXTEN | libc::ISIG);
        raw.c_cc[libc::VMIN] = 1;
        raw.c_cc[libc::VTIME] = 0;

        if unsafe { libc::tcsetattr(fd, libc::TCSAFLUSH, &raw const raw) } != 0 {
            return Err(io::Error::last_os_error());
        }

        Ok(Self { fd, original })
    }
}

#[cfg(unix)]
impl Drop for TerminalModeGuard {
    fn drop(&mut self) {
        let _ = unsafe { libc::tcsetattr(self.fd, libc::TCSAFLUSH, &raw const self.original) };
    }
}

#[cfg(not(unix))]
struct TerminalModeGuard;

#[cfg(not(unix))]
impl TerminalModeGuard {
    fn enable() -> io::Result<Self> {
        Ok(Self)
    }
}

fn read_key(input: &mut io::Stdin) -> Result<Key> {
    let mut byte = [0_u8; 1];
    input.read_exact(&mut byte)?;
    match byte[0] {
        b'\r' | b'\n' => Ok(Key::Enter),
        b'\t' => Ok(Key::Tab),
        0x03 => Ok(Key::CtrlC),
        0x04 => Ok(Key::CtrlD),
        0x7f | 0x08 => Ok(Key::Backspace),
        0x1b => read_escape_sequence(input),
        byte if byte.is_ascii_control() => Ok(Key::Unknown),
        byte if byte.is_ascii() => Ok(Key::Char(byte as char)),
        byte => read_utf8_character(input, byte).map(Key::Char),
    }
}

fn read_escape_sequence(input: &mut io::Stdin) -> Result<Key> {
    let mut byte = [0_u8; 1];
    if !escape_sequence_byte_is_available(input)? || input.read(&mut byte)? == 0 || byte[0] != b'[' {
        return Ok(Key::Escape);
    }

    input.read_exact(&mut byte)?;
    match byte[0] {
        b'A' => Ok(Key::Up),
        b'B' => Ok(Key::Down),
        b'C' => Ok(Key::Right),
        b'D' => Ok(Key::Left),
        b'F' => Ok(Key::End),
        b'H' => Ok(Key::Home),
        b'1' | b'3' | b'4' | b'7' | b'8' => {
            let prefix = byte[0];
            input.read_exact(&mut byte)?;
            if byte[0] != b'~' {
                return Ok(Key::Unknown);
            }
            match prefix {
                b'1' | b'7' => Ok(Key::Home),
                b'3' => Ok(Key::Delete),
                b'4' | b'8' => Ok(Key::End),
                _ => Ok(Key::Unknown),
            }
        }
        _ => Ok(Key::Unknown),
    }
}

#[cfg(unix)]
fn escape_sequence_byte_is_available(input: &io::Stdin) -> io::Result<bool> {
    let mut poll_fd = libc::pollfd {
        fd: input.as_raw_fd(),
        events: libc::POLLIN,
        revents: 0,
    };
    let result = unsafe { libc::poll(&raw mut poll_fd, 1, 20) };
    if result < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(result > 0 && poll_fd.revents & libc::POLLIN != 0)
}

#[cfg(not(unix))]
fn escape_sequence_byte_is_available(_input: &io::Stdin) -> io::Result<bool> {
    Ok(true)
}

fn read_utf8_character(input: &mut impl Read, first_byte: u8) -> Result<char> {
    let width = if first_byte & 0b1110_0000 == 0b1100_0000 {
        2
    } else if first_byte & 0b1111_0000 == 0b1110_0000 {
        3
    } else if first_byte & 0b1111_1000 == 0b1111_0000 {
        4
    } else {
        return Err("Invalid UTF-8 input".into());
    };

    let mut bytes = [0_u8; 4];
    bytes[0] = first_byte;
    input.read_exact(&mut bytes[1..width])?;
    let text = std::str::from_utf8(&bytes[..width])?;
    text.chars().next().ok_or_else(|| "Invalid UTF-8 input".into())
}

fn previous_boundary(text: &str, cursor: usize) -> Option<usize> {
    if cursor == 0 {
        return None;
    }
    text[..cursor].char_indices().last().map(|(index, _)| index)
}

fn next_boundary(text: &str, cursor: usize) -> Option<usize> {
    if cursor == text.len() {
        return None;
    }
    text[cursor..]
        .char_indices()
        .nth(1)
        .map_or(Some(text.len()), |(index, _)| Some(cursor + index))
}

fn display_width(text: &str) -> usize {
    text.chars().count()
}

fn redraw_line(prompt: &str, line: &str, cursor: usize, color: bool) -> Result<()> {
    let prompt = if color {
        format!("{PROMPT_COLOR}{prompt}{RESET}")
    } else {
        prompt.to_string()
    };
    let line_text = if color && !line.is_empty() {
        format!("{INPUT_COLOR}{line}{RESET}")
    } else {
        line.to_string()
    };
    let cursor_from_end = display_width(&line[cursor..]);

    write!(io::stdout(), "\r\x1b[2K{prompt}{line_text}")?;
    if cursor_from_end > 0 {
        write!(io::stdout(), "\x1b[{cursor_from_end}D")?;
    }
    io::stdout().flush()?;
    Ok(())
}

fn complete_command(
    prompt: &str,
    line: &mut String,
    cursor: &mut usize,
    color: bool,
    command_names: &[&str],
) -> Result<()> {
    let prefix_end = line[..*cursor].find(char::is_whitespace).unwrap_or(*cursor);
    if *cursor > prefix_end {
        return Ok(());
    }

    let prefix = &line[..*cursor];
    let matches = command_names
        .iter()
        .copied()
        .filter(|command| command.starts_with(prefix))
        .collect::<Vec<_>>();

    match matches.as_slice() {
        [] => {}
        [command] => {
            line.replace_range(0..prefix_end, command);
            *cursor = command.len();
            if line.len() == command.len() {
                line.push(' ');
                *cursor += 1;
            }
        }
        _ => {
            let common_prefix = common_prefix(&matches);
            if common_prefix.len() > prefix.len() {
                line.replace_range(0..prefix_end, &common_prefix);
                *cursor = common_prefix.len();
            } else {
                write!(io::stdout(), "\r\n{}\r\n", matches.join("  "))?;
                redraw_line(prompt, line, *cursor, color)?;
            }
        }
    }

    Ok(())
}

fn common_prefix(values: &[&str]) -> String {
    let Some(first) = values.first() else {
        return String::new();
    };
    let mut prefix = (*first).to_string();
    for value in &values[1..] {
        while !value.starts_with(&prefix) {
            let Some(previous) = previous_boundary(&prefix, prefix.len()) else {
                return String::new();
            };
            prefix.truncate(previous);
        }
    }
    prefix
}
