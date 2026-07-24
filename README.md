# My Projects

**Tags:** #portfolio #c-programming #terminal-ui #ncurses #open-source #linux #cli

---

A collection of terminal-based tools and applications built in C.

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

---

## 📦 Installation

### Clone the repository
git clone https://github.com/rhianor1975/projects.git
cd projects

### Build a specific project
cd fenrir && make
# or
cd Elara && make

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
