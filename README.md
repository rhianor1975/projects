# My Projects

**Tags:** #portfolio #c-programming #terminal-ui #ncurses #open-source #linux #cli #boardgames

---

A collection of terminal-based tools, applications and board games built in C.

---

## 📁 Projects

### 🐺 [Fenrir](./fenrir/) – Terminal Music Player

A powerful terminal-based music player that uses `mpv` as the playback engine.

* Directory browsing with metadata loading
* Persistent state (resume playback, volume, theme)
* Chapter support for .m4b/.m4a files
* 8 colour themes
* Shuffle mode
* CUE sheet support

**Build:** cd fenrir && make  
**Run:** ./fenrir [directory]

---

### ✨ [Elara](./Elara/) – Terminal Text Editor

A full-featured terminal text editor with syntax highlighting, tabs, themes, and language support.

* 21 colour themes
* Multiple tabs (Ctrl+N, Ctrl+\)
* Language definitions via JSON
* Mouse support (Ctrl+J)
* Undo/Redo (Ctrl+Z / Ctrl+Y)
* Search and replace (Ctrl+F / Ctrl+B / Ctrl+R)
* Compile and run support (F7 / F8)

**Build:** cd Elara && make  
**Run:** ./elara [filename]

---

### ♟️ [Fidhcheall](./Fidhcheall/) – Chess Engine Tester

A single self-contained C file that plays matches, round-robins, and gauntlets between UCI chess engines and reports Elo, LOS, and SPRT results.

* Round-robin and gauntlet tournaments over any number of engines
* Its own arbiter (legal moves, mate/draw detection, SAN) — no external chess library
* Paired openings (seeded random plies, or a curated EPD/PGN opening book), shared across every pairing
* Elo ± 95% CI, LOS, and SPRT early-stopping from pentanomial statistics
* PGN output, per-engine UCI options, `--config FILE` for large fields
* No dependencies beyond a C compiler and libm

**Build:** cd Fidhcheall && make  
**Run:** ./fidch -r 100 -e1 ./engineA -e2 ./engineB

---

### 🪼 [Medusa](./Medusa/) – Terminal Client for Jellyfin

A lightweight terminal client for Jellyfin media servers: browse libraries, drill into shows/seasons, and play movies/episodes through `mpv`. No ncurses — raw `termios` and ANSI escapes.

* UDP server discovery on your LAN, no manual server URL needed
* Movies/TV browsing with Series → Season → Episode drill-down, sorting, pagination
* Continue Watching & Search reconstruct the real library path before playing
* One mpv window persists across a whole binge session — episodes auto-advance in place
* Audio/subtitle track selection, remembered by language across episodes and sessions
* Watched/unwatched toggle that cascades correctly through Series → Season → Episode
* Hand-rolled JSON parser, no vendored dependency

**Build:** cd Medusa && make  
**Run:** ./medusa

---

### 🗡️ [Talisman](./Talisman/) – Talisman, 2nd Edition

The board game in a terminal, with computer opponents that plan on the board rather than searching it. Three concentric regions drawn as the perimeters of one 7x7 grid — 24 + 16 + 8 + 1 = 49, so every cell is a real space.

* Every expansion shuffled in, plus three optional rules: the six Alternative Endings, Henchmen, and the Chaos Bloodbath
* Cards and characters transcribed from the real rulebook, not approximated
* Headless autoplay with seeded replay, so any game can be reproduced exactly
* Rules invariants checked as the game runs — a batch that prints nothing is a batch with no rules bugs
* Fits an 86x29 terminal; the layout grows into whatever it is given

**Build:** cd Talisman && make
**Run:** ./talisman — or ./talisman --watch to let four computer players get on with it

---

### 🐉 [Prophecy](./Prophecy/) – Prophecy, 2nd Edition

Walk a ring of twenty spaces, build a hero, and take four of the five Artifacts from the Astral Planes. The ring is the border of a 6x6 grid, because its perimeter is exactly the twenty spaces the rulebook specifies.

* The complete 2nd-edition base game: 63 Adventure cards, 50 Guild Abilities, 61 Items, 10 Guardians, 5 Artifacts
* Both expansion worlds as playable subsystems — the Dragon Realm's three climbing paths, and the Water Realm's Bubbles, 4x6 grid and the -1-per-hand penalty that makes carrying less the better choice
* The Ancient Races, with their advantages and their costs
* Every card marked as printed, expansion or house rule, since Prophecy has a Czech original that differs from the English and no shortage of fan material
* Modes combine freely: `PROPHECY_CANON=1` deals the printed deck alone

**Build:** cd Prophecy && make
**Run:** ./prophecy

---

## 🔧 Dependencies

### Common
* `gcc` – C compiler
* `make` – Build tool
* `ncurses` – Terminal UI library

### Fenrir (Music Player)
* `mpv` – Playback engine
* `ffprobe` – Metadata reading

### Elara (Text Editor)
* `ncurses` – UI library
* `pthread` – Threading

### Fidhcheall (Chess Engine Tester)
* `libm` – math library (linked with `-lm`), no UI dependency

### Medusa (Jellyfin Client)
* `libcurl` – HTTP client for the Jellyfin REST API
* `mpv` – playback engine, driven over its JSON IPC socket

### Talisman and Prophecy (board games)
* `ncurses` – UI library. Nothing else: the cards are compiled in

---

## 📦 Installation

### Clone the repository
git clone https://github.com/rhianor1975/projects.git
cd projects

### Build a specific project
cd fenrir && make
# or
cd Elara && make
# or
cd Medusa && make

### Install system-wide
sudo make install

---

## 🚀 Quick Start

### Fenrir (music player):
cd fenrir && make
./fenrir ~/Music

### Elara (text editor):
cd Elara && make
./elara myfile.c

### Fidhcheall (chess engine tester):
cd Fidhcheall && make
./fidch -r 20 -e1 ./engineA -e2 ./engineB

### Medusa (Jellyfin client):
cd Medusa && make
./medusa

### Talisman (board game):
cd Talisman && make
./talisman --watch

### Prophecy (board game):
cd Prophecy && make
PROPHECY_RACES=1 PROPHECY_DRAGON=1 PROPHECY_WATER=1 ./prophecy

---

## 📂 Folder Structure

<pre>
projects/
├── README.md          # This file
├── .gitignore         # Top-level ignores
├── fenrir/            # 🐺 Music player
│   ├── fenrir.c
│   ├── Makefile
│   └── README.md
├── Elara/             # ✨ Text editor
│   ├── elara.c
│   ├── Makefile
│   ├── README.md
│   └── languages/
│       ├── c.json
│       ├── python.json
│       └── ...
├── Fidhcheall/         # ♟️ Chess engine tester
│   ├── fidhcheall.c
│   ├── Makefile
│   └── README.md
├── Medusa/             # 🪼 Jellyfin terminal client
│   ├── main.c
│   ├── ui.c / ui.h
│   ├── api.c / api.h
│   ├── mpv.c / mpv.h
│   ├── json.c / json.h
│   ├── config.c / config.h
│   ├── Makefile
│   └── README.md
├── Talisman/           # 🗡️ Talisman, 2nd edition
│   ├── src/
│   ├── Makefile
│   └── README.md
├── Prophecy/           # 🐉 Prophecy, 2nd edition
│   ├── src/
│   ├── Makefile
│   └── README.md
└── (future projects)
</pre>

---

## 📄 License

MIT License – feel free to use, modify, and share.

## 👤 Author

Rhianor the Dark
More projects coming soon...

---
#github #developer #programming #c #linux #tui
