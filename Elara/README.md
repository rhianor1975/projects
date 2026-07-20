# Elara – A Terminal Text Editor

**Tags:** #text-editor #terminal-ui #ncurses #c-programming #linux #cli

---

A full-featured terminal text editor built with **ncurses**. Inspired by the simplicity of Kilo and the power of modern editors.

## Features

* **Syntax highlighting** – supports many languages via JSON definitions
* **Multiple tabs** – edit several files at once (Ctrl+N, Ctrl+\\)
* **Themes** – 21 built-in colour themes (F6 to cycle)
* **Mouse support** – click to position, scroll, select (Ctrl+J to toggle)
* **Line wrapping** – character or word wrap (F9 to cycle)
* **Undo/Redo** – full history (Ctrl+Z / Ctrl+Y)
* **Search & Replace** – with case sensitivity (Ctrl+F, Ctrl+B, Ctrl+R)
* **Clipboard** – copy, cut, paste (Ctrl+C, Ctrl+X, Ctrl+V)
* **Language support** – JSON-defined language specifications
* **Compile & Run** – for supported languages (F7, F8)
* **Auto-indent** – configurable (Ctrl+P to toggle paste mode)
* **UTF-8 support** – full Unicode character handling
* **Persistent configuration** – settings saved to `~/.editorrc`

## Dependencies

* **ncurses**
* **pthread** (built into glibc)
* **C compiler** (gcc recommended)

## Build

To build the project, run:
make

## Usage

To launch the editor:
./elara [filename]

## Key Bindings

### File Operations

| Key | Action |
| :--- | :--- |
| **F2 / Ctrl+S** | Save file |
| **F3 / Ctrl+O** | Open file browser |
| **F4** | Save as |
| **F10 / Ctrl+X** | Close tab / Quit |

### Tabs

| Key | Action |
| :--- | :--- |
| **Ctrl+N** | New tab |
| **Ctrl+\\** | Next tab |
| **F12** | Open file in new tab |
| **Alt+1..9 / Esc 1..9** | Jump to tab 1-9 |

### Editing

| Key | Action |
| :--- | :--- |
| **Ctrl+Z** | Undo |
| **Ctrl+Y** | Redo |
| **Tab** | Insert tab / Trigger completion |
| **Ctrl+K** | Delete current line |
| **Ctrl+D** | Duplicate line |
| **Ctrl+A** | Select all |
| **Ctrl+C** | Copy selection |
| **Ctrl+X** | Cut selection |
| **Ctrl+V** | Paste |
| **Alt+Backspace** | Delete word backward |

### Navigation

| Key | Action |
| :--- | :--- |
| **Arrow Keys** | Move cursor |
| **Ctrl+Left/Right** | Jump between words |
| **Home / End** | Start / end of line |
| **Page Up/Down** | Scroll page |
| **Mouse Wheel** | Scroll (mouse ON) |
| **Ctrl+G** | Go to line number |
| **F5** | Toggle line numbers |

### Search & Replace

| Key | Action |
| :--- | :--- |
| **Ctrl+F** | Search forward |
| **Ctrl+B** | Search backward |
| **Ctrl+E** | Find next occurrence |
| **Ctrl+H** | Toggle case sensitivity |
| **Ctrl+R** | Replace text |
| **Ctrl+W** | Word and character count |

### Features

| Key | Action |
| :--- | :--- |
| **F6** | Cycle themes |
| **Ctrl+T** | Theme picker |
| **F9** | Cycle wrap modes (OFF → Character → Word) |
| **F7** | Compile / Check (if supported) |
| **F8** | Run (if supported) |
| **F11 / Ctrl+U** | Insert template (if defined) |
| **Ctrl+L** | Change language |
| **Ctrl+J** | Toggle mouse |
| **Ctrl+Q** | Toggle reading mode |
| **Ctrl+]** | Toggle UTF-8 mode |
| **F1** | Help |

## Configuration

Settings are saved to `~/.editorrc`:

theme=0          # Theme index (0-20)
line_numbers=1   # Show line numbers
tab_width=4      # Tab width in spaces
auto_indent=1    # Auto-indent
scroll_speed=3   # Mouse scroll speed
wrap_text=1      # Text wrapping
wrap_mode=1      # 0=Character, 1=Word
wrap_indent=4    # Wrapped line indent
wrap_indicator=1 # Show ~ on wrapped lines
enable_mouse=0   # Mouse support
indent_guides=1  # Show indent guides

## Language Support

Languages are defined as JSON files in the `languages/` directory.

Supported languages include: C, C++, Python, JavaScript, Java, Go, Rust, Ruby, PHP, Shell, HTML, CSS, Markdown, SQL, YAML, and more.

Each language definition includes:
* Keywords, types, builtins
* Comment styles
* Auto-indent rules
* Brackets for auto-closing
* Completions and snippets
* Templates
* Compile and run commands

## Testing

To run the self-test suite:
./elara --self-test

## License

MIT License – feel free to use, modify, and share.

## Author

Rhianor the Dark

---
#tui-editor #development #texteditor #programming #unix
