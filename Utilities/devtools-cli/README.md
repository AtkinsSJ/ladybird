# devtools-cli

`devtools-cli` is a command-line DevTools client for diagnosing Ladybird pages.
It connects to a Ladybird DevTools server and provides a debugger-like REPL.

## Build

```bash
Meta/ladybird.py run devtools-cli -- --help
```

## Start Ladybird With DevTools

```bash
Build/release/bin/Ladybird --devtools=6000     file://$PWD/Utilities/devtools-cli/manual-test-page.html
```

## Connect

```bash
Meta/ladybird.py run devtools-cli
Meta/ladybird.py run devtools-cli -- --port 6000
```

Use `--no-color`, `--no-colour`, `NO_COLOR=1`, or `DEVTOOLS_CLI_NO_COLOR=1`
to disable ANSI colors.

## Commands

### Session

- `tabs` refreshes and prints available tabs.
- `attach [index]` attaches to a tab, prompting for an index when omitted.
- `raw <json>` sends a raw protocol request and prints highlighted JSON.
- `help` prints command help.
- `quit`, `q`, and `exit` leave the REPL.

The prompt shows the selected frame actor and selected DOM node when those are
known. Command history and command-name completion are available in the REPL.


## Smoke Check

A useful manual sequence is:

```text
tabs
attach
select #grid
computed width border-*
children
```
