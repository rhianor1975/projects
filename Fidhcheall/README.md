# Fidhcheall – Chess Engine Tester

**Tags:** #chess #uci #engine-testing #c-programming #sprt #cli

---

Named for the ancient Gaelic strategy board game (Scottish Gaelic *fidhcheall*); the word became the modern Gaelic word for chess.

A single self-contained C file that plays matches, round-robins, and gauntlets between UCI chess engines and reports the results — win/loss/draw, PGN, Elo with an error bar, LOS, and SPRT early-stopping. It includes its own small chess rules engine (legal move generation, checkmate/stalemate/draw detection, SAN notation) and talks to UCI engines as subprocesses over pipes.

Lightweight, dependency-free engine-vs-engine testing is scarce on macOS — the usual options (cutechess-cli, fastchess) are heavier C++/Qt builds or need fiddling to get running. Fidhcheall's whole pitch: `cc fidhcheall.c -o fidch` and you're testing.

## Dependencies

* **C compiler** (gcc or clang) and a POSIX libm/libpthread
* Nothing else — no external libraries. `fork`/`pipe`/`select`/`pthread` based, so macOS and Linux; not native Windows without WSL.

## Build

To build the project, run:
make

This will produce the `fidch` executable. Equivalently, the one-liner the name is built around:
cc -O2 -Wall -pthread -o fidch fidhcheall.c -lm

## Usage

Two engines, a plain match:
./fidch -r 100 -e1 ./engineA -e2 ./engineB

Three or more engines, round-robin (every pair plays):
./fidch -r 20 -e1 ./ferrum -e2 ./corvus -e3 ./fang -t 500

Gauntlet — engine 1 is the challenger against the rest of the field:
./fidch -r 20 -e1 ./ferrum -e2 ./fang -e3 ./corvus -e4 ./stockfish --tournament gauntlet

Large fields via the repeatable form, with per-engine name/options:
./fidch -r 20 -e ./ferrum -e "./stockfish;name=SF16;opt=Threads=4,Hash=256" -e ./fang

Everything out of a config file instead of a long command line:
./fidch --config tourney.txt

SPRT early-stop (paired openings + pentanomial LLR — needs `--random-plies` or `--openings` for variety):
./fidch -e1 ./ferrum -e2 ./ferrum_ref --random-plies 4 --seed 1 --sprt 0,5

A curated opening book instead of random plies (EPD positions or a PGN of games):
./fidch -e1 ./ferrum -e2 ./fang --openings 8moves_v3.epd -r 200

Experimental: run games concurrently instead of one at a time (see the note below `-c` in Features):
./fidch -r 100 -e1 ./ferrum -e2 ./fang -c 4

Run `./fidch -h` for the full flag list.

## Config File Format

One directive per line; blank lines and lines starting with `#` are ignored. Flags also given on the actual command line override the config file.

```
# tourney.txt
rounds 20
movetime 500
tournament gauntlet
engine ./ferrum;name=Ferrum
engine ./fang
engine ./corvus
openings 8moves_v3.epd
output gauntlet.pgn
```

## Features

* **Its own arbiter** – legal move generation, checkmate/stalemate/threefold/50-move/insufficient-material detection, SAN output. No dependency on an external chess library.
* **Round-robin and gauntlet tournaments** – any number of engines (`-e1..-eN` or repeatable `-e`), full cross-table + standings for round-robin, challenger-vs-field summary for gauntlet.
* **Per-engine UCI options and display names** – `--eN-options K=V,K=V` (numbered form) or `;opt=K=V,K=V` (repeatable form).
* **Time controls** – fixed movetime, fixed depth, or fixed node count per move.
* **Paired openings** – every opening played from both colors, shared across every pairing in a tournament for a fair comparison. Sources: seeded random plies, or a curated `--openings FILE.epd` / `FILE.pgn` set.
* **Statistics** – pentanomial scoring, Elo with a 95% error bar, LOS (likelihood of superiority), and SPRT early-stopping (`--sprt E0,E1[,alpha,beta]`).
* **PGN output** – written incrementally as games complete, with `[FEN]`/`[SetUp]` tags when a non-standard opening was used.
* **Robust to engine crashes/timeouts** – a dead or hung engine forfeits its game (with the reason recorded, and its stderr captured to a log file); the tournament continues.
* **`--config FILE`** – engines, options, and settings out of a file instead of a long command line.
* **`-c N` concurrency (experimental)** – run N games at once via a worker-thread pool instead of one at a time; default is `1` (sequential, unchanged behavior). Up to `2N` engine processes run simultaneously, so if your engines are themselves multi-threaded, cap them (e.g. an engine option like `Threads=1`) or you'll oversubscribe your CPU. Under SPRT, a pairing's stopping point can overshoot by a few in-flight games rather than stopping exactly on the deciding game.

## How It Works

Each engine is launched as a subprocess (`fork`/`exec`) and driven over its stdin/stdout pipes with the UCI protocol (`uci`, `isready`, `position`, `go`). Fidhcheall's own move generator validates every move the engine returns; an illegal move, a crash, or a timeout forfeits that game rather than aborting the run. A tournament is just a schedule of pairings over that one primitive (`play_game`) — round-robin plays every pair, gauntlet plays engine 1 against everyone else — so the arbiter and engine I/O never change between a two-engine match and a ten-engine tournament.

## License

MIT License – feel free to use, modify, and share.

## Author

Rhianor the Dark
---
#chess-engine #uci-protocol #sprt #elo #gauntlet #tournament
