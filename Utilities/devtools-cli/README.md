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


### DOM Selection

- `select <selector>` selects and highlights a DOM node using a CSS selector.
  When multiple nodes match, the CLI prints a numbered menu and prompts for an
  index. Escape or Ctrl-C cancels the menu.
- `query <selector>` prints matching nodes without changing the selection.
- `selected`, `children`, `child <index>`, `parent`, `next`, and `previous`
  navigate from the selected node.
- `html`, `outer-html`, and `highlight` inspect or highlight the selected node.
- `pick` starts Ladybird picker mode; picking a node selects it in the CLI.

Commands that use a stale node report an error and clear the selection.


### Style And Box Model

- `computed [props...]` prints computed style for the selected node.
- `rules [props...]` prints applied style rules for the selected node.
- `box [props...]` prints box-model data for the selected node.

Property filters support wildcards, for example `computed border-*`.


### Style Sheets

- `stylesheets` lists style sheet resources.
- `stylesheet <id>` prints the text for a style sheet resource.


### JavaScript Sources

- `sources` lists JavaScript sources known to the selected frame.
- `source <index|actor>` prints source text by list index or source actor.


### Console Evaluation

- `eval <javascript>` evaluates JavaScript in the selected frame and prints the
  result or exception.


### Grid Layout

- `grid` prints grid data for the selected node.
- `grids` lists grid containers and selects one through a numbered menu.
- `highlight-grid [options]` highlights the selected grid. Supported options:
  `--extend-lines`, `--line-numbers`, `--area-names`, `--track-sizes`, and
  `--color <value>`.
- `unhighlight-grid [--all]` hides the selected grid highlight or all grid
  highlights.


## Smoke Check

A useful manual sequence is:

```text
tabs
attach
select #grid
computed width border-*
children
```

```text
grids
highlight-grid --line-numbers --color #ff00ff
unhighlight-grid --all
```
