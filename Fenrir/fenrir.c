/*
 * Fenrir – version 1.0
 *
 * A terminal-based music player that uses mpv as the playback engine.
 * It provides a file browser, playlist management, metadata reading (via ffprobe),
 * persistent state (resume playback, volume, theme, sort order), and a responsive
 * ncurses interface with progress bars, chapter support, and multiple colour themes.
 *
 * Compile with:
 *   gcc -o fenrir fenrir.c -lncurses -lpthread
 *
 * Run with:
 *   ./fenrir [starting_directory]
 */

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <locale.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>

/* =========================== CONSTANTS =========================== */

#define MAX_ITEMS 4096            /* Maximum number of files/dirs shown in one directory */
#define MPV_SOCKET_PATH "/tmp/mpv_player_socket"  /* Unix socket for mpv IPC */
#define SOCKET_BUFFER_SIZE 65536  /* Buffer for reading mpv responses */
#define MAX_METADATA_QUEUE 4096   /* Queue size for background metadata loading */

/* =========================== ENUMS =========================== */

/* Sorting modes for the file list */
typedef enum {
    SORT_ALPHA,   /* Alphabetical by display name */
    SORT_TRACK,   /* By track number (from metadata), then disc */
    SORT_TYPE,    /* By file extension */
    SORT_SIZE     /* By file size (largest first) */
} SortMode;

/* Available colour themes */
typedef enum {
    THEME_GREEN,
    THEME_ORANGE,
    THEME_RED,
    THEME_BLUE,
    THEME_CYAN,
    THEME_MONO_WHITE,
    THEME_MONO_AMBER,
    THEME_MONO_GREEN
} ColorTheme;

/* =========================== DATA STRUCTURES =========================== */

/*
 * Item – represents a single entry in the current directory listing.
 * Could be a directory, a parent directory (".."), or an audio file.
 * Metadata is loaded asynchronously via a background thread.
 */
typedef struct {
    char name[PATH_MAX];          /* Filename (e.g., "song.mp3") */
    char display_name[256];       /* Cleaned‑up name for UI (no extension, underscores → spaces) */
    char full_path[PATH_MAX];     /* Absolute path to the file/directory */
    char bitrate[32];             /* Human‑readable bitrate (e.g., "320 kbps") or "N/A" */
    int is_audio;                 /* 1 if it's a recognised audio file */
    int is_dir;                   /* 1 if it's a directory (not a file) */
    int is_parent;                /* 1 for the special ".." entry */
    int duration;                 /* Duration in seconds (0 if unknown) */
    long long file_size;          /* Size in bytes */
    int metadata_loaded;          /* 1 when ffprobe has been run for this file */
    int track_num;                /* Track number from metadata (INT_MAX if absent) */
    int disc_num;                 /* Disc number (default 1) */
    int metadata_loading;         /* 1 if currently being processed by the metadata thread */
    int is_cue;                   /* 1 if it's a .cue sheet (treated specially) */
    int has_chapters;             /* 1 if the file supports chapters (e.g., .m4b) */
    int chapter_count;            /* Number of chapters (if known) */
} Item;

/*
 * PlayerState – holds the current playback status, progress, and mpv‑related flags.
 * Most fields are updated via mpv's property change events.
 */
typedef struct {
    int current_index;            /* Index in items[] of the currently playing file (-1 if none) */
    double progress_seconds;      /* Current playback position in seconds */
    double total_seconds;         /* Total duration of current file */
    int progress_percent;         /* Cached progress as percentage (0–100) */
    int is_paused;                /* 1 if playback is paused */
    int current_chapter;          /* Current chapter index (0‑based) */
    int total_chapters;           /* Total chapters in current file (0 if none) */
    char playing_file[PATH_MAX];  /* Filename of the file currently loaded in mpv */
    char playing_info[512];       /* Formatted "Now Playing: ..." string */
    int volume;                   /* Volume level (0–100) */
    int shuffle_mode;             /* 1 if shuffle is enabled */
    int *shuffle_order;           /* Array of indices into items[] in shuffled order; NULL if off */
    int audio_count;              /* Number of audio files (excluding CUE) in current directory */
    time_t last_update;           /* Last time we received a property update */
    int pending_resort;           /* Flag to trigger a re‑sort after metadata loads */
    int mpv_initialized;          /* 1 after we've sent "observe_property" commands */
    int chapter_mode;             /* 1 if user is manually navigating chapters */
    int pending_chapter;          /* Used during restore to seek to a chapter */
    char loading_file[PATH_MAX];  /* Used during restore to wait for chapter list */
    int loading_seek;             /* Seek position (seconds) to apply after chapters are known */
    int loading_chapter;          /* Chapter index to jump to after chapters are known */
    int loading_pending;          /* 1 if we're waiting for chapter‑list before seeking */
    int playlist_loaded;          /* 1 if we've sent a playlist to mpv (used for shuffle) */
} PlayerState;

/* =========================== GLOBALS =========================== */

Item items[MAX_ITEMS];            /* The current directory listing */
int item_count = 0;               /* Number of valid entries in items[] */
int selected = 0;                 /* Currently highlighted item index */
int top = 0;                      /* First visible item index in the list view */

char current_path[PATH_MAX];      /* The directory we are currently browsing */
pid_t mpv_pid = -1;               /* Process ID of the mpv child process */
volatile sig_atomic_t mpv_exited = 0;  /* Set by SIGCHLD when mpv dies */

int mpv_socket_fd = -1;           /* File descriptor for the Unix socket to mpv */
char socket_buffer[SOCKET_BUFFER_SIZE];   /* Buffer for assembling mpv responses */
size_t socket_buffer_len = 0;     /* Current length of data in socket_buffer */

char status_msg[256] = "";        /* Short status message shown at bottom */
time_t status_msg_time = 0;       /* Time when status_msg was set (for auto‑clear) */

int need_ui_update = 1;           /* Set to 1 when the screen must be redrawn */
int user_interacting = 0;         /* Set when user presses a key (for idle behaviour) */
time_t last_user_action = 0;      /* Time of last key press (unused currently) */
int eof_processing = 0;           /* Prevent re‑entrant handling of end‑of‑file events */

PlayerState player = {            /* Initialise player state */
    .current_index = -1,
    .progress_seconds = 0,
    .total_seconds = 0,
    .progress_percent = 0,
    .is_paused = 0,
    .current_chapter = 0,
    .total_chapters = 0,
    .volume = 100,
    .shuffle_mode = 0,
    .shuffle_order = NULL,
    .audio_count = 0,
    .last_update = 0,
    .pending_resort = 0,
    .mpv_initialized = 0,
    .chapter_mode = 0,
    .pending_chapter = -1,
    .loading_pending = 0,
    .loading_seek = 0,
    .loading_chapter = -1,
    .playing_file = "",
    .playlist_loaded = 0
};

SortMode current_sort = SORT_ALPHA;           /* Current sorting method */
const char *sort_names[] = {"Alphabetical", "Track / Part #", "File Extension", "File Size"};
const char *theme_names[] = {"Green", "Orange", "Red", "Blue", "Cyan",
                             "Mono-White", "Mono-Amber", "Mono-Green"};
ColorTheme current_theme = THEME_GREEN;

/* NCurses colour pair indices */
#define COLOR_HEADER      1
#define COLOR_NORMAL      2
#define COLOR_SELECTED    3
#define COLOR_STATUS      4
#define COLOR_PROGRESS    5
#define COLOR_TITLE       6
#define COLOR_SHUFFLE     7
#define COLOR_PARENT      8
#define COLOR_DIR         9
#define COLOR_PLAYING     10
#define COLOR_CUE         11
#define COLOR_CHAPTER     12

/* =========================== METADATA THREAD =========================== */

/*
 * A single background thread loads metadata (duration, bitrate, track number, etc.)
 * using ffprobe. Items are queued as they become visible or when the directory is
 * scanned. This prevents blocking the UI while ffprobe runs.
 */
pthread_mutex_t item_mutex = PTHREAD_MUTEX_INITIALIZER;  /* Protects items[] and related globals */
pthread_t metadata_thread;
int metadata_thread_running = 0;

/* Ring buffer for metadata queue */
int metadata_queue[MAX_METADATA_QUEUE];
int metadata_queue_size = 0;
int metadata_queue_head = 0;
int metadata_queue_tail = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

/* =========================== FUNCTION PROTOTYPES =========================== */

void setup_colors(ColorTheme theme);
void clean_filename_for_display(const char *filename, char *display);
int is_audio_file(const char *filename);
int is_item_cue(int index);
int is_playing_cue(void);
void get_audio_info_sync(int index);
int file_has_chapters(const char *filename);
void *metadata_loader_thread(void *arg);
void queue_metadata_load(int index);
void clear_metadata_queue(void);
void reset_metadata_state(void);
void scan_directory(void);
void play_file_by_name(const char *filename);
void play_file(const char *file);
void play_file_paused_at(const char *file, int seconds, int chapter);
void draw_progress_bar(int row, int max_width);
void draw(void);
void open_selected(void);
void go_parent(void);
void next_file(void);
void previous_file(void);
void next_chapter(void);
void previous_chapter(void);
void toggle_shuffle(void);
void shuffle_list(void);
void cycle_sort(void);
void sigchld_handler(int sig);
void mpv_open_socket(void);
void mpv_close_socket(void);
int mpv_send_json(const char *json);
void mpv_seek_relative(int seconds);
void mpv_toggle_pause(void);
void mpv_quit(void);
void mpv_poll_time(void);
void mpv_set_volume(int vol);
void mpv_adjust_volume(int delta);
void start_persistent_mpv(void);
void sync_player_state(void);
void clear_status_after_delay(void);
void save_state(void);
int load_state(char *out_path, char *out_file, int *out_seconds, int *out_theme, int *out_sort);
int load_chapter_from_state(void);
void process_mpv_responses(void);
void cleanup_metadata_thread(void);
void drain_mpv_socket(void);
void handle_end_file_event(const char *line);
void handle_property_change(const char *line);
void mpv_initialize_ipc(void);
static int parse_mpv_data_double(const char *line, double *out);
static int parse_mpv_data_bool(const char *line, int *out);
static int parse_mpv_data_string(const char *line, char *out_path, size_t size);
static int parse_mpv_data_int(const char *line, int *out);
int find_item_index(const char *filename);
void create_mpv_playlist(void);
void mpv_playlist_next(void);
void mpv_playlist_prev(void);
static void shell_escape(const char *in, char *out, size_t out_size);
static void json_escape(const char *in, char *out, size_t out_size);

/* =========================== ESCAPING HELPERS ===========================
 *
 * Filenames come straight from the filesystem and must never be trusted
 * as-is when they're about to be embedded into a shell command string
 * (popen) or a JSON command string (mpv IPC). These helpers make that
 * embedding safe.
 *
 * shell_escape() takes a raw filename and wraps it in single quotes for a
 * POSIX shell, escaping any embedded single quotes as '\''.
 *
 * json_escape() escapes backslashes, double quotes, newlines, and tabs,
 * and drops other control characters to produce a valid JSON string.
 *
 * Both functions truncate safely if the output buffer is too small.
 */

/* Wrap `in` in single quotes for use in a POSIX shell command, escaping
 * any embedded single quotes as '\'' . Truncates safely if out_size is
 * too small (never overflows out). */
static void shell_escape(const char *in, char *out, size_t out_size) {
    if (out_size == 0) return;
    size_t oi = 0;
    out[oi++] = '\'';
    for (size_t i = 0; in[i] != '\0'; i++) {
        if (in[i] == '\'') {
            if (oi + 4 >= out_size - 1) break; /* leave room for closing quote + NUL */
            out[oi++] = '\'';
            out[oi++] = '\\';
            out[oi++] = '\'';
            out[oi++] = '\'';
        } else {
            if (oi + 1 >= out_size - 1) break;
            out[oi++] = in[i];
        }
    }
    if (oi < out_size - 1) out[oi++] = '\'';
    out[oi] = '\0';
}

/* Escape `in` for safe embedding inside a JSON string literal. Truncates
 * safely if out_size is too small (never overflows out). */
static void json_escape(const char *in, char *out, size_t out_size) {
    if (out_size == 0) return;
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (oi + 2 >= out_size) break;
            out[oi++] = '\\';
            out[oi++] = (char)c;
        } else if (c == '\n') {
            if (oi + 2 >= out_size) break;
            out[oi++] = '\\'; out[oi++] = 'n';
        } else if (c == '\t') {
            if (oi + 2 >= out_size) break;
            out[oi++] = '\\'; out[oi++] = 't';
        } else if (c < 0x20) {
            continue; /* drop other control characters */
        } else {
            out[oi++] = (char)c;
        }
    }
    out[oi] = '\0';
}

/* =========================== STATE MANAGEMENT =========================== */

/* Returns the path to the state file (e.g., ~/.fenrir_state) */
static char *get_state_file_path(void) {
    static char path[PATH_MAX];
    char *home = getenv("HOME");
    if (!home) return NULL;
    snprintf(path, sizeof(path), "%s/.fenrir_state", home);
    return path;
}

/* Save current settings and playback position to the state file */
void save_state(void) {
    char *state_file = get_state_file_path();
    if (!state_file) return;

    FILE *f = fopen(state_file, "w");
    if (!f) return;

    fprintf(f, "# Fenrir State File\n");
    fprintf(f, "# Format: KEY=VALUE\n\n");
    
    fprintf(f, "THEME=%d\n", (int)current_theme);
    fprintf(f, "SORT=%d\n", (int)current_sort);
    fprintf(f, "VOLUME=%d\n", player.volume);
    fprintf(f, "SHUFFLE_MODE=%d\n", player.shuffle_mode);
    fprintf(f, "LAST_DIR=%s\n", current_path);
    
    if (player.playing_file[0] != '\0') {
        fprintf(f, "CURRENT_FILE=%s\n", player.playing_file);
        fprintf(f, "CURRENT_SECONDS=%d\n", (int)player.progress_seconds);
        fprintf(f, "CURRENT_CHAPTER=%d\n", player.current_chapter);
        fprintf(f, "PLAYING_PAUSED=%d\n", player.is_paused);
    }

    fclose(f);
}

/* Load saved state; returns 1 if a valid directory was found. */
int load_state(char *out_path, char *out_file, int *out_seconds, int *out_theme, int *out_sort) {
    char *state_file = get_state_file_path();
    if (!state_file) return 0;

    FILE *f = fopen(state_file, "r");
    if (!f) return 0;

    char line[1024];
    if (out_path) out_path[0] = '\0';
    if (out_file) out_file[0] = '\0';
    if (out_seconds) *out_seconds = 0;
    if (out_theme) *out_theme = (int)current_theme;
    if (out_sort) *out_sort = (int)current_sort;
    
    int found_path = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "LAST_DIR=", 9) == 0) {
            strncpy(out_path, line + 9, PATH_MAX - 1);
            out_path[PATH_MAX - 1] = '\0';
            found_path = 1;
        } else if (strncmp(line, "CURRENT_FILE=", 13) == 0) {
            strncpy(out_file, line + 13, PATH_MAX - 1);
            out_file[PATH_MAX - 1] = '\0';
        } else if (strncmp(line, "CURRENT_SECONDS=", 16) == 0) {
            if (out_seconds) *out_seconds = atoi(line + 16);
        } else if (strncmp(line, "THEME=", 6) == 0) {
            if (out_theme) *out_theme = atoi(line + 6);
        } else if (strncmp(line, "SORT=", 5) == 0) {
            if (out_sort) *out_sort = atoi(line + 5);
        } else if (strncmp(line, "VOLUME=", 7) == 0) {
            player.volume = atoi(line + 7);
        } else if (strncmp(line, "SHUFFLE_MODE=", 13) == 0) {
            player.shuffle_mode = atoi(line + 13);
        } else if (strncmp(line, "PLAYING_PAUSED=", 15) == 0) {
            player.is_paused = atoi(line + 15);
        }
    }

    fclose(f);
    return found_path;
}

/* Load only the chapter number from state (used when resuming) */
int load_chapter_from_state(void) {
    char *state_file = get_state_file_path();
    if (!state_file) return -1;
    
    FILE *f = fopen(state_file, "r");
    if (!f) return -1;
    
    char line[1024];
    int chapter = -1;
    
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "CURRENT_CHAPTER=", 16) == 0) {
            chapter = atoi(line + 16);
            break;
        }
    }
    
    fclose(f);
    return chapter;
}

/* =========================== MPV PLAYLIST MANAGEMENT =========================== */

/*
 * Create a temporary M3U playlist containing all audio files (excluding .cue)
 * in the current directory and tell mpv to load it. This is used for shuffle
 * and to enable playlist‑next/prev commands.
 */
void create_mpv_playlist(void) {
    char playlist_path[PATH_MAX];
    snprintf(playlist_path, sizeof(playlist_path), "/tmp/music_playlist_%d.m3u", getpid());
    
    FILE *f = fopen(playlist_path, "w");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), "Failed to create playlist");
        status_msg_time = time(NULL);
        return;
    }
    
    int track_count = 0;
    pthread_mutex_lock(&item_mutex);
    for (int i = 0; i < item_count; i++) {
        if (items[i].is_audio && !items[i].is_parent && !items[i].is_cue) {
            fprintf(f, "%s\n", items[i].full_path);
            track_count++;
        }
    }
    pthread_mutex_unlock(&item_mutex);
    fclose(f);
    
    if (track_count == 0) {
        unlink(playlist_path);
        return;
    }
    
    char cmd[PATH_MAX + 256];
    snprintf(cmd, sizeof(cmd), 
             "{\"command\":[\"loadlist\",\"%s\",\"replace\"]}", playlist_path);
    if (mpv_send_json(cmd) == 0) {
        player.playlist_loaded = 1;
        snprintf(status_msg, sizeof(status_msg), "Loaded playlist with %d tracks", track_count);
        status_msg_time = time(NULL);
    }
    
    unlink(playlist_path);
}

/* Tell mpv to move to the next file in the playlist */
void mpv_playlist_next(void) {
    if (mpv_pid > 0) {
        mpv_send_json("{\"command\":[\"playlist-next\"]}");
        need_ui_update = 1;
    }
}

/* Tell mpv to move to the previous file in the playlist */
void mpv_playlist_prev(void) {
    if (mpv_pid > 0) {
        mpv_send_json("{\"command\":[\"playlist-prev\"]}");
        need_ui_update = 1;
    }
}

/* =========================== COLOUR SETUP =========================== */

/* Initialise ncurses colour pairs according to the selected theme */
void setup_colors(ColorTheme theme) {
    start_color();
    use_default_colors();
    
    int default_bg = -1; /* -1 means use terminal default background */
    
    switch(theme) {
        case THEME_GREEN:
            init_pair(COLOR_HEADER, COLOR_GREEN, default_bg);
            init_pair(COLOR_NORMAL, COLOR_GREEN, default_bg);
            init_pair(COLOR_SELECTED, COLOR_WHITE, COLOR_GREEN);
            init_pair(COLOR_STATUS, COLOR_GREEN, default_bg);
            init_pair(COLOR_PROGRESS, COLOR_GREEN, default_bg);
            init_pair(COLOR_TITLE, COLOR_GREEN, default_bg);
            init_pair(COLOR_SHUFFLE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_PARENT, COLOR_YELLOW, default_bg);
            init_pair(COLOR_DIR, COLOR_CYAN, default_bg);
            init_pair(COLOR_PLAYING, COLOR_GREEN, default_bg);
            init_pair(COLOR_CUE, COLOR_MAGENTA, default_bg);
            init_pair(COLOR_CHAPTER, COLOR_CYAN, default_bg);
            break;
        case THEME_ORANGE:
            init_pair(COLOR_HEADER, COLOR_YELLOW, default_bg);
            init_pair(COLOR_NORMAL, COLOR_YELLOW, default_bg);
            init_pair(COLOR_SELECTED, COLOR_WHITE, COLOR_YELLOW);
            init_pair(COLOR_STATUS, COLOR_YELLOW, default_bg);
            init_pair(COLOR_PROGRESS, COLOR_YELLOW, default_bg);
            init_pair(COLOR_TITLE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_SHUFFLE, COLOR_CYAN, default_bg);
            init_pair(COLOR_PARENT, COLOR_CYAN, default_bg);
            init_pair(COLOR_DIR, COLOR_GREEN, default_bg);
            init_pair(COLOR_PLAYING, COLOR_YELLOW, default_bg);
            init_pair(COLOR_CUE, COLOR_MAGENTA, default_bg);
            init_pair(COLOR_CHAPTER, COLOR_CYAN, default_bg);
            break;
        case THEME_RED:
            init_pair(COLOR_HEADER, COLOR_RED, default_bg);
            init_pair(COLOR_NORMAL, COLOR_RED, default_bg);
            init_pair(COLOR_SELECTED, COLOR_WHITE, COLOR_RED);
            init_pair(COLOR_STATUS, COLOR_RED, default_bg);
            init_pair(COLOR_PROGRESS, COLOR_RED, default_bg);
            init_pair(COLOR_TITLE, COLOR_RED, default_bg);
            init_pair(COLOR_SHUFFLE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_PARENT, COLOR_YELLOW, default_bg);
            init_pair(COLOR_DIR, COLOR_CYAN, default_bg);
            init_pair(COLOR_PLAYING, COLOR_RED, default_bg);
            init_pair(COLOR_CUE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_CHAPTER, COLOR_CYAN, default_bg);
            break;
        case THEME_BLUE:
            init_pair(COLOR_HEADER, COLOR_BLUE, default_bg);
            init_pair(COLOR_NORMAL, COLOR_BLUE, default_bg);
            init_pair(COLOR_SELECTED, COLOR_WHITE, COLOR_BLUE);
            init_pair(COLOR_STATUS, COLOR_BLUE, default_bg);
            init_pair(COLOR_PROGRESS, COLOR_BLUE, default_bg);
            init_pair(COLOR_TITLE, COLOR_BLUE, default_bg);
            init_pair(COLOR_SHUFFLE, COLOR_CYAN, default_bg);
            init_pair(COLOR_PARENT, COLOR_CYAN, default_bg);
            init_pair(COLOR_DIR, COLOR_GREEN, default_bg);
            init_pair(COLOR_PLAYING, COLOR_BLUE, default_bg);
            init_pair(COLOR_CUE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_CHAPTER, COLOR_CYAN, default_bg);
            break;
        case THEME_CYAN:
            init_pair(COLOR_HEADER, COLOR_CYAN, default_bg);
            init_pair(COLOR_NORMAL, COLOR_CYAN, default_bg);
            init_pair(COLOR_SELECTED, COLOR_WHITE, COLOR_CYAN);
            init_pair(COLOR_STATUS, COLOR_CYAN, default_bg);
            init_pair(COLOR_PROGRESS, COLOR_CYAN, default_bg);
            init_pair(COLOR_TITLE, COLOR_CYAN, default_bg);
            init_pair(COLOR_SHUFFLE, COLOR_YELLOW, default_bg);
            init_pair(COLOR_PARENT, COLOR_YELLOW, default_bg);
            init_pair(COLOR_DIR, COLOR_GREEN, default_bg);
            init_pair(COLOR_PLAYING, COLOR_CYAN, default_bg);
            init_pair(COLOR_CUE, COLOR_MAGENTA, default_bg);
            init_pair(COLOR_CHAPTER, COLOR_CYAN, default_bg);
            break;
        case THEME_MONO_WHITE:
            for (int i = 1; i <= 12; i++) init_pair(i, COLOR_WHITE, default_bg);
            break;
        case THEME_MONO_AMBER:
            for (int i = 1; i <= 12; i++) init_pair(i, COLOR_YELLOW, default_bg);
            break;
        case THEME_MONO_GREEN:
            for (int i = 1; i <= 12; i++) init_pair(i, COLOR_GREEN, default_bg);
            break;
    }
}

/* =========================== FILE NAME HELPERS =========================== */

/*
 * Convert a filename into a display‑friendly version:
 * - strip extension
 * - replace underscores and hyphens with spaces
 * - capitalise the first letter of each word
 */
void clean_filename_for_display(const char *filename, char *display) {
    char temp[256];
    strncpy(temp, filename, sizeof(temp)-1);
    temp[sizeof(temp)-1] = '\0';
    char *dot = strrchr(temp, '.');
    if (dot) *dot = '\0';
    for (int i = 0; temp[i]; i++) {
        if (temp[i] == '_' || temp[i] == '-') temp[i] = ' ';
    }
    int cap_next = 1;
    for (int i = 0; temp[i]; i++) {
        if (cap_next && temp[i] >= 'a' && temp[i] <= 'z') {
            temp[i] = temp[i] - 32;
            cap_next = 0;
        } else if (temp[i] == ' ') cap_next = 1;
    }
    strncpy(display, temp, 255);
    display[255] = '\0';
}

/* Determine if a file is a recognised audio format (including .cue) */
int is_audio_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".mp3") == 0 ||
            strcasecmp(ext, ".flac") == 0 ||
            strcasecmp(ext, ".wav") == 0 ||
            strcasecmp(ext, ".ogg") == 0 ||
            strcasecmp(ext, ".m4a") == 0 ||
            strcasecmp(ext, ".m4b") == 0 ||
            strcasecmp(ext, ".wma") == 0 ||
            strcasecmp(ext, ".aac") == 0 ||
            strcasecmp(ext, ".opus") == 0 ||
            strcasecmp(ext, ".cue") == 0);
}

/* Check if item at index is a CUE sheet */
int is_item_cue(int index) {
    if (index < 0 || index >= item_count) return 0;
    return items[index].is_cue;
}

/* Check if the currently playing item is a CUE sheet */
int is_playing_cue(void) {
    return is_item_cue(player.current_index);
}

/* Determine if a file likely supports chapters (e.g., .m4b) */
int file_has_chapters(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".m4b") == 0 || strcasecmp(ext, ".m4a") == 0);
}

/* =========================== METADATA LOADING =========================== */

/* Reset all metadata flags and clear the queue (called when changing directory) */
void reset_metadata_state(void) {
    pthread_mutex_lock(&item_mutex);
    for (int i = 0; i < item_count; i++) {
        items[i].metadata_loaded = 0;
        items[i].metadata_loading = 0;
        items[i].duration = 0;
        strcpy(items[i].bitrate, "N/A");
        items[i].track_num = INT_MAX;
        items[i].disc_num = 1;
        items[i].has_chapters = 0;
        items[i].chapter_count = 0;
    }
    pthread_mutex_unlock(&item_mutex);
    pthread_mutex_lock(&queue_mutex);
    metadata_queue_size = 0;
    metadata_queue_head = 0;
    metadata_queue_tail = 0;
    pthread_mutex_unlock(&queue_mutex);
}

/*
 * Synchronously fetch metadata for one file using ffprobe.
 * This is called by the metadata thread. It takes a copy of the item's
 * path and name while holding the lock, then releases the lock while
 * ffprobe runs to avoid blocking the UI.
 */
void get_audio_info_sync(int index) {
    pthread_mutex_lock(&item_mutex);
    if (index < 0 || index >= item_count) { pthread_mutex_unlock(&item_mutex); return; }
    if (items[index].metadata_loaded) { pthread_mutex_unlock(&item_mutex); return; }
    if (!items[index].metadata_loading) items[index].metadata_loading = 1;

    /* Take a private snapshot of everything we need while holding the
     * lock. This item may be moved around (or replaced) by a concurrent
     * qsort() the instant we release the mutex, so we must never touch
     * items[index] again without re-checking identity under the lock. */
    int is_cue = items[index].is_cue;
    char full_path[PATH_MAX];
    char name_copy[PATH_MAX];
    strncpy(full_path, items[index].full_path, sizeof(full_path) - 1);
    full_path[sizeof(full_path) - 1] = '\0';
    strncpy(name_copy, items[index].name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';
    pthread_mutex_unlock(&item_mutex);

    int duration = 0;
    char bitrate[32] = "N/A";
    int track_num = INT_MAX, disc_num = 1;
    int has_chapters = 0;

    /* CUE sheets don't have real audio metadata; just mark them quickly */
    if (is_cue) {
        pthread_mutex_lock(&item_mutex);
        if (index < item_count && strcmp(items[index].name, name_copy) == 0) {
            items[index].duration = 0;
            strcpy(items[index].bitrate, "CUE Sheet");
            items[index].track_num = 1;
            items[index].disc_num = 1;
            items[index].metadata_loaded = 1;
            items[index].metadata_loading = 0;
        }
        pthread_mutex_unlock(&item_mutex);
        player.pending_resort = 1;
        need_ui_update = 1;
        return;
    }

    has_chapters = file_has_chapters(name_copy);

    /* Build the ffprobe command with the path safely single-quoted for
     * the shell, since filenames on disk are untrusted input and must
     * never be interpolated raw into a popen() command string. */
    char escaped_path[PATH_MAX * 4 + 8];
    shell_escape(full_path, escaped_path, sizeof(escaped_path));

    char cmd[PATH_MAX * 4 + 300], buffer[256];
    FILE *fp;
    snprintf(cmd, sizeof(cmd),
             "ffprobe -v error -show_entries format=bit_rate,duration:format_tags=track,disc -of default=noprint_wrappers=1 %s 2>/dev/null",
             escaped_path);
    fp = popen(cmd, "r");
    if (fp) {
        double rate_bps = 0.0, dur_sec = 0.0;
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0';
            if (strncmp(buffer, "bit_rate=", 9) == 0) rate_bps = strtod(buffer+9, NULL);
            else if (strncmp(buffer, "duration=", 9) == 0) dur_sec = strtod(buffer+9, NULL);
            else if (strncasecmp(buffer, "TAG:track=", 10) == 0) track_num = atoi(buffer+10);
            else if (strncasecmp(buffer, "TAG:disc=", 9) == 0) { disc_num = atoi(buffer+9); if (disc_num <= 0) disc_num = 1; }
        }
        pclose(fp);
        if (rate_bps > 0.0) snprintf(bitrate, 32, "%.0f kbps", rate_bps/1000.0);
        if (dur_sec > 0.0 && dur_sec < 100*3600.0) duration = (int)(dur_sec + 0.5);
        if (strcmp(bitrate, "N/A") == 0 && duration > 0) {
            struct stat st;
            if (stat(full_path, &st) == 0) {
                double kbps = ((double)st.st_size * 8.0 / duration) / 1000.0;
                snprintf(bitrate, 32, "%.0f kbps", kbps);
            }
        }
        if (duration <= 0) duration = 180;  /* fallback if ffprobe gives nothing */
    }

    pthread_mutex_lock(&item_mutex);
    /* Only write the result back if this slot still holds the same item
     * we looked up -- a concurrent sort may have moved it (or the
     * directory may have been rescanned) while ffprobe was running. */
    if (index < item_count && strcmp(items[index].name, name_copy) == 0) {
        items[index].duration = duration;
        strcpy(items[index].bitrate, bitrate);
        items[index].track_num = track_num;
        items[index].disc_num = disc_num;
        items[index].metadata_loaded = 1;
        items[index].metadata_loading = 0;
        items[index].has_chapters = has_chapters;
    }
    pthread_mutex_unlock(&item_mutex);

    if (track_num != INT_MAX || current_sort == SORT_TRACK) player.pending_resort = 1;
    need_ui_update = 1;
}

/* The metadata loader thread: waits for items on the queue and calls get_audio_info_sync() */
void *metadata_loader_thread(void *arg) {
    (void)arg;
    while (metadata_thread_running) {
        pthread_mutex_lock(&queue_mutex);
        while (metadata_queue_size == 0 && metadata_thread_running)
            pthread_cond_wait(&queue_cond, &queue_mutex);
        if (!metadata_thread_running) { pthread_mutex_unlock(&queue_mutex); break; }
        int index = metadata_queue[metadata_queue_head];
        metadata_queue_head = (metadata_queue_head + 1) % MAX_METADATA_QUEUE;
        metadata_queue_size--;
        pthread_mutex_unlock(&queue_mutex);
        get_audio_info_sync(index);
        usleep(1000); /* tiny delay to avoid hammering the CPU */
    }
    return NULL;
}

/* Queue an item for metadata loading if it hasn't been loaded yet */
void queue_metadata_load(int index) {
    if (index < 0 || index >= item_count) return;
    pthread_mutex_lock(&item_mutex);
    if (items[index].metadata_loaded) { pthread_mutex_unlock(&item_mutex); return; }
    pthread_mutex_unlock(&item_mutex);
    pthread_mutex_lock(&queue_mutex);
    if (metadata_queue_size < MAX_METADATA_QUEUE) {
        metadata_queue[metadata_queue_tail] = index;
        metadata_queue_tail = (metadata_queue_tail + 1) % MAX_METADATA_QUEUE;
        metadata_queue_size++;
        pthread_cond_signal(&queue_cond);
    }
    pthread_mutex_unlock(&queue_mutex);
}

/* Empty the metadata queue (used when changing directory) */
void clear_metadata_queue(void) {
    pthread_mutex_lock(&queue_mutex);
    metadata_queue_size = 0;
    metadata_queue_head = 0;
    metadata_queue_tail = 0;
    pthread_mutex_unlock(&queue_mutex);
}

/* =========================== SORTING =========================== */

/* Comparison function for qsort; uses global current_sort */
int compare_items(const void *a, const void *b) {
    const Item *item_a = (const Item *)a;
    const Item *item_b = (const Item *)b;
    /* Parent ("..") always comes first */
    if (item_a->is_parent) return -1;
    if (item_b->is_parent) return 1;
    /* CUE sheets appear before normal audio */
    if (item_a->is_cue && !item_b->is_cue) return -1;
    if (!item_a->is_cue && item_b->is_cue) return 1;
    /* Directories come before files */
    if (item_a->is_dir && !item_b->is_dir) return -1;
    if (!item_a->is_dir && item_b->is_dir) return 1;

    switch (current_sort) {
        case SORT_TRACK: {
            int d1 = item_a->disc_num, d2 = item_b->disc_num;
            if (d1 != d2) return (d1 < d2) ? -1 : 1;
            int t1 = item_a->track_num, t2 = item_b->track_num;
            if (t1 != t2) return (t1 < t2) ? -1 : 1;
            break;
        }
        case SORT_TYPE: {
            const char *ext_a = strrchr(item_a->name, '.');
            const char *ext_b = strrchr(item_b->name, '.');
            int comp = strcasecmp(ext_a ? ext_a : "", ext_b ? ext_b : "");
            if (comp != 0) return comp;
            break;
        }
        case SORT_SIZE:
            if (item_a->file_size != item_b->file_size)
                return (item_a->file_size > item_b->file_size) ? -1 : 1;
            break;
        default: break;
    }
    return strcasecmp(item_a->display_name, item_b->display_name);
}

/* Find the index of an item by its filename (not full path) */
int find_item_index(const char *filename) {
    if (!filename || filename[0] == '\0') return -1;
    for (int i = 0; i < item_count; i++)
        if (strcmp(items[i].name, filename) == 0) return i;
    return -1;
}

/* =========================== DIRECTORY SCANNING =========================== */

/* Scan the current directory, populate items[], and sort. Also queues metadata loading. */
void scan_directory(void) {
    DIR *dir;
    struct dirent *entry;
    reset_metadata_state();
    clear_metadata_queue();
    player.pending_resort = 0;
    player.chapter_mode = 0;
    player.playlist_loaded = 0;
    item_count = 0;
    selected = 0;
    top = 0;
    status_msg[0] = '\0';
    player.current_index = -1;
    player.audio_count = 0;
    need_ui_update = 1;

    dir = opendir(current_path);
    if (!dir) {
        snprintf(status_msg, sizeof(status_msg), "Cannot open directory: %s", strerror(errno));
        status_msg_time = time(NULL);
        return;
    }

    /* Add the ".." parent entry */
    strcpy(items[item_count].name, "..");
    strcpy(items[item_count].display_name, ".. (Parent)");
    strcpy(items[item_count].full_path, current_path);
    strcpy(items[item_count].bitrate, "");
    items[item_count].is_audio = 0;
    items[item_count].is_dir = 1;
    items[item_count].is_parent = 1;
    items[item_count].duration = 0;
    items[item_count].file_size = 0;
    items[item_count].metadata_loaded = 1;
    items[item_count].metadata_loading = 0;
    items[item_count].is_cue = 0;
    items[item_count].has_chapters = 0;
    items[item_count].chapter_count = 0;
    item_count++;

    while ((entry = readdir(dir)) != NULL && item_count < MAX_ITEMS) {
        if (entry->d_name[0] == '.') continue;  /* skip hidden files */
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", current_path, entry->d_name);
        struct stat st;
        if (stat(full, &st) == -1) continue;

        pthread_mutex_lock(&item_mutex);
        strncpy(items[item_count].name, entry->d_name, sizeof(items[item_count].name)-1);
        items[item_count].name[sizeof(items[item_count].name)-1] = '\0';
        strncpy(items[item_count].full_path, full, sizeof(items[item_count].full_path)-1);
        items[item_count].full_path[sizeof(items[item_count].full_path)-1] = '\0';
        items[item_count].is_parent = 0;
        items[item_count].file_size = st.st_size;
        items[item_count].metadata_loaded = 0;
        items[item_count].metadata_loading = 0;
        items[item_count].duration = 0;
        strcpy(items[item_count].bitrate, "N/A");
        items[item_count].track_num = INT_MAX;
        items[item_count].disc_num = 1;
        items[item_count].is_cue = 0;
        items[item_count].has_chapters = 0;
        items[item_count].chapter_count = 0;
        pthread_mutex_unlock(&item_mutex);

        if (S_ISDIR(st.st_mode)) {
            items[item_count].is_dir = 1;
            items[item_count].is_audio = 0;
            snprintf(items[item_count].display_name, sizeof(items[item_count].display_name), "%s/", entry->d_name);
            strcpy(items[item_count].bitrate, "");
            items[item_count].duration = 0;
            items[item_count].metadata_loaded = 1;
            item_count++;
        } else if (is_audio_file(entry->d_name)) {
            items[item_count].is_dir = 0;
            items[item_count].is_audio = 1;
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, ".cue") == 0) items[item_count].is_cue = 1;
            clean_filename_for_display(entry->d_name, items[item_count].display_name);
            queue_metadata_load(item_count);
            item_count++;
        }
    }
    closedir(dir);
    pthread_mutex_lock(&item_mutex);
    qsort(items, item_count, sizeof(Item), compare_items);
    pthread_mutex_unlock(&item_mutex);

    /* Restore selection if we have a currently playing file */
    if (player.playing_file[0] != '\0') {
        int idx = find_item_index(player.playing_file);
        if (idx >= 0) { player.current_index = idx; selected = idx; }
    }
    player.audio_count = 0;
    for (int i = 0; i < item_count; i++)
        if (items[i].is_audio && !is_item_cue(i)) player.audio_count++;
    if (player.shuffle_mode) shuffle_list();
}

/* =========================== SHUFFLE =========================== */

/* Build a shuffled order array of audio indices */
void shuffle_list(void) {
    int audio_count = 0;
    int audio_indices[MAX_ITEMS];
    for (int i = 0; i < item_count; i++) {
        if (items[i].is_audio && !is_item_cue(i)) audio_indices[audio_count++] = i;
    }
    player.audio_count = audio_count;
    if (player.shuffle_order) { free(player.shuffle_order); player.shuffle_order = NULL; }
    if (audio_count == 0) return;
    player.shuffle_order = malloc(audio_count * sizeof(int));
    if (!player.shuffle_order) return;
    srand((unsigned)time(NULL));
    for (int i = audio_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = audio_indices[i];
        audio_indices[i] = audio_indices[j];
        audio_indices[j] = tmp;
    }
    for (int i = 0; i < audio_count; i++) player.shuffle_order[i] = audio_indices[i];
}

/* =========================== SORT / SHUFFLE COMMANDS =========================== */

/* Cycle to the next sorting method and re-sort the list */
void cycle_sort(void) {
    char current_selection_name[PATH_MAX] = "";
    if (item_count > 0 && selected >= 0 && selected < item_count)
        strcpy(current_selection_name, items[selected].name);
    current_sort = (SortMode)((current_sort + 1) % 4);
    pthread_mutex_lock(&item_mutex);
    qsort(items, item_count, sizeof(Item), compare_items);
    pthread_mutex_unlock(&item_mutex);
    snprintf(status_msg, sizeof(status_msg), "Ordered by: %s", sort_names[current_sort]);
    status_msg_time = time(NULL);
    need_ui_update = 1;
    /* Try to keep the previously selected item in view */
    if (current_selection_name[0] != '\0') {
        int idx = find_item_index(current_selection_name);
        if (idx >= 0) {
            selected = idx;
            int max_display = LINES - 9;
            if (selected < top) top = selected;
            else if (selected >= top + max_display) top = selected - max_display + 1;
        }
    }
    if (player.playing_file[0] != '\0') {
        int idx = find_item_index(player.playing_file);
        if (idx >= 0) player.current_index = idx;
    }
    if (player.shuffle_mode) shuffle_list();
    player.pending_resort = 0;
    save_state();
}

/* Toggle shuffle mode on/off */
void toggle_shuffle(void) {
    player.shuffle_mode = !player.shuffle_mode;
    need_ui_update = 1;
    if (player.shuffle_mode) {
        shuffle_list();
        if (player.playlist_loaded && mpv_pid > 0) {
            mpv_send_json("{\"command\":[\"set_property\",\"shuffle\",true]}");
        }
        snprintf(status_msg, sizeof(status_msg), "Shuffle ON");
    } else {
        if (player.shuffle_order) { free(player.shuffle_order); player.shuffle_order = NULL; }
        player.audio_count = 0;
        for (int i = 0; i < item_count; i++)
            if (items[i].is_audio && !is_item_cue(i)) player.audio_count++;
        if (mpv_pid > 0) create_mpv_playlist();
        snprintf(status_msg, sizeof(status_msg), "Shuffle OFF");
    }
    status_msg_time = time(NULL);
    save_state();
}

/* =========================== MPV IPC FUNCTIONS =========================== */

/* Connect to mpv's Unix socket. Returns -1 on failure. */
void mpv_open_socket(void) {
    if (mpv_socket_fd != -1) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(mpv_socket_fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) return;
        close(mpv_socket_fd);
        mpv_socket_fd = -1;
    }
    mpv_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (mpv_socket_fd < 0) return;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MPV_SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (connect(mpv_socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(mpv_socket_fd);
        mpv_socket_fd = -1;
        return;
    }
    int flags = fcntl(mpv_socket_fd, F_GETFL, 0);
    fcntl(mpv_socket_fd, F_SETFL, flags | O_NONBLOCK);
    socket_buffer_len = 0;
    memset(socket_buffer, 0, SOCKET_BUFFER_SIZE);
}

/* Close the socket and reset buffer */
void mpv_close_socket(void) {
    if (mpv_socket_fd != -1) { close(mpv_socket_fd); mpv_socket_fd = -1; }
    socket_buffer_len = 0;
    player.mpv_initialized = 0;
}

/* Send a JSON command to mpv. Re‑opens the socket if needed. */
int mpv_send_json(const char *json) {
    if (mpv_socket_fd == -1) {
        mpv_open_socket();
        if (mpv_socket_fd == -1) {
            snprintf(status_msg, sizeof(status_msg), "ERROR: MPV socket disconnected");
            status_msg_time = time(NULL);
            return -1;
        }
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\n", json);
    ssize_t n = write(mpv_socket_fd, buf, strlen(buf));
    if (n < 0) {
        mpv_close_socket();
        snprintf(status_msg, sizeof(status_msg), "ERROR: MPV communication failed");
        status_msg_time = time(NULL);
        return -1;
    }
    return 0;
}

/* Read all pending data from the socket into the buffer and process any complete lines */
void drain_mpv_socket(void) {
    if (mpv_socket_fd == -1) return;
    char buffer[4096];
    ssize_t n;
    int drained = 0;
    while ((n = read(mpv_socket_fd, buffer, sizeof(buffer)-1)) > 0) {
        drained = 1;
        buffer[n] = '\0';
        size_t remaining = SOCKET_BUFFER_SIZE - socket_buffer_len - 1;
        if (remaining > 0) {
            size_t to_copy = (n < remaining) ? n : remaining;
            memcpy(socket_buffer + socket_buffer_len, buffer, to_copy);
            socket_buffer_len += to_copy;
            socket_buffer[socket_buffer_len] = '\0';
        }
    }
    if (drained) process_mpv_responses();
}

/* =========================== MPV RESPONSE PARSING =========================== */

/* Helper parsers for mpv property event lines */
static int parse_mpv_data_double(const char *line, double *out) {
    const char *p = strstr(line, "\"data\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p && (isspace((unsigned char)*p) || *p == '\"')) p++;
    char *endptr = NULL;
    double val = strtod(p, &endptr);
    if (endptr == p) return -1;
    *out = val;
    return 0;
}

static int parse_mpv_data_bool(const char *line, int *out) {
    const char *p = strstr(line, "\"data\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p && (isspace((unsigned char)*p) || *p == '\"')) p++;
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 0; }
    else if (strncmp(p, "false", 5) == 0) { *out = 0; return 0; }
    return -1;
}

static int parse_mpv_data_string(const char *line, char *out_path, size_t size) {
    const char *p = strstr(line, "\"data\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p && (isspace((unsigned char)*p) || *p == '\"')) p++;
    size_t i = 0;
    while (*p && *p != '\"' && i < size - 1) out_path[i++] = *p++;
    out_path[i] = '\0';
    return (i > 0) ? 0 : -1;
}

static int parse_mpv_data_int(const char *line, int *out) {
    const char *p = strstr(line, "\"data\"");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p && (isspace((unsigned char)*p) || *p == '\"')) p++;
    char *endptr = NULL;
    long val = strtol(p, &endptr, 10);
    if (endptr == p) return -1;
    *out = (int)val;
    return 0;
}

/* Handle the "end-file" event: if reason is "eof", advance to next file */
void handle_end_file_event(const char *line)
{
    if (!strstr(line, "\"event\":\"end-file\""))
        return;

    /* Ignore stop/quit events */
    if (!strstr(line, "\"reason\":\"eof\""))
        return;

    if (eof_processing)
        return;

    eof_processing = 1;

    // Use next_file() which properly handles both shuffle and normal mode
    next_file();

    eof_processing = 0;
    need_ui_update = 1;
}

/* Handle property-change events and update the PlayerState accordingly */
void handle_property_change(const char *line) {
    int id = -1;
    char *id_str = strstr(line, "\"id\"");
    if (id_str) {
        char *colon = strchr(id_str, ':');
        if (colon) id = atoi(colon + 1);
    }

    double val;
    int ival;
    char path_str[PATH_MAX];

    if (id == 1) { /* pause */
        int bval;
        if (parse_mpv_data_bool(line, &bval) == 0) {
            player.is_paused = bval;
            need_ui_update = 1;
        }
    } else if (id == 2) { /* path (current file) */
        if (parse_mpv_data_string(line, path_str, PATH_MAX) == 0) {
            if (strlen(path_str) > 0) {
                int found = 0;
                char *filename = strrchr(path_str, '/');
                if (filename) filename++; else filename = path_str;
                for (int i = 0; i < item_count; i++) {
                    if (strcmp(items[i].name, filename) == 0) {
                        if (player.current_index != i) {
                            player.current_index = i;
                            strcpy(player.playing_file, filename);
                            char time_str[32];
                            if (items[i].duration > 0)
                                snprintf(time_str, sizeof(time_str), "(%d:%02d)", items[i].duration/60, items[i].duration%60);
                            else strcpy(time_str, "");
                            if (items[i].has_chapters && player.total_chapters > 0)
                                snprintf(player.playing_info, sizeof(player.playing_info),
                                         "Now Playing: %s [%s] %s [Chapters: %d]",
                                         items[i].display_name, items[i].bitrate, time_str, player.total_chapters);
                            else
                                snprintf(player.playing_info, sizeof(player.playing_info),
                                         "Now Playing: %s [%s] %s",
                                         items[i].display_name, items[i].bitrate, time_str);
                            need_ui_update = 1;
                        }
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    player.current_index = -1;
                    player.playing_file[0] = '\0';
                    player.playing_info[0] = '\0';
                    need_ui_update = 1;
                }
            }
        }
    } else if (id == 3) { /* time-pos */
        if (parse_mpv_data_double(line, &val) == 0) {
            player.progress_seconds = val;
            need_ui_update = 1;
        }
    } else if (id == 4) { /* duration */
        if (parse_mpv_data_double(line, &val) == 0) {
            player.total_seconds = val;
            if (player.total_seconds <= 0) player.total_seconds = 180;
            need_ui_update = 1;
        }
    } else if (id == 5) { /* chapter */
        if (parse_mpv_data_int(line, &ival) == 0) {
            player.current_chapter = ival;
            need_ui_update = 1;
        }
    } else if (id == 6) { /* chapters (count) */
        if (parse_mpv_data_int(line, &ival) == 0) {
            player.total_chapters = ival;
            need_ui_update = 1;
        }
    } else if (id == 7) { /* chapter-list (full list) – used to handle restore with chapters */
        const char *data = strstr(line, "\"data\"");
        if (data) {
            int count = 0;
            const char *p = data;
            while ((p = strstr(p, "{")) != NULL) { count++; p++; }
            if (count > 0) {
                player.total_chapters = count;
                need_ui_update = 1;
                if (player.loading_pending && player.loading_chapter >= 0 && player.loading_chapter < count) {
                    char ch_cmd[256];
                    snprintf(ch_cmd, sizeof(ch_cmd), "{\"command\":[\"set_property\",\"chapter\",%d]}", player.loading_chapter);
                    mpv_send_json(ch_cmd);
                    player.current_chapter = player.loading_chapter;
                    usleep(50000);
                    char seek_cmd[256];
                    snprintf(seek_cmd, sizeof(seek_cmd), "{\"command\":[\"seek\",%d,\"absolute\"]}", player.loading_seek);
                    mpv_send_json(seek_cmd);
                    player.loading_pending = 0;
                    need_ui_update = 1;
                }
            }
        }
    }
}

/* Send the "observe_property" commands to mpv after the socket is connected */
void mpv_initialize_ipc(void) {
    if (player.mpv_initialized) return;
    if (mpv_socket_fd == -1) return;
    mpv_send_json("{\"command\":[\"observe_property\",1,\"pause\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",2,\"path\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",3,\"time-pos\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",4,\"duration\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",5,\"chapter\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",6,\"chapters\"]}");
    mpv_send_json("{\"command\":[\"observe_property\",7,\"chapter-list\"]}");
    player.mpv_initialized = 1;
}

/* Process any complete JSON lines in the socket buffer */
void process_mpv_responses(void) {
    if (mpv_socket_fd == -1) return;
    char *line_start = socket_buffer;
    char *line_end;
    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';
        if (strstr(line_start, "\"event\"")) {
            if (strstr(line_start, "\"end-file\"")) handle_end_file_event(line_start);
            if (strstr(line_start, "\"property-change\"")) handle_property_change(line_start);
        }
        line_start = line_end + 1;
    }
    if (line_start > socket_buffer) {
        size_t remaining_len = socket_buffer_len - (line_start - socket_buffer);
        if (remaining_len > 0) memmove(socket_buffer, line_start, remaining_len);
        socket_buffer_len = remaining_len;
        socket_buffer[socket_buffer_len] = '\0';
    }
}

/* =========================== MPV PLAYBACK CONTROL =========================== */

void mpv_seek_relative(int seconds) {
    char json[256];
    snprintf(json, sizeof(json), "{\"command\":[\"seek\",%d,\"relative\"]}", seconds);
    mpv_send_json(json);
}

void mpv_toggle_pause(void) {
    mpv_send_json("{\"command\":[\"cycle\",\"pause\"]}");
    need_ui_update = 1;
}

void mpv_quit(void) {
    mpv_send_json("{\"command\":[\"quit\"]}");
}

/* Poll mpv for updates and process any pending responses */
void mpv_poll_time(void) {
    if (mpv_socket_fd == -1) { mpv_open_socket(); if (mpv_socket_fd == -1) return; }
    drain_mpv_socket();
    process_mpv_responses();
    if (player.total_seconds > 0) {
        int new_percent = (int)((player.progress_seconds * 100) / player.total_seconds);
        if (new_percent > 100) new_percent = 100;
        if (new_percent != player.progress_percent) {
            player.progress_percent = new_percent;
            need_ui_update = 1;
        }
    }
}

void mpv_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    player.volume = vol;
    char json[256];
    snprintf(json, sizeof(json), "{\"command\":[\"set_property\",\"volume\",%d]}", vol);
    mpv_send_json(json);
}

void mpv_adjust_volume(int delta) {
    int new_vol = player.volume + delta;
    mpv_set_volume(new_vol);
    need_ui_update = 1;
}

/* Start mpv as a daemon with IPC socket */
void start_persistent_mpv(void) {
    if (mpv_pid > 0) {
        if (kill(mpv_pid, 0) == 0) return;
        mpv_pid = -1;
    }
    unlink(MPV_SOCKET_PATH);
    mpv_pid = fork();
    if (mpv_pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd != -1) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execlp("mpv", "mpv", "--no-video", "--idle=yes", "--quiet",
               "--input-ipc-server=" MPV_SOCKET_PATH, NULL);
        exit(1);
    } else if (mpv_pid > 0) {
        int attempts = 0;
        while (attempts < 20) {
            usleep(100000);
            mpv_open_socket();
            if (mpv_socket_fd != -1) break;
            attempts++;
        }
        mpv_set_volume(player.volume);
        mpv_initialize_ipc();
    }
}

/* Call this regularly to keep the player state in sync */
void sync_player_state(void) {
    if (mpv_socket_fd == -1) { mpv_open_socket(); if (mpv_socket_fd == -1) return; mpv_initialize_ipc(); }
    process_mpv_responses();
}

/* =========================== CHAPTER NAVIGATION =========================== */

void next_chapter(void) {
    if (mpv_pid > 0 && player.total_chapters > 0) {
        int next = player.current_chapter + 1;
        if (next < player.total_chapters) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"chapter\",%d]}", next);
            mpv_send_json(cmd);
            snprintf(status_msg, sizeof(status_msg), "Chapter %d/%d", next + 1, player.total_chapters);
            status_msg_time = time(NULL);
            need_ui_update = 1;
            player.chapter_mode = 1;
        }
    }
}

void previous_chapter(void) {
    if (mpv_pid > 0 && player.total_chapters > 0) {
        int prev = player.current_chapter - 1;
        if (prev >= 0) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"chapter\",%d]}", prev);
            mpv_send_json(cmd);
            snprintf(status_msg, sizeof(status_msg), "Chapter %d/%d", prev + 1, player.total_chapters);
            status_msg_time = time(NULL);
            need_ui_update = 1;
            player.chapter_mode = 1;
        }
    }
}

/* =========================== PLAYBACK COMMANDS =========================== */

void play_file_by_name(const char *filename) {
    if (!filename || filename[0] == '\0') return;
    play_file(filename);
    save_state();
}

/* Load and play a file (from the current directory) */
void play_file(const char *file) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", current_path, file);
    if (mpv_pid <= 0) {
        start_persistent_mpv();
        if (mpv_pid <= 0) {
            snprintf(status_msg, sizeof(status_msg), "ERROR: Failed to start MPV");
            status_msg_time = time(NULL);
            return;
        }
    }
    
    eof_processing = 0;
    player.chapter_mode = 0;
    player.pending_chapter = -1;
    player.loading_pending = 0;
    
    char escaped_path[PATH_MAX * 2];
    json_escape(path, escaped_path, sizeof(escaped_path));
    char load_cmd[PATH_MAX * 2 + 256];
    snprintf(load_cmd, sizeof(load_cmd), "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", escaped_path);
    if (mpv_send_json(load_cmd) < 0) {
        snprintf(status_msg, sizeof(status_msg), "ERROR: Failed to load file");
        status_msg_time = time(NULL);
        return;
    }
    
    mpv_set_volume(player.volume);
    strcpy(player.playing_file, file);
    pthread_mutex_lock(&item_mutex);
    for (int i = 0; i < item_count; i++) {
        if (strcmp(items[i].name, file) == 0) {
            player.current_index = i;
            char time_str[32];
            if (items[i].duration > 0)
                snprintf(time_str, sizeof(time_str), "(%d:%02d)", items[i].duration/60, items[i].duration%60);
            else strcpy(time_str, "");
            snprintf(player.playing_info, sizeof(player.playing_info), "Now Playing: %s [%s] %s",
                     items[i].display_name, items[i].bitrate, time_str);
            player.total_seconds = items[i].duration;
            if (player.total_seconds <= 0) player.total_seconds = 180;
            player.progress_seconds = 0;
            player.progress_percent = 0;
            player.current_chapter = 0;
            player.total_chapters = 0;
            break;
        }
    }
    pthread_mutex_unlock(&item_mutex);
    snprintf(status_msg, sizeof(status_msg), "Playing: %s", file);
    status_msg_time = time(NULL);
    player.last_update = time(NULL);
    need_ui_update = 1;
}

/* Load a file, seek to a position, and optionally set a chapter, then pause */
void play_file_paused_at(const char *file, int seconds, int chapter) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", current_path, file);
    if (mpv_pid <= 0) {
        start_persistent_mpv();
        if (mpv_pid <= 0) {
            snprintf(status_msg, sizeof(status_msg), "ERROR: Failed to start MPV");
            status_msg_time = time(NULL);
            return;
        }
    }
    
    create_mpv_playlist();
    
    eof_processing = 0;
    player.chapter_mode = 0;
    strcpy(player.playing_file, file);
    player.pending_chapter = -1;

    int has_chapters = 0;
    pthread_mutex_lock(&item_mutex);
    for (int i = 0; i < item_count; i++) {
        if (strcmp(items[i].name, file) == 0) {
            player.current_index = i;
            has_chapters = items[i].has_chapters;
            char time_str[32];
            if (items[i].duration > 0)
                snprintf(time_str, sizeof(time_str), "(%d:%02d)", items[i].duration/60, items[i].duration%60);
            else strcpy(time_str, "");
            snprintf(player.playing_info, sizeof(player.playing_info), "Now Playing: %s [%s] %s",
                     items[i].display_name, items[i].bitrate, time_str);
            player.total_seconds = items[i].duration;
            if (player.total_seconds <= 0) player.total_seconds = 180;
            player.progress_seconds = seconds;
            if (player.total_seconds > 0)
                player.progress_percent = (int)((player.progress_seconds * 100) / player.total_seconds);
            player.total_chapters = 0;
            break;
        }
    }
    pthread_mutex_unlock(&item_mutex);

    char escaped_path[PATH_MAX * 2];
    json_escape(path, escaped_path, sizeof(escaped_path));
    char load_cmd[PATH_MAX * 2 + 256];
    snprintf(load_cmd, sizeof(load_cmd), "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", escaped_path);
    if (mpv_send_json(load_cmd) < 0) {
        snprintf(status_msg, sizeof(status_msg), "ERROR: Failed to load file");
        status_msg_time = time(NULL);
        return;
    }

    /* If the file has chapters, we need to wait for the chapter-list to arrive
     * before seeking, so we set loading_pending and handle it in handle_property_change. */
    if (has_chapters) {
        strcpy(player.loading_file, file);
        player.loading_seek = seconds;
        player.loading_chapter = chapter;
        player.loading_pending = 1;
    } else {
        usleep(250000);  /* Give mpv a moment to load the file */
        char seek_cmd[256];
        snprintf(seek_cmd, sizeof(seek_cmd), "{\"command\":[\"seek\",%d,\"absolute\"]}", seconds);
        mpv_send_json(seek_cmd);
        player.loading_pending = 0;
    }

    mpv_send_json("{\"command\":[\"set_property\",\"pause\",true]}");
    mpv_set_volume(player.volume);

    snprintf(status_msg, sizeof(status_msg), "Restoring: %s", file);
    status_msg_time = time(NULL);
    player.last_update = time(NULL);
    need_ui_update = 1;
}

/* Stop playback and clear the player state */
void stop_player(void) {
    if (mpv_pid > 0) mpv_send_json("{\"command\":[\"stop\"]}");
    player.progress_seconds = 0;
    player.total_seconds = 0;
    player.progress_percent = 0;
    player.playing_info[0] = '\0';
    player.current_chapter = 0;
    player.total_chapters = 0;
    player.is_paused = 0;
    player.current_index = -1;
    player.playing_file[0] = '\0';
    player.chapter_mode = 0;
    player.pending_chapter = -1;
    player.loading_pending = 0;
    player.playlist_loaded = 0;
    eof_processing = 0;
    need_ui_update = 1;
}

/* Force‑kill the mpv process and clean up */
void kill_player(void) {
    if (mpv_pid > 0) {
        mpv_send_json("{\"command\":[\"quit\"]}");
        usleep(100000);
        kill(mpv_pid, SIGTERM);
        int status;
        waitpid(mpv_pid, &status, 0);
        mpv_pid = -1;
    }
    mpv_close_socket();
    unlink(MPV_SOCKET_PATH);
}

/* =========================== NAVIGATION: NEXT / PREVIOUS =========================== */

/* Advance to the next audio file (respecting shuffle order if enabled) */
void next_file(void) {
    user_interacting = 1;
    last_user_action = time(NULL);
    
    player.chapter_mode = 0;
    player.loading_pending = 0;
    
    if (item_count == 0 || player.audio_count == 0) { 
        stop_player(); 
        return; 
    }
    
    int next_index = -1;
    
    if (player.shuffle_mode && player.shuffle_order) {
        int current_pos = -1;
        for (int i = 0; i < player.audio_count; i++) {
            if (player.shuffle_order[i] == player.current_index) { 
                current_pos = i; 
                break; 
            }
        }
        for (int i = current_pos + 1; i < player.audio_count; i++) {
            if (!is_item_cue(player.shuffle_order[i])) { 
                next_index = player.shuffle_order[i]; 
                break; 
            }
        }
        if (next_index == -1) {
            for (int i = 0; i <= current_pos; i++) {
                if (!is_item_cue(player.shuffle_order[i])) { 
                    next_index = player.shuffle_order[i]; 
                    break; 
                }
            }
        }
    } else {
        int start = (player.current_index < 0) ? 0 : player.current_index + 1;
        for (int i = start; i < item_count; i++) {
            if (items[i].is_audio && !is_item_cue(i)) { 
                next_index = i; 
                break; 
            }
        }
        if (next_index == -1) {
            for (int i = 0; i < item_count; i++) {
                if (items[i].is_audio && !is_item_cue(i)) { 
                    next_index = i; 
                    break; 
                }
            }
        }
    }
    
    if (next_index >= 0) {
        selected = next_index;
        int max_display = LINES - 9;
        if (selected < top) top = selected;
        else if (selected >= top + max_display) top = selected - max_display + 1;
        
        player.current_chapter = 0;
        player.total_chapters = 0;
        player.chapter_mode = 0;
        
        play_file_by_name(items[next_index].name);
        need_ui_update = 1;
    } else {
        stop_player();
        snprintf(status_msg, sizeof(status_msg), "End of playlist");
        status_msg_time = time(NULL);
        need_ui_update = 1;
    }
}

/* Go to the previous audio file (respecting shuffle order) */
void previous_file(void) {
    user_interacting = 1;
    last_user_action = time(NULL);
    if (player.playlist_loaded && mpv_pid > 0) {
        mpv_playlist_prev();
    } else {
        if (item_count == 0 || player.audio_count == 0) return;
        int prev_index = -1;
        if (player.shuffle_mode && player.shuffle_order) {
            int current_pos = -1;
            for (int i = 0; i < player.audio_count; i++) {
                if (player.shuffle_order[i] == player.current_index) { current_pos = i; break; }
            }
            for (int i = current_pos - 1; i >= 0; i--) {
                if (!is_item_cue(player.shuffle_order[i])) { prev_index = player.shuffle_order[i]; break; }
            }
            if (prev_index == -1) {
                for (int i = player.audio_count - 1; i >= current_pos; i--) {
                    if (!is_item_cue(player.shuffle_order[i])) { prev_index = player.shuffle_order[i]; break; }
                }
            }
        } else {
            int start = (player.current_index < 0) ? item_count - 1 : player.current_index - 1;
            for (int i = start; i >= 0; i--) {
                if (items[i].is_audio && !is_item_cue(i)) { prev_index = i; break; }
            }
            if (prev_index == -1) {
                for (int i = item_count - 1; i >= 0; i--) {
                    if (items[i].is_audio && !is_item_cue(i)) { prev_index = i; break; }
                }
            }
        }
        if (prev_index >= 0) {
            selected = prev_index;
            int max_display = LINES - 9;
            if (selected < top) top = selected;
            else if (selected >= top + max_display) top = selected - max_display + 1;
            play_file_by_name(items[prev_index].name);
            need_ui_update = 1;
        }
    }
}

/* =========================== UI: OPEN / NAVIGATE =========================== */

/* Open the selected item: enter a directory or play an audio file */
void open_selected(void) {
    if (item_count == 0) return;
    if (items[selected].is_parent) { go_parent(); return; }
    if (items[selected].is_dir) {
        strncpy(current_path, items[selected].full_path, sizeof(current_path)-1);
        current_path[sizeof(current_path)-1] = '\0';
        scan_directory();
        if (mpv_pid > 0) create_mpv_playlist();
        need_ui_update = 1;
        return;
    }
    if (items[selected].is_audio) {
        if (selected == player.current_index && mpv_pid > 0) mpv_toggle_pause();
        else play_file_by_name(items[selected].name);
        need_ui_update = 1;
    }
}

/* Go up one directory */
void go_parent(void) {
    char *slash = strrchr(current_path, '/');
    if (slash && slash != current_path) *slash = '\0';
    else strcpy(current_path, "/");
    scan_directory();
    if (mpv_pid > 0) create_mpv_playlist();
    need_ui_update = 1;
}

/* =========================== PROGRESS BAR =========================== */

/* Draw a progress bar with elapsed / total time, and chapters if available */
void draw_progress_bar(int row, int max_width) {
    if (max_width < 20) return;
    
    char info[64];
    if (player.total_chapters > 0 && player.current_chapter >= 0) {
        snprintf(info, sizeof(info), "%02d:%02d / %02d:%02d [Ch %d/%d]",
                 (int)player.progress_seconds/60, (int)player.progress_seconds%60,
                 (int)player.total_seconds/60, (int)player.total_seconds%60,
                 player.current_chapter+1, player.total_chapters);
    } else {
        snprintf(info, sizeof(info), "%02d:%02d / %02d:%02d",
                 (int)player.progress_seconds/60, (int)player.progress_seconds%60,
                 (int)player.total_seconds/60, (int)player.total_seconds%60);
    }
    
    int info_len = strlen(info);
    int bar_width = max_width - info_len - 1;
    
    if (bar_width < 10) {
        attron(COLOR_PAIR(COLOR_PROGRESS));
        mvprintw(row, 0, "%s", info);
        attroff(COLOR_PAIR(COLOR_PROGRESS));
        return;
    }
    
    int filled = (player.progress_percent * (bar_width - 2)) / 100;
    if (filled < 0) filled = 0;
    if (filled > bar_width - 2) filled = bar_width - 2;
    
    attron(COLOR_PAIR(COLOR_PROGRESS));
    mvprintw(row, 0, "%s ", info);
    int bar_start = info_len + 1;
    mvaddch(row, bar_start, '[');
    for (int i = 0; i < bar_width - 2; i++) {
        if (i < filled) {
            mvaddch(row, bar_start + 1 + i, '=');
        } else if (i == filled) {
            mvaddch(row, bar_start + 1 + i, '>');
        } else {
            mvaddch(row, bar_start + 1 + i, ' ');
        }
    }
    mvaddch(row, bar_start + bar_width - 1, ']');
    attroff(COLOR_PAIR(COLOR_PROGRESS));
}

/* =========================== MAIN DRAW FUNCTION =========================== */

/* Redraw the entire screen */
void draw(void) {
    clear();
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    /* Header */
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    char header_left[128];
    snprintf(header_left, sizeof(header_left), "Fenrir %s[%s theme] %s",
             player.shuffle_mode ? "[S] " : "", theme_names[current_theme],
             player.chapter_mode ? "[CHAPTER MODE]" : "");
    mvprintw(0, 0, "%s", header_left);
    time_t raw_time = time(NULL);
    struct tm *time_info = localtime(&raw_time);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%I:%M:%S %p", time_info);
    int time_x = (max_x - strlen(time_str)) / 2;
    if (time_x < 0) time_x = 0;
    mvprintw(0, time_x, "%s", time_str);
    char controls[] = "o:order  s:shuffle  t:theme  q:quit";
    int controls_x = max_x - strlen(controls);
    if (controls_x < 0) controls_x = 0;
    mvprintw(0, controls_x, "%s", controls);
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    
    /* Divider */
    attron(COLOR_PAIR(COLOR_HEADER));
    for (int i = 0; i < max_x; i++) mvaddch(1, i, '-');
    attroff(COLOR_PAIR(COLOR_HEADER));
    
    /* Path and stats */
    attron(COLOR_PAIR(COLOR_NORMAL));
    char path_display[max_x + 1];
    {
        int avail = max_x - 25;
        if (avail < 0) avail = 0;
        if ((int)strlen(current_path) > avail) {
            int keep = max_x - 28;
            if (keep < 0) keep = 0;
            if (keep > (int)strlen(current_path)) keep = (int)strlen(current_path);
            snprintf(path_display, sizeof(path_display), "...%s",
                     current_path + strlen(current_path) - keep);
        } else {
            strncpy(path_display, current_path, sizeof(path_display) - 1);
            path_display[sizeof(path_display) - 1] = '\0';
        }
    }
    mvprintw(2, 0, "Path: %s", path_display);
    int dir_count = 0, audio_count = 0, cue_count = 0;
    pthread_mutex_lock(&item_mutex);
    for (int i = 0; i < item_count; i++) {
        if (!items[i].is_parent) {
            if (items[i].is_dir) dir_count++;
            if (items[i].is_audio) {
                if (items[i].is_cue) cue_count++;
                else audio_count++;
            }
        }
    }
    pthread_mutex_unlock(&item_mutex);
    char stats[128];
    snprintf(stats, sizeof(stats), "Order: %-14s Dirs: %-3d Audio: %d CUE: %d%s",
             sort_names[current_sort], dir_count, audio_count, cue_count,
             player.playlist_loaded ? " [Playlist]" : "");
    int stats_x = max_x - strlen(stats);
    if (stats_x < 0) stats_x = 0;
    mvprintw(2, stats_x, "%s", stats);
    attroff(COLOR_PAIR(COLOR_NORMAL));
    
    /* Now playing */
    if (player.playing_info[0] != '\0') {
        attron(COLOR_PAIR(COLOR_TITLE) | A_BOLD);
        char info[max_x + 1];
        if (is_playing_cue() && player.total_chapters > 0) {
            snprintf(info, sizeof(info), "Now Playing: %s [Track %d of %d]",
                     player.playing_info, player.current_chapter+1, player.total_chapters);
        } else {
            int copy_len = max_x - 1;
            if (copy_len < 0) copy_len = 0;
            strncpy(info, player.playing_info, copy_len);
            info[copy_len] = '\0';
        }
        mvprintw(3, 0, "%s", info);
        attroff(COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    }
    
    /* Progress bar */
    if (mpv_pid > 0) {
        draw_progress_bar(4, max_x);
    }
    
    /* Divider */
    attron(COLOR_PAIR(COLOR_NORMAL));
    for (int i = 0; i < max_x; i++) mvaddch(5, i, '-');
    attroff(COLOR_PAIR(COLOR_NORMAL));
    
    /* File list */
    int max_display = max_y - 9;
    int start_row = 6;
    
    pthread_mutex_lock(&item_mutex);
    /* Queue metadata for visible items */
    for (int i = 0; i < max_display && (top + i) < item_count; i++) {
        int item_index = top + i;
        if (items[item_index].is_audio && !items[item_index].metadata_loaded && !items[item_index].metadata_loading) {
            pthread_mutex_unlock(&item_mutex);
            queue_metadata_load(item_index);
            pthread_mutex_lock(&item_mutex);
        }
    }
    
    /* Draw each visible item */
    for (int i = 0; i < max_display; i++) {
        int item_index = top + i;
        if (item_index >= item_count) break;
        int row = start_row + i;
        int is_selected = (selected == item_index);
        int is_playing = (item_index == player.current_index && mpv_pid > 0 && player.playing_file[0] != '\0');
        
        if (is_selected) attron(COLOR_PAIR(COLOR_SELECTED) | A_REVERSE);
        
        char line[max_x + 1];
        line[0] = '\0';
        
        if (items[item_index].is_parent) {
            attron(COLOR_PAIR(COLOR_PARENT) | A_BOLD);
            snprintf(line, sizeof(line), "  %s", items[item_index].display_name);
            mvprintw(row, 0, "%s", line);
            attroff(COLOR_PAIR(COLOR_PARENT) | A_BOLD);
        } else if (items[item_index].is_dir) {
            attron(COLOR_PAIR(COLOR_DIR) | A_BOLD);
            snprintf(line, sizeof(line), "[%s]", items[item_index].display_name);
            mvprintw(row, 0, "%s", line);
            attroff(COLOR_PAIR(COLOR_DIR) | A_BOLD);
        } else if (items[item_index].is_audio) {
            if (is_playing) attron(COLOR_PAIR(COLOR_PLAYING) | A_BOLD);
            else if (is_item_cue(item_index)) attron(COLOR_PAIR(COLOR_CUE) | A_BOLD);
            else if (items[item_index].has_chapters) attron(COLOR_PAIR(COLOR_CHAPTER) | A_BOLD);
            else attron(COLOR_PAIR(COLOR_NORMAL));
            
            char temp[max_x * 2];
            int shuffle_pos = -1;
            if (player.shuffle_mode && player.shuffle_order) {
                for (int j = 0; j < player.audio_count; j++) {
                    if (player.shuffle_order[j] == item_index) { shuffle_pos = j+1; break; }
                }
            }
            
            char state_suffix[64] = "";
            if (is_playing) {
                if (player.is_paused) strcpy(state_suffix, " <-- PAUSED");
                else strcpy(state_suffix, " <-- PLAYING");
            }
            
            char duration_tag[32] = "";
            if (items[item_index].duration > 0) {
                snprintf(duration_tag, sizeof(duration_tag), " (%d:%02d)", 
                        items[item_index].duration/60, items[item_index].duration%60);
            }
            
            char cue_tag[16] = "";
            if (items[item_index].is_cue) strcpy(cue_tag, " [CUE]");
            char chapter_tag[16] = "";
            if (items[item_index].has_chapters) strcpy(chapter_tag, " [CH]");
            char shuffle_tag[32] = "";
            if (shuffle_pos != -1) snprintf(shuffle_tag, sizeof(shuffle_tag), " [#%d]", shuffle_pos);
            
            if (is_playing) {
                snprintf(temp, sizeof(temp), "  %s [%s]%s%s%s%s%s",
                        items[item_index].display_name, items[item_index].bitrate,
                        cue_tag, chapter_tag, duration_tag, shuffle_tag, state_suffix);
            } else {
                snprintf(temp, sizeof(temp), "  %s [%s]%s%s%s%s",
                        items[item_index].display_name, items[item_index].bitrate,
                        cue_tag, chapter_tag, duration_tag, shuffle_tag);
            }
            
            {
                int copy_len = max_x - 1;
                if (copy_len < 0) copy_len = 0;
                strncpy(line, temp, copy_len);
                line[copy_len] = '\0';
            }
            mvprintw(row, 0, "%s", line);
            
            if (is_playing) attroff(COLOR_PAIR(COLOR_PLAYING) | A_BOLD);
            else if (is_item_cue(item_index)) attroff(COLOR_PAIR(COLOR_CUE) | A_BOLD);
            else if (items[item_index].has_chapters) attroff(COLOR_PAIR(COLOR_CHAPTER) | A_BOLD);
            else attroff(COLOR_PAIR(COLOR_NORMAL));
        }
        
        if (is_selected) attroff(COLOR_PAIR(COLOR_SELECTED) | A_REVERSE);
    }
    pthread_mutex_unlock(&item_mutex);
    
    /* Status line */
    attron(COLOR_PAIR(COLOR_STATUS));
    if (status_msg[0] != '\0') {
        char status_line[max_x + 1];
        int copy_len = max_x - 1;
        if (copy_len < 0) copy_len = 0;
        strncpy(status_line, status_msg, copy_len);
        status_line[copy_len] = '\0';
        mvprintw(max_y - 3, 0, "%s", status_line);
    }
    attroff(COLOR_PAIR(COLOR_STATUS));
    
    /* Bottom divider */
    attron(COLOR_PAIR(COLOR_NORMAL));
    for (int i = 0; i < max_x; i++) mvaddch(max_y - 2, i, '-');
    attroff(COLOR_PAIR(COLOR_NORMAL));
    
    /* Help line */
    attron(COLOR_PAIR(COLOR_NORMAL));
    char help[] = "Enter:Play  Space:Pause  +/-:Vol  o:Order  s:Shuffle  n:Next  b:Prev  c:NextCh  x:PrevCh  t:Theme  q:Quit";
    if (max_x >= 0 && strlen(help) > (size_t)max_x) {
        char truncated[max_x + 1];
        int copy_len = max_x - 1;
        if (copy_len < 0) copy_len = 0;
        strncpy(truncated, help, copy_len);
        truncated[copy_len] = '\0';
        mvprintw(max_y - 1, 0, "%s", truncated);
    } else {
        mvprintw(max_y - 1, 0, "%s", help);
    }
    attroff(COLOR_PAIR(COLOR_NORMAL));
    refresh();
}

/* =========================== STATUS CLEARING =========================== */

/* Automatically clear the status message after 5 seconds */
void clear_status_after_delay(void) {
    static time_t last_status_time = 0;
    static char old_status[256] = "";
    if (status_msg[0] != '\0') {
        if (strcmp(status_msg, old_status) != 0) {
            strcpy(old_status, status_msg);
            last_status_time = time(NULL);
        }
    }
    if (status_msg[0] != '\0' && time(NULL) - last_status_time > 5) {
        status_msg[0] = '\0';
        need_ui_update = 1;
    }
}

/* =========================== SIGNAL HANDLER =========================== */

/* SIGCHLD handler – marks that mpv has exited */
void sigchld_handler(int sig) {
    (void)sig;
    mpv_exited = 1;
}

/* =========================== THREAD CLEANUP =========================== */

/* Stop the metadata thread and wait for it */
void cleanup_metadata_thread(void) {
    if (metadata_thread_running) {
        metadata_thread_running = 0;
        pthread_cond_signal(&queue_cond);
        pthread_join(metadata_thread, NULL);
    }
}

/* =========================== MAIN =========================== */

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    printf("Loading...\n");
    fflush(stdout);
    setlocale(LC_ALL, "");

    char restored_file[PATH_MAX] = "";
    int restored_seconds = 0, restored_theme = 0, restored_sort = 0;
    int has_restored_state = 0;

    /* Determine starting directory: either from command line or state file */
    if (argc > 1) {
        if (!realpath(argv[1], current_path)) strcpy(current_path, ".");
    } else {
        char saved_path[PATH_MAX] = "";
        if (load_state(saved_path, restored_file, &restored_seconds, &restored_theme, &restored_sort)) {
            struct stat st;
            if (stat(saved_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                strcpy(current_path, saved_path);
                if (restored_theme < 0 || restored_theme >= 8) restored_theme = THEME_GREEN;
                if (restored_sort < 0 || restored_sort >= 4) restored_sort = SORT_ALPHA;
                current_theme = (ColorTheme)restored_theme;
                current_sort = (SortMode)restored_sort;
                has_restored_state = 1;
            }
        }
        if (!has_restored_state) {
            if (!realpath(".", current_path)) strcpy(current_path, ".");
        }
    }

    /* Start metadata thread */
    metadata_thread_running = 1;
    if (pthread_create(&metadata_thread, NULL, metadata_loader_thread, NULL) != 0) {
        metadata_thread_running = 0;
        fprintf(stderr, "Failed to create metadata thread\n");
    }

    /* Initialise ncurses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(100);
    setup_colors(current_theme);

    clear();
    attron(COLOR_PAIR(COLOR_NORMAL) | A_BOLD);
    mvprintw(LINES/2, (COLS-10)/2, "Loading...");
    attroff(COLOR_PAIR(COLOR_NORMAL) | A_BOLD);
    refresh();

    /* Set up SIGCHLD handler to detect mpv exit */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* Start mpv and scan the initial directory */
    start_persistent_mpv();
    scan_directory();
    if (mpv_pid > 0) create_mpv_playlist();

    /* If we have a saved file, restore playback */
    if (has_restored_state && restored_file[0] != '\0') {
        int idx = find_item_index(restored_file);
        if (idx >= 0) {
            selected = idx;
            int max_display = LINES - 9;
            if (selected >= top + max_display) top = selected - max_display + 1;
            get_audio_info_sync(idx);
            int restored_chapter = load_chapter_from_state();
            play_file_paused_at(restored_file, restored_seconds, restored_chapter);
        }
    }

    /* Main event loop */
    time_t last_poll = 0;
    while (1) {
        time_t now = time(NULL);
        /* Poll mpv every second */
        if (now - last_poll >= 1) {
            mpv_poll_time();
            last_poll = now;
            /* Save state every 5 seconds of progress */
            static int last_saved_seconds = -1;
            int current_sec = (int)player.progress_seconds;
            if (current_sec != last_saved_seconds && current_sec % 5 == 0) {
                save_state();
                last_saved_seconds = current_sec;
            }
        }

        sync_player_state();
        clear_status_after_delay();

        /* Re‑sort if metadata loading triggered a pending resort */
        if (player.pending_resort) {
            player.pending_resort = 0;
            pthread_mutex_lock(&item_mutex);
            qsort(items, item_count, sizeof(Item), compare_items);
            pthread_mutex_unlock(&item_mutex);
            if (mpv_pid > 0) create_mpv_playlist();
            if (player.playing_file[0] != '\0') {
                int idx = find_item_index(player.playing_file);
                if (idx >= 0) player.current_index = idx;
            }
            need_ui_update = 1;
        }

        if (need_ui_update) {
            draw();
            need_ui_update = 0;
        }

        /* If mpv died, restart it */
        if (mpv_exited && mpv_pid > 0) {
            int status;
            pid_t w = waitpid(mpv_pid, &status, WNOHANG);
            if (w == mpv_pid) {
                mpv_pid = -1;
                mpv_close_socket();
                start_persistent_mpv();
                if (mpv_pid > 0) create_mpv_playlist();
            }
            mpv_exited = 0;
        }

        /* Handle keyboard input */
        int ch = getch();
        if (ch != ERR) {
            user_interacting = 1;
            last_user_action = time(NULL);
            switch (ch) {
                case KEY_UP:
                    if (selected > 0) { selected--; if (selected < top) top = selected; need_ui_update = 1; }
                    break;
                case KEY_DOWN:
                    if (selected < item_count - 1) {
                        selected++;
                        int max_display = LINES - 9;
                        if (selected >= top + max_display) top = selected - max_display + 1;
                        need_ui_update = 1;
                    }
                    break;
                case KEY_LEFT: mpv_seek_relative(-10); need_ui_update = 1; break;
                case KEY_RIGHT: mpv_seek_relative(+10); need_ui_update = 1; break;
                case '\n':
                case KEY_ENTER: open_selected(); need_ui_update = 1; break;
                case KEY_BACKSPACE:
                case 127: go_parent(); need_ui_update = 1; break;
                case ' ': if (mpv_pid > 0) { mpv_toggle_pause(); need_ui_update = 1; } break;
                case 'o':
                case 'O': cycle_sort(); need_ui_update = 1; break;
                case 'n':
                case 'N': next_file(); need_ui_update = 1; break;
                case 'b':
                case 'B': previous_file(); need_ui_update = 1; break;
                case 'c':
                case 'C': next_chapter(); need_ui_update = 1; break;
                case 'x':
                case 'X': previous_chapter(); need_ui_update = 1; break;
                case 's':
                case 'S': toggle_shuffle(); need_ui_update = 1; break;
                case 't':
                case 'T':
                    current_theme = (ColorTheme)((current_theme + 1) % 8);
                    setup_colors(current_theme);
                    clear();
                    need_ui_update = 1;
                    break;
                case '+':
                case '=':
                    mpv_adjust_volume(5);
                    snprintf(status_msg, sizeof(status_msg), "Volume: %d%%", player.volume);
                    status_msg_time = time(NULL);
                    need_ui_update = 1;
                    break;
                case '-':
                    mpv_adjust_volume(-5);
                    snprintf(status_msg, sizeof(status_msg), "Volume: %d%%", player.volume);
                    status_msg_time = time(NULL);
                    need_ui_update = 1;
                    break;
                case 'q':
                case 'Q':
                    save_state();
                    kill_player();
                    cleanup_metadata_thread();
                    if (player.shuffle_order) free(player.shuffle_order);
                    endwin();
                    return 0;
            }
        } else {
            user_interacting = 0;
        }
    }
    return 0;
}