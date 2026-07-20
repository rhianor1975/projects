# Fenrir – Terminal Music Player

**Tags:** #music-player #terminal-ui #mpv #c-programming #ncurses #audio

---

A terminal‑based music player that uses **mpv** as the playback engine. It features a file browser, playlist management, metadata reading (via `ffprobe`), persistent state (resume playback, volume, theme, sort order), chapter support, and multiple colour themes.

## Dependencies

* **mpv** (with `--input-ipc-server` support)
* **ffprobe** (from ffmpeg)
* **ncurses**
* **pthread** (built into glibc)
* **C compiler** (gcc recommended)

## Build

To build the project, run:
make

This will produce the `fenrir` executable.

## Usage

To run the program:
./fenrir [starting_directory]

If no directory is given, it starts in the current folder (or resumes your last session from the state file).

## Key Bindings

| Key | Action |
| :--- | :--- |
| **Enter** | Play / Open directory |
| **Space** | Pause / Resume |
| **n** | Next track |
| **p** | Previous track |
| **c** | Next chapter |
| **x** | Previous chapter |
| **+** | Increase volume |
| **-** | Decrease volume |
| **s** | Toggle shuffle |
| **o** | Cycle sort order (Alphabetical, Track, Type, Size) |
| **t** | Cycle colour theme |
| **←** | Seek backward 10 seconds |
| **→** | Seek forward 10 seconds |
| **Backspace** | Go to parent directory |
| **q** | Quit |

## Features

* **Metadata loading** – duration, bitrate, track number, disc number (via `ffprobe`)
* **Chapter support** – for `.m4b` and `.m4a` files
* **Shuffle mode** – random playback order
* **Multiple sort orders** – alphabetical, by track number, by file type, by size
* **8 colour themes** – green, orange, red, blue, cyan, mono‑white, mono‑amber, mono‑green
* **Persistent state** – remembers last directory, playing file, position, volume, theme, and sort order
* **Playlist generation** – creates an M3U playlist of all audio files in the current directory
* **CUE sheet support** – recognises `.cue` files and displays them separately

## State File

Settings and playback position are saved to `~/.fenrir_state` and restored on next launch.

## How It Works

The program uses a background thread to load metadata so the interface stays responsive. Communication with `mpv` happens over a Unix socket using JSON commands. The UI is built with `ncurses` and updates in real time.

## License

MIT License – feel free to use, modify, and share.

## Author

Rhianor the Dark
---
#cli #linux #tui #audio-player #music #mpv-player