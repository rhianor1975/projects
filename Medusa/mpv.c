// mpv.c - see mpv.h

#include "mpv.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/wait.h>

static bool connect_socket(const char* path, int* out_fd) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }
    *out_fd = fd;
    return true;
}

bool mpv_start(MpvPlayer* mpv, const char* stream_url, double start_seconds, char* err, size_t err_sz) {
    memset(mpv, 0, sizeof(*mpv));
    mpv->sock_fd = -1;

    snprintf(mpv->socket_path, sizeof(mpv->socket_path), "/tmp/medusa-mpv-%d.sock", (int)getpid());
    unlink(mpv->socket_path); // stale socket from a crashed previous run

    char ipc_arg[160];
    snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-server=%s", mpv->socket_path);

    char start_arg[32] = "";
    if (start_seconds > 0) snprintf(start_arg, sizeof(start_arg), "--start=%.2f", start_seconds);

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, err_sz, "fork failed");
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }

        char* argv[11];
        int argc = 0;
        argv[argc++] = "mpv";
        argv[argc++] = ipc_arg;
        argv[argc++] = "--quiet";
        argv[argc++] = "--really-quiet";
        argv[argc++] = "--force-window=yes";
        argv[argc++] = "--fs";
        // Don't auto-close at EOF - stay open so we can reliably observe
        // eof-reached over IPC instead of racing mpv's own exit.
        argv[argc++] = "--keep-open=yes";
        if (start_arg[0]) argv[argc++] = start_arg;
        argv[argc++] = (char*)stream_url;
        argv[argc] = NULL;

        execvp("mpv", argv);
        _exit(127); // execvp failed - mpv not found
    }

    mpv->pid = pid;

    for (int attempt = 0; attempt < 40; attempt++) { // ~4s max
        if (connect_socket(mpv->socket_path, &mpv->sock_fd)) break;
        usleep(100000);
    }

    if (mpv->sock_fd < 0) {
        int status;
        if (waitpid(mpv->pid, &status, WNOHANG) > 0) {
            snprintf(err, err_sz, "mpv exited immediately - is it installed?");
        } else {
            snprintf(err, err_sz, "could not connect to mpv's IPC socket");
            kill(mpv->pid, SIGTERM);
            waitpid(mpv->pid, NULL, 0);
        }
        mpv->pid = -1;
        return false;
    }

    return true;
}

bool mpv_is_running(MpvPlayer* mpv) {
    if (mpv->pid <= 0) return false;
    int status;
    pid_t r = waitpid(mpv->pid, &status, WNOHANG);
    if (r == 0) return true;
    mpv->pid = -1;
    return false;
}

// Reads IPC lines until one is a reply whose "request_id" matches
// expected_request_id, skipping unsolicited "event" lines AND replies to
// any OTHER in-flight request. mpv's get_property replies don't otherwise
// echo back which property they're answering, so without this check a
// reply that happens to arrive out of turn (e.g. buffered alongside an
// event burst right after a seek/loadfile) could get misattributed to the
// wrong query - matching by request_id makes that impossible instead of
// just unlikely. Returns a malloc'd JsonValue the caller must json_free(),
// or NULL on timeout/disconnect.
static JsonValue* read_reply(int fd, int timeout_ms, int expected_request_id) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    char buf[4096];
    size_t pos = 0;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining_ms = timeout_ms - elapsed_ms;
        if (remaining_ms <= 0) return NULL;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) return NULL;

        ssize_t n = read(fd, buf + pos, sizeof(buf) - pos - 1);
        if (n <= 0) return NULL;
        pos += (size_t)n;
        buf[pos] = '\0';

        char* line_start = buf;
        char* nl;
        while ((nl = strchr(line_start, '\n')) != NULL) {
            *nl = '\0';
            JsonValue* v = json_parse(line_start);
            line_start = nl + 1;
            if (v) {
                bool is_event = json_get(v, "event") != NULL;
                int rid = (int)json_get_number(v, "request_id", -1);
                if (!is_event && rid == expected_request_id) {
                    return v;
                }
                json_free(v); // an event, or a reply to some other request - keep waiting
            }
        }
        size_t remaining = pos - (size_t)(line_start - buf);
        if (remaining > 0) memmove(buf, line_start, remaining);
        pos = remaining;
    }
}

static int next_request_id(void) {
    static int id = 0;
    return ++id;
}

static JsonValue* query_property(int fd, const char* prop) {
    int rid = next_request_id();
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"get_property\",\"%s\"],\"request_id\":%d}\n", prop, rid);
    if (write(fd, cmd, strlen(cmd)) <= 0) return NULL;
    return read_reply(fd, 500, rid);
}

bool mpv_get_status(MpvPlayer* mpv, double* out_time_pos, double* out_duration, bool* out_paused,
                     bool* out_eof_reached, int* out_aid, int* out_sid) {
    if (mpv->sock_fd < 0) return false;

    JsonValue* reply = query_property(mpv->sock_fd, "time-pos");
    if (!reply) return false;
    const JsonValue* data = json_get(reply, "data");
    if (data && data->type == JSON_NUMBER) *out_time_pos = data->as.number;
    json_free(reply);

    reply = query_property(mpv->sock_fd, "duration");
    if (!reply) return false;
    data = json_get(reply, "data");
    if (data && data->type == JSON_NUMBER) *out_duration = data->as.number;
    json_free(reply);

    reply = query_property(mpv->sock_fd, "pause");
    if (!reply) return false;
    data = json_get(reply, "data");
    if (data && data->type == JSON_BOOL) *out_paused = data->as.boolean;
    json_free(reply);

    reply = query_property(mpv->sock_fd, "eof-reached");
    if (!reply) return false;
    data = json_get(reply, "data");
    if (data && data->type == JSON_BOOL) *out_eof_reached = data->as.boolean;
    json_free(reply);

    reply = query_property(mpv->sock_fd, "aid");
    if (!reply) return false;
    data = json_get(reply, "data");
    if (data && data->type == JSON_NUMBER) *out_aid = (int)data->as.number;
    else if (data && data->type == JSON_BOOL && !data->as.boolean) *out_aid = 0;
    json_free(reply);

    reply = query_property(mpv->sock_fd, "sid");
    if (!reply) return false;
    data = json_get(reply, "data");
    if (data && data->type == JSON_NUMBER) *out_sid = (int)data->as.number;
    else if (data && data->type == JSON_BOOL && !data->as.boolean) *out_sid = 0; // subtitles off
    json_free(reply);

    return true;
}

bool mpv_load_file(MpvPlayer* mpv, const char* stream_url) {
    if (mpv->sock_fd < 0) return false;
    char cmd[700];
    int n = snprintf(cmd, sizeof(cmd), "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}\n", stream_url);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return write(mpv->sock_fd, cmd, (size_t)n) > 0;
}

bool mpv_wait_for_file_loaded(MpvPlayer* mpv, int timeout_ms) {
    if (mpv->sock_fd < 0) return false;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    char buf[4096];
    size_t pos = 0;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining_ms = timeout_ms - elapsed_ms;
        if (remaining_ms <= 0) return false;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(mpv->sock_fd, &fds);
        struct timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int ret = select(mpv->sock_fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) return false;

        ssize_t n = read(mpv->sock_fd, buf + pos, sizeof(buf) - pos - 1);
        if (n <= 0) return false;
        pos += (size_t)n;
        buf[pos] = '\0';

        char* line_start = buf;
        char* nl;
        while ((nl = strchr(line_start, '\n')) != NULL) {
            *nl = '\0';
            JsonValue* v = json_parse(line_start);
            line_start = nl + 1;
            if (v) {
                const char* event = json_get_string(v, "event", NULL);
                bool is_loaded = event && strcmp(event, "file-loaded") == 0;
                json_free(v);
                if (is_loaded) return true;
            }
        }
        size_t remaining = pos - (size_t)(line_start - buf);
        if (remaining > 0) memmove(buf, line_start, remaining);
        pos = remaining;
    }
}

void mpv_seek_absolute(MpvPlayer* mpv, double seconds) {
    if (mpv->sock_fd < 0) return;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",%.2f,\"absolute\"]}\n", seconds);
    write(mpv->sock_fd, cmd, strlen(cmd));
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

void mpv_set_title(MpvPlayer* mpv, const char* title) {
    if (mpv->sock_fd < 0) return;
    char escaped[400];
    json_escape_into(escaped, sizeof(escaped), title);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"title\",\"%s\"]}\n", escaped);
    write(mpv->sock_fd, cmd, strlen(cmd));
}

void mpv_set_pause(MpvPlayer* mpv, bool paused) {
    if (mpv->sock_fd < 0) return;
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"pause\",%s]}\n", paused ? "true" : "false");
    write(mpv->sock_fd, cmd, strlen(cmd));
}

void mpv_set_audio_track(MpvPlayer* mpv, int track_number) {
    if (mpv->sock_fd < 0) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"aid\",%d]}\n", track_number);
    write(mpv->sock_fd, cmd, strlen(cmd));
}

void mpv_set_subtitle_track(MpvPlayer* mpv, int track_number) {
    if (mpv->sock_fd < 0) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"sid\",%d]}\n", track_number);
    write(mpv->sock_fd, cmd, strlen(cmd));
}

void mpv_disable_subtitles(MpvPlayer* mpv) {
    if (mpv->sock_fd < 0) return;
    const char cmd[] = "{\"command\":[\"set_property\",\"sid\",false]}\n";
    write(mpv->sock_fd, cmd, strlen(cmd));
}

bool mpv_add_subtitle(MpvPlayer* mpv, const char* url) {
    if (mpv->sock_fd < 0) return false;
    char cmd[900];
    int n = snprintf(cmd, sizeof(cmd), "{\"command\":[\"sub-add\",\"%s\",\"select\"]}\n", url);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return write(mpv->sock_fd, cmd, (size_t)n) > 0;
}

void mpv_stop(MpvPlayer* mpv) {
    if (mpv->sock_fd >= 0) {
        // Only bother asking a process we still believe is alive to quit -
        // if it already exited, the peer is gone and this would just be a
        // failed write() (harmless now that SIGPIPE is ignored, but pointless).
        if (mpv->pid > 0) {
            const char quit_cmd[] = "{\"command\":[\"quit\"]}\n";
            write(mpv->sock_fd, quit_cmd, strlen(quit_cmd));
        }
        close(mpv->sock_fd);
        mpv->sock_fd = -1;
    }
    if (mpv->pid > 0) {
        for (int i = 0; i < 20; i++) { // ~2s to quit cleanly
            int status;
            if (waitpid(mpv->pid, &status, WNOHANG) > 0) { mpv->pid = -1; break; }
            usleep(100000);
        }
        if (mpv->pid > 0) {
            kill(mpv->pid, SIGTERM);
            waitpid(mpv->pid, NULL, 0);
            mpv->pid = -1;
        }
    }
    unlink(mpv->socket_path);
}
