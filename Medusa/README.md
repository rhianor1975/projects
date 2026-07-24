# Medusa – Terminal Client for Jellyfin

**Tags:** #jellyfin #media-player #c-programming #mpv #terminal #cli

---

Named for the biological term for a jellyfish's adult body form — a nod to Jellyfin's own mascot.

A lightweight terminal client for [Jellyfin](https://jellyfin.org) media servers: browse your libraries, drill into shows and seasons, and play movies/episodes through `mpv`. No ncurses, no notcurses — just raw `termios` and ANSI escapes for the UI, matching the style of a from-scratch terminal chess board rather than a heavyweight TUI framework.

Playback is deliberately simple: Medusa resolves a direct-stream URL and hands it to `mpv`, which opens in its own native window and decodes almost anything — no `PlaybackInfo`/`DeviceProfile` transcode negotiation most clients do. Medusa drives `mpv` over its JSON IPC socket (fork/exec + Unix socket, the same shape as talking to a UCI chess engine over pipes) for status, seeking, and audio/subtitle track selection.

## Dependencies

* **C compiler** (gcc or clang) and `libcurl`
* **`mpv`** — must be installed and on `PATH`; it's what actually plays video
* Linux or macOS (POSIX `termios`/`fork`/Unix-domain sockets throughout)

## Build

    make

Produces the `medusa` executable.

## Usage

    ./medusa              # launch the client
    ./medusa --debug      # also log every server request/response to medusa-debug.log
    ./medusa --help        # usage summary

On first run it broadcasts a UDP discovery request for Jellyfin servers on your LAN (or you can type a server URL manually), then logs in with your Jellyfin username/password. Server, login, and playback (audio/subtitle) preferences are saved to `~/.config/medusa/config`.

Each screen shows its own available keys at the bottom — arrows to navigate, Enter to open/play, `w` to toggle watched, `s` to sort, `a`/`s` (on the info screen) to pick audio/subtitle tracks, `q` to quit.

## Features

* **Server discovery** — UDP broadcast on port 7359 (Jellyfin's own discovery protocol) finds servers on your network automatically.
* **Library browsing** — Movies and TV Shows, with Series → Season → Episode drill-down, client-side sorting (name, year, episode number), and pagination for large libraries (no artificial item cap).
* **Continue Watching & Search** — selecting an item from either flat list reconstructs its real position in the library hierarchy (via Jellyfin's item-ancestry endpoint) before playing, so back-navigation and episode auto-advance behave exactly as if you'd browsed there normally.
* **Playback via mpv** — its own window, live progress bar, resume-from-last-position, and Jellyfin progress reporting (`Sessions/Playing`/`Progress`/`Stopped`) every 5 seconds.
* **Binge flow** — one mpv window/process persists across an entire season: finishing an episode automatically loads and plays the next one in the same window (no window-per-episode churn), with reliable end-of-file detection (`--keep-open` + polling `eof-reached`, immune to a manual seek-to-the-end race).
* **Audio/subtitle track selection** — pick a track once per session; later episodes re-match your choice by language automatically. Switching tracks directly in mpv's own window is detected and adopted as your new preference, persisted for next time.
* **Watched state** — toggle played/unplayed on any item; marking a Series or Season cascades server-side to every episode inside it, and folder rows in the UI correctly reflect a fully-watched season/show, not just individual episodes.
* **Hand-rolled JSON parser** — no vendored dependency; a small recursive-descent parser tailored to what the Jellyfin API actually returns.

## How It Works

Six small modules, each doing one thing: `config` (persisted server/session/preferences), `json` (parser), `api` (libcurl calls to Jellyfin's REST API), `mpv` (spawn + JSON-IPC control), `ui` (termios/ANSI screens and navigation), and `main` (wiring). Playback state — a `MpvPlayer` struct holding the process ID and IPC socket — is owned by the browsing loop for an entire binge session, not per-episode, so consecutive episodes reuse the same mpv window via its `loadfile` IPC command instead of restarting the process.

## License

MIT License – feel free to use, modify, and share.

## Author

Rhianor the Dark

---
#jellyfin-client #mpv #terminal-ui #media-streaming #c
