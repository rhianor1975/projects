// api.c - see api.h

#include "api.h"
#include "json.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
} Buffer;

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t real_size = size * nmemb;
    Buffer* buf = (Buffer*)userp;
    char* ptr = realloc(buf->data, buf->len + real_size + 1);
    if (!ptr) return 0;
    buf->data = ptr;
    memcpy(buf->data + buf->len, contents, real_size);
    buf->len += real_size;
    buf->data[buf->len] = '\0';
    return real_size;
}

static void json_escape_into(char* dst, size_t dst_sz, const char* src) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 3 < dst_sz; si++) {
        char c = src[si];
        if (c == '"' || c == '\\') { dst[di++] = '\\'; dst[di++] = c; }
        else if (c == '\n') { dst[di++] = '\\'; dst[di++] = 'n'; }
        else dst[di++] = c;
    }
    dst[di] = '\0';
}

#define DEBUG_LOG_PATH "medusa-debug.log" // written next to wherever medusa is run from
static bool debug_mode = false;

void api_set_debug(bool enabled) {
    debug_mode = enabled;
    if (enabled) remove(DEBUG_LOG_PATH); // start each run with a clean log
}

static void debug_log_response(const char* url, const char* body) {
    if (!debug_mode) return;
    FILE* f = fopen(DEBUG_LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "==== GET %s ====\n%s\n\n", url, body ? body : "(empty)");
    fclose(f);
}

// Logs an outgoing POST (what we sent) and what the server replied with -
// separate from debug_log_response since these calls (session/progress
// reporting) don't return an items list to log, and their success/failure
// is otherwise invisible even with --debug on.
static void debug_log_post(const char* url, const char* request_body, long status, const char* response_body) {
    if (!debug_mode) return;
    FILE* f = fopen(DEBUG_LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "==== POST %s ====\n> %s\n< HTTP %ld: %s\n\n",
            url, request_body ? request_body : "(empty)", status,
            (response_body && response_body[0]) ? response_body : "(empty)");
    fclose(f);
}

void api_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void api_cleanup(void) {
    curl_global_cleanup();
}

void api_normalize_server(char* server, size_t sz) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", server);

    // strip trailing slash
    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') tmp[--len] = '\0';

    if (strncmp(tmp, "http://", 7) != 0 && strncmp(tmp, "https://", 8) != 0) {
        snprintf(server, sz, "http://%s", tmp);
    } else {
        snprintf(server, sz, "%s", tmp);
    }
}

static bool http_request(const char* method, const char* url, const char* extra_header,
                          const char* token, const char* body,
                          Buffer* out, long* status_out, char* err, size_t err_sz) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        snprintf(err, err_sz, "failed to initialize curl");
        return false;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (extra_header) headers = curl_slist_append(headers, extra_header);
    if (token && token[0]) {
        char token_header[256];
        snprintf(token_header, sizeof(token_header), "X-Emby-Token: %s", token);
        headers = curl_slist_append(headers, token_header);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status_out) *status_out = status;

    bool ok = (res == CURLE_OK);
    if (!ok) snprintf(err, err_sz, "%s", curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

bool api_authenticate(Config* cfg, const char* username, const char* password, char* err, size_t err_sz) {
    api_normalize_server(cfg->server, sizeof(cfg->server));

    char url[256];
    snprintf(url, sizeof(url), "%s/Users/AuthenticateByName", cfg->server);

    char esc_user[256], esc_pass[256];
    json_escape_into(esc_user, sizeof(esc_user), username);
    json_escape_into(esc_pass, sizeof(esc_pass), password);

    char body[600];
    snprintf(body, sizeof(body), "{\"Username\":\"%s\",\"Pw\":\"%s\"}", esc_user, esc_pass);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: MediaBrowser Client=\"medusa\", Device=\"Terminal\", DeviceId=\"%s\", Version=\"0.1.0\"",
             cfg->device_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("POST", url, auth_header, NULL, body, &buf, &status, err, err_sz);

    if (ok && status == 200 && buf.data) {
        JsonValue* root = json_parse(buf.data);
        if (root) {
            const JsonValue* user = json_get(root, "User");
            const char* token = json_get_string(root, "AccessToken", NULL);
            const char* user_id = json_get_string(user, "Id", NULL);
            const char* user_name = json_get_string(user, "Name", NULL);
            if (token && user_id) {
                snprintf(cfg->access_token, sizeof(cfg->access_token), "%s", token);
                snprintf(cfg->user_id, sizeof(cfg->user_id), "%s", user_id);
                if (user_name) snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", user_name);
            } else {
                ok = false;
                snprintf(err, err_sz, "unexpected response from server");
            }
            json_free(root);
        } else {
            ok = false;
            snprintf(err, err_sz, "could not parse server response");
        }
    } else if (ok) {
        ok = false;
        if (status == 401) snprintf(err, err_sz, "invalid username or password");
        else snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    free(buf.data);
    return ok;
}

bool api_get_libraries(const Config* cfg, ApiLibrary* out, int max_out, int* out_count, char* err, size_t err_sz) {
    char url[256];
    snprintf(url, sizeof(url), "%s/Users/%s/Views", cfg->server, cfg->user_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("GET", url, NULL, cfg->access_token, NULL, &buf, &status, err, err_sz);

    *out_count = 0;
    if (ok && status == 200 && buf.data) {
        JsonValue* root = json_parse(buf.data);
        if (root) {
            const JsonValue* items = json_get(root, "Items");
            int n = json_array_count(items);
            for (int i = 0; i < n && *out_count < max_out; i++) {
                const JsonValue* it = json_array_at(items, i);
                ApiLibrary* lib = &out[*out_count];
                snprintf(lib->id, sizeof(lib->id), "%s", json_get_string(it, "Id", ""));
                snprintf(lib->name, sizeof(lib->name), "%s", json_get_string(it, "Name", "Unnamed"));
                snprintf(lib->collection_type, sizeof(lib->collection_type), "%s", json_get_string(it, "CollectionType", ""));
                (*out_count)++;
            }
            json_free(root);
        } else {
            ok = false;
            snprintf(err, err_sz, "could not parse server response");
        }
    } else if (ok) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    free(buf.data);
    return ok;
}

static bool parse_items_json(const char* json, ApiItem** out_items, int* out_count) {
    JsonValue* root = json_parse(json);
    if (!root) return false;

    const JsonValue* items = json_get(root, "Items");
    int n = json_array_count(items);
    ApiItem* out = n > 0 ? calloc((size_t)n, sizeof(ApiItem)) : NULL;

    for (int i = 0; i < n; i++) {
        const JsonValue* it = json_array_at(items, i);
        ApiItem* item = &out[i];

        snprintf(item->id, sizeof(item->id), "%s", json_get_string(it, "Id", ""));
        snprintf(item->name, sizeof(item->name), "%s", json_get_string(it, "Name", "Unnamed"));
        snprintf(item->type, sizeof(item->type), "%s", json_get_string(it, "Type", ""));
        item->is_folder = json_get_bool(it, "IsFolder", false);
        snprintf(item->series_name, sizeof(item->series_name), "%s", json_get_string(it, "SeriesName", ""));
        item->season_number = (int)json_get_number(it, "ParentIndexNumber", 0);
        item->episode_number = (int)json_get_number(it, "IndexNumber", 0);
        snprintf(item->overview, sizeof(item->overview), "%s", json_get_string(it, "Overview", ""));
        item->year = (int)json_get_number(it, "ProductionYear", 0);

        double runtime_ticks = json_get_number(it, "RunTimeTicks", 0);
        item->runtime_ticks = (long long)runtime_ticks;
        item->runtime_min = (int)(runtime_ticks / 10000000.0 / 60.0);

        const JsonValue* user_data = json_get(it, "UserData");
        item->played = json_get_bool(user_data, "Played", false);
        double pos_ticks = json_get_number(user_data, "PlaybackPositionTicks", 0);
        item->playback_position_ticks = (long long)pos_ticks;
        item->progress_pct = (runtime_ticks > 0) ? (int)((pos_ticks / runtime_ticks) * 100.0) : 0;
        if (item->progress_pct > 100) item->progress_pct = 100;

        // For a folder (Series/Season), the "Played" boolean only reflects
        // whether the folder itself was explicitly marked - if it became
        // fully watched by finishing episodes one at a time (e.g. on a
        // different client), that boolean can stay false even though
        // everything in it has been seen. UnplayedItemCount is Jellyfin's
        // actual aggregate signal for that case.
        if (item->is_folder) {
            int unplayed = (int)json_get_number(user_data, "UnplayedItemCount", -1);
            if (unplayed == 0) item->played = true;
        }
    }

    *out_items = out;
    *out_count = n;
    json_free(root);
    return true;
}

static bool fetch_items(const Config* cfg, const char* url, ApiItem** out_items, int* out_count, char* err, size_t err_sz) {
    *out_items = NULL;
    *out_count = 0;

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("GET", url, NULL, cfg->access_token, NULL, &buf, &status, err, err_sz);
    debug_log_response(url, buf.data);

    if (ok && status == 200 && buf.data) {
        if (!parse_items_json(buf.data, out_items, out_count)) {
            ok = false;
            snprintf(err, err_sz, "could not parse server response");
        }
    } else if (ok) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    free(buf.data);
    return ok;
}

bool api_get_items(const Config* cfg, const char* parent_id, ApiItem** out_items, int* out_count, char* err, size_t err_sz) {
    char url[512];
    snprintf(url, sizeof(url),
             "%s/Users/%s/Items?ParentId=%s&Fields=Overview&SortBy=SortName&SortOrder=Ascending",
             cfg->server, cfg->user_id, parent_id);
    return fetch_items(cfg, url, out_items, out_count, err, err_sz);
}

bool api_get_continue_watching(const Config* cfg, ApiItem** out_items, int* out_count, char* err, size_t err_sz) {
    char url[320];
    snprintf(url, sizeof(url),
             "%s/Users/%s/Items/Resume?Fields=Overview&Limit=200&Recursive=true"
             "&MediaTypes=Video&IncludeItemTypes=Movie,Episode",
             cfg->server, cfg->user_id);
    return fetch_items(cfg, url, out_items, out_count, err, err_sz);
}

bool api_search_items(const Config* cfg, const char* query, ApiItem** out_items, int* out_count, char* err, size_t err_sz) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        *out_items = NULL;
        *out_count = 0;
        snprintf(err, err_sz, "failed to initialize curl");
        return false;
    }
    char* escaped = curl_easy_escape(curl, query, 0);

    char url[768];
    snprintf(url, sizeof(url),
             "%s/Users/%s/Items?searchTerm=%s&Recursive=true&IncludeItemTypes=Movie,Series,Episode"
             "&Fields=Overview&SortBy=SortName",
             cfg->server, cfg->user_id, escaped ? escaped : "");

    if (escaped) curl_free(escaped);
    curl_easy_cleanup(curl);

    return fetch_items(cfg, url, out_items, out_count, err, err_sz);
}

bool api_get_ancestors(const Config* cfg, const char* item_id, AncestorNode* out, int max_out, int* out_count, char* err, size_t err_sz) {
    *out_count = 0;

    char url[350];
    snprintf(url, sizeof(url), "%s/Items/%s/Ancestors?userId=%s", cfg->server, item_id, cfg->user_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("GET", url, NULL, cfg->access_token, NULL, &buf, &status, err, err_sz);

    if (ok && status == 200 && buf.data) {
        // Unlike other endpoints, this one returns a bare JSON array, not
        // {"Items":[...]}.
        JsonValue* root = json_parse(buf.data);
        if (root) {
            int n = json_array_count(root);
            for (int i = 0; i < n && *out_count < max_out; i++) {
                const JsonValue* it = json_array_at(root, i);
                AncestorNode* node = &out[*out_count];
                snprintf(node->id, sizeof(node->id), "%s", json_get_string(it, "Id", ""));
                snprintf(node->name, sizeof(node->name), "%s", json_get_string(it, "Name", "Unnamed"));
                snprintf(node->type, sizeof(node->type), "%s", json_get_string(it, "Type", ""));
                (*out_count)++;
            }
            json_free(root);
        } else {
            ok = false;
            snprintf(err, err_sz, "could not parse server response");
        }
    } else if (ok) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    free(buf.data);
    return ok;
}

void api_build_stream_url(const Config* cfg, const char* item_id, char* out_url, size_t out_sz) {
    snprintf(out_url, out_sz, "%s/Videos/%s/stream?static=true&mediaSourceId=%s&api_key=%s",
             cfg->server, item_id, item_id, cfg->access_token);
}

static bool post_session_event(const Config* cfg, const char* path, const char* body, char* err, size_t err_sz) {
    char url[300];
    snprintf(url, sizeof(url), "%s%s", cfg->server, path);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("POST", url, NULL, cfg->access_token, body, &buf, &status, err, err_sz);
    debug_log_post(url, body, status, buf.data);
    if (ok && (status < 200 || status >= 300)) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }
    free(buf.data);
    return ok;
}

bool api_report_playback_start(const Config* cfg, const char* item_id, const char* media_source_id,
                                const char* play_session_id, long long position_ticks, char* err, size_t err_sz) {
    char body[512];
    snprintf(body, sizeof(body),
             "{\"ItemId\":\"%s\",\"MediaSourceId\":\"%s\",\"PlaySessionId\":\"%s\","
             "\"PositionTicks\":%lld,\"IsPaused\":false,\"CanSeek\":true,\"PlayMethod\":\"DirectPlay\"}",
             item_id, media_source_id, play_session_id, position_ticks);
    return post_session_event(cfg, "/Sessions/Playing", body, err, err_sz);
}

bool api_report_playback_progress(const Config* cfg, const char* item_id, const char* media_source_id,
                                   const char* play_session_id, long long position_ticks, bool is_paused,
                                   char* err, size_t err_sz) {
    char body[512];
    snprintf(body, sizeof(body),
             "{\"ItemId\":\"%s\",\"MediaSourceId\":\"%s\",\"PlaySessionId\":\"%s\","
             "\"PositionTicks\":%lld,\"IsPaused\":%s,\"CanSeek\":true,\"PlayMethod\":\"DirectPlay\"}",
             item_id, media_source_id, play_session_id, position_ticks, is_paused ? "true" : "false");
    return post_session_event(cfg, "/Sessions/Playing/Progress", body, err, err_sz);
}

bool api_report_playback_stopped(const Config* cfg, const char* item_id, const char* media_source_id,
                                  const char* play_session_id, long long position_ticks,
                                  char* err, size_t err_sz) {
    char body[300];
    snprintf(body, sizeof(body),
             "{\"ItemId\":\"%s\",\"MediaSourceId\":\"%s\",\"PlaySessionId\":\"%s\",\"PositionTicks\":%lld}",
             item_id, media_source_id, play_session_id, position_ticks);
    return post_session_event(cfg, "/Sessions/Playing/Stopped", body, err, err_sz);
}

bool api_set_played(const Config* cfg, const char* item_id, bool played, char* err, size_t err_sz) {
    char url[300];
    snprintf(url, sizeof(url), "%s/Users/%s/PlayedItems/%s", cfg->server, cfg->user_id, item_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request(played ? "POST" : "DELETE", url, NULL, cfg->access_token, "", &buf, &status, err, err_sz);
    if (ok && (status < 200 || status >= 300)) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }
    free(buf.data);
    return ok;
}

bool api_dump_raw_item(const Config* cfg, const char* item_id, const char* out_path, char* err, size_t err_sz) {
    char url[300];
    snprintf(url, sizeof(url), "%s/Users/%s/Items/%s", cfg->server, cfg->user_id, item_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("GET", url, NULL, cfg->access_token, NULL, &buf, &status, err, err_sz);
    if (ok && status != 200) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    if (ok) {
        FILE* f = fopen(out_path, "w");
        if (!f) {
            ok = false;
            snprintf(err, err_sz, "could not write %s", out_path);
        } else {
            fwrite(buf.data, 1, buf.len, f);
            fclose(f);
        }
    }

    free(buf.data);
    return ok;
}

bool api_get_media_streams(const Config* cfg, const char* item_id, MediaStreamInfo* out, int max_out,
                            int* out_count, char* err, size_t err_sz) {
    *out_count = 0;

    char url[350];
    snprintf(url, sizeof(url), "%s/Users/%s/Items/%s?Fields=MediaStreams", cfg->server, cfg->user_id, item_id);

    Buffer buf = {0};
    long status = 0;
    bool ok = http_request("GET", url, NULL, cfg->access_token, NULL, &buf, &status, err, err_sz);
    debug_log_response(url, buf.data);

    if (ok && status == 200 && buf.data) {
        JsonValue* root = json_parse(buf.data);
        if (root) {
            const JsonValue* sources = json_get(root, "MediaSources");
            const JsonValue* first_source = json_array_at(sources, 0);
            const JsonValue* streams = json_get(first_source, "MediaStreams");
            int n = json_array_count(streams);

            int audio_ordinal = 0, subtitle_ordinal = 0;
            for (int i = 0; i < n && *out_count < max_out; i++) {
                const JsonValue* s = json_array_at(streams, i);
                const char* type = json_get_string(s, "Type", "");
                bool is_audio = strcmp(type, "Audio") == 0;
                bool is_subtitle = strcmp(type, "Subtitle") == 0;
                if (!is_audio && !is_subtitle) continue;

                MediaStreamInfo* info = &out[*out_count];
                memset(info, 0, sizeof(*info));
                info->stream_index = (int)json_get_number(s, "Index", -1);
                info->track_number = is_audio ? ++audio_ordinal : ++subtitle_ordinal;
                snprintf(info->type, sizeof(info->type), "%s", type);
                snprintf(info->codec, sizeof(info->codec), "%s", json_get_string(s, "Codec", ""));
                snprintf(info->language, sizeof(info->language), "%s", json_get_string(s, "Language", ""));
                snprintf(info->display_title, sizeof(info->display_title), "%s", json_get_string(s, "DisplayTitle", ""));
                info->is_default = json_get_bool(s, "IsDefault", false);
                info->is_forced = json_get_bool(s, "IsForced", false);
                info->is_external = json_get_bool(s, "IsExternal", false);
                (*out_count)++;
            }
            json_free(root);
        } else {
            ok = false;
            snprintf(err, err_sz, "could not parse server response");
        }
    } else if (ok) {
        ok = false;
        snprintf(err, err_sz, "server returned HTTP %ld", status);
    }

    free(buf.data);
    return ok;
}

void api_build_subtitle_url(const Config* cfg, const char* item_id, const MediaStreamInfo* stream,
                             char* out_url, size_t out_sz) {
    const char* format = stream->codec[0] ? stream->codec : "srt";
    snprintf(out_url, out_sz, "%s/Videos/%s/%s/Subtitles/%d/Stream.%s?api_key=%s",
             cfg->server, item_id, item_id, stream->stream_index, format, cfg->access_token);
}
