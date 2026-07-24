// mpv.h - spawn mpv as a subprocess and control it over its JSON IPC socket.
//
// mpv opens its own native window for video (the TUI stays in the terminal
// for browsing/status) - this module just launches it and talks to it,
// mirroring the fork/pipe/select pattern used for the chess engine, but
// over a Unix-domain socket with newline-delimited JSON instead of pipes.
#ifndef MEDUSA_MPV_H
#define MEDUSA_MPV_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    pid_t pid;
    int sock_fd;
    char socket_path[128];
} MpvPlayer;

// Launches mpv pointed at stream_url, seeking to start_seconds if > 0 (a
// saved resume position). On failure, mpv is NOT left running and err
// describes what went wrong (e.g. mpv not installed).
bool mpv_start(MpvPlayer* mpv, const char* stream_url, double start_seconds, char* err, size_t err_sz);

// True while the mpv process is alive; reaps it (updates internal state)
// once it has exited.
bool mpv_is_running(MpvPlayer* mpv);

// Polls mpv's time-pos/duration (seconds), pause, eof-reached, and current
// audio/subtitle track (aid/sid) over the IPC socket. Returns false on a
// query failure/timeout - not necessarily that mpv quit; check
// mpv_is_running() separately for that. *out_duration may briefly stay 0
// right after start, before mpv has finished loading the file. *out_aid/
// *out_sid are 0 when off/unknown (mpv reports subtitles-off as a JSON
// `false` rather than a track number - this normalizes that to 0 too).
//
// mpv is launched with --keep-open=yes, so it does NOT close itself at
// end-of-file - it stays open (paused on the last frame) so *out_eof_reached
// is a reliable, race-free signal rather than something callers have to
// infer by racing to catch time_pos close to duration before mpv exits on
// its own (which is especially unreliable right after a seek near the end,
// since mpv can hit EOF and close within a single ~500ms poll interval).
bool mpv_get_status(MpvPlayer* mpv, double* out_time_pos, double* out_duration, bool* out_paused,
                     bool* out_eof_reached, int* out_aid, int* out_sid);

// Tells an already-running mpv to load a different URL into the SAME
// window/process (replacing what's currently loaded) - for moving between
// episodes without spawning a new window each time.
bool mpv_load_file(MpvPlayer* mpv, const char* stream_url);

// Blocks (up to timeout_ms) for mpv's "file-loaded" IPC event - its own
// definitive signal that the file from mpv_load_file has actually finished
// loading (demuxer opened, tracks selected) and it's safe to seek. This is
// NOT the same as polling a property like "duration": right after loadfile,
// properties go through a transient/unstable phase (duration briefly
// reporting a partial estimate before settling), so guessing readiness from
// property values can seek at the wrong moment - the event is race-free.
bool mpv_wait_for_file_loaded(MpvPlayer* mpv, int timeout_ms);

// Seeks to an absolute position (seconds). Only meaningful once
// mpv_wait_for_file_loaded has confirmed the file is ready.
void mpv_seek_absolute(MpvPlayer* mpv, double seconds);

// Sets mpv's pause state. "pause" is a player-wide property, not tied to
// whatever file is loaded - if the previous file ended via --keep-open=yes
// (which pauses instead of exiting), that paused state carries straight
// into the next mpv_load_file. Call mpv_set_pause(mpv, false) after loading
// the next episode so it doesn't sit there paused.
void mpv_set_pause(MpvPlayer* mpv, bool paused);

// Sets the window title. Without this, mpv's default title is derived from
// the stream URL - which for us is the raw Jellyfin stream request,
// including the api_key query param, visible in a screenshot/share.
void mpv_set_title(MpvPlayer* mpv, const char* title);

// Switches to embedded audio/subtitle track number `track_number` (mpv's
// aid/sid, 1-based per type - matches MediaStreamInfo.track_number for
// tracks embedded in the same file we're direct-streaming).
void mpv_set_audio_track(MpvPlayer* mpv, int track_number);
void mpv_set_subtitle_track(MpvPlayer* mpv, int track_number);

// Turns subtitles off entirely.
void mpv_disable_subtitles(MpvPlayer* mpv);

// Adds an external (sidecar) subtitle file/URL and makes it the active
// subtitle track immediately.
bool mpv_add_subtitle(MpvPlayer* mpv, const char* url);

// Asks mpv to quit, closes the socket, and waits for the process to exit
// (force-killing if it doesn't within a couple seconds). Safe to call on a
// MpvPlayer that was never started (zero-initialized with sock_fd = -1).
void mpv_stop(MpvPlayer* mpv);

#endif
