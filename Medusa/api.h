// api.h - Jellyfin REST API calls (auth, libraries, items) over libcurl.
#ifndef MEDUSA_API_H
#define MEDUSA_API_H

#include <stdbool.h>
#include <stddef.h>
#include "config.h"

#define MAX_LIBRARIES 32
#define MAX_ANCESTORS 8
#define MAX_MEDIA_STREAMS 32

typedef struct {
    char id[64];
    char name[128];
    char collection_type[32]; // "movies", "tvshows", "music", ...
} ApiLibrary;

typedef struct {
    char id[64];
    char name[128];
    char type[32]; // "Season", "Series", "CollectionFolder", ...
} AncestorNode;

typedef struct {
    int stream_index;   // Jellyfin's global stream Index - needed for external subtitle delivery URLs
    int track_number;   // 1-based ordinal within its type (Audio/Subtitle) - matches mpv's aid/sid for embedded tracks
    char type[16];       // "Audio" or "Subtitle" (Video/other stream types are skipped)
    char codec[16];
    char language[8];    // ISO 639-2-ish code, e.g. "eng"/"jpn" - may be empty
    char display_title[160]; // Jellyfin's own friendly label, when present
    bool is_default;
    bool is_forced;
    bool is_external;    // subtitle-only: a sidecar file rather than embedded in the container
} MediaStreamInfo;

typedef struct {
    char id[64];
    char name[256];
    char type[32];        // "Movie", "Series", "Season", "Episode", ...
    bool is_folder;       // true for Series/Season - browse into it, don't play it
    char series_name[128]; // set for Episode items, for display in flat lists
    int season_number;      // Jellyfin's ParentIndexNumber, 0 if unknown
    int episode_number;      // Jellyfin's IndexNumber, 0 if unknown
    int year;
    int runtime_min;
    long long runtime_ticks; // exact duration (10,000,000 ticks/sec) - for the playback progress bar
    long long playback_position_ticks; // exact resume position - for seeking mpv on play
    bool played;
    int progress_pct; // 0-100, derived from PlaybackPositionTicks / RunTimeTicks
    char overview[1024];
} ApiItem;

// Must be called once at startup / shutdown (wraps curl_global_init/cleanup).
void api_init(void);
void api_cleanup(void);

// When enabled, every items fetch (library browsing, Continue Watching,
// Search) appends its raw JSON response to /tmp/medusa-debug.log, so
// browsing normally is enough to capture what the server actually sent.
void api_set_debug(bool enabled);

// Rewrites cfg->server in place to a normalized "http://host:port" form
// (adds a scheme if missing, strips a trailing slash).
void api_normalize_server(char* server, size_t sz);

// On success, fills cfg->access_token/user_id/user_name and returns true.
// On failure, writes a human-readable message into err.
bool api_authenticate(Config* cfg, const char* username, const char* password, char* err, size_t err_sz);

bool api_get_libraries(const Config* cfg, ApiLibrary* out, int max_out, int* out_count, char* err, size_t err_sz);

// Fetches every child item under parent_id in one request (no artificial
// cap - a library with 5000 movies gets all 5000). *out_items is malloc'd
// to exactly *out_count entries on success; the caller must free() it.
// On failure *out_items is NULL and *out_count is 0. The same ownership
// rule applies to api_get_continue_watching and api_search_items below.
bool api_get_items(const Config* cfg, const char* parent_id, ApiItem** out_items, int* out_count, char* err, size_t err_sz);

// In-progress items across all libraries (Jellyfin's "Resume" endpoint).
bool api_get_continue_watching(const Config* cfg, ApiItem** out_items, int* out_count, char* err, size_t err_sz);

// Server-wide search across movies, series, and episodes.
bool api_search_items(const Config* cfg, const char* query, ApiItem** out_items, int* out_count, char* err, size_t err_sz);

// The ancestor chain for item_id, immediate parent first (e.g. for an
// episode: Season, then Series, then the top-level library). Used to
// reconstruct a real browse path for an item found via a flat list
// (Continue Watching, Search).
bool api_get_ancestors(const Config* cfg, const char* item_id, AncestorNode* out, int max_out, int* out_count, char* err, size_t err_sz);

// Builds a direct-stream URL for item_id (static=true - the original file,
// no transcode negotiation). mpv decodes almost anything, so this skips the
// full PlaybackInfo/DeviceProfile dance most clients do.
void api_build_stream_url(const Config* cfg, const char* item_id, char* out_url, size_t out_sz);

// Playback progress reporting (so Jellyfin tracks watched/resume state).
// media_source_id is item_id for the common single-source case.
bool api_report_playback_start(const Config* cfg, const char* item_id, const char* media_source_id,
                                const char* play_session_id, long long position_ticks, char* err, size_t err_sz);
bool api_report_playback_progress(const Config* cfg, const char* item_id, const char* media_source_id,
                                   const char* play_session_id, long long position_ticks, bool is_paused,
                                   char* err, size_t err_sz);
bool api_report_playback_stopped(const Config* cfg, const char* item_id, const char* media_source_id,
                                  const char* play_session_id, long long position_ticks,
                                  char* err, size_t err_sz);

// Marks item_id played/unplayed. Jellyfin cascades this server-side when
// item_id is a Series or Season - all of its episodes get marked too, no
// need to walk the tree ourselves.
bool api_set_played(const Config* cfg, const char* item_id, bool played, char* err, size_t err_sz);

// Debug helper: fetches item_id's raw, unparsed JSON (GET
// /Users/{userId}/Items/{itemId}) and writes it verbatim to out_path.
bool api_dump_raw_item(const Config* cfg, const char* item_id, const char* out_path, char* err, size_t err_sz);

// Audio/Subtitle streams for item_id's (single) media source, in container
// order. track_number is computed per-type so it lines up with mpv's own
// aid/sid numbering for embedded tracks.
bool api_get_media_streams(const Config* cfg, const char* item_id, MediaStreamInfo* out, int max_out,
                            int* out_count, char* err, size_t err_sz);

// Delivery URL for an external (sidecar) subtitle stream.
void api_build_subtitle_url(const Config* cfg, const char* item_id, const MediaStreamInfo* stream,
                             char* out_url, size_t out_sz);

#endif
