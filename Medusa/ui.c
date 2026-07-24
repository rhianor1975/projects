// ui.c - see ui.h
//
// Server discovery (UDP broadcast) is real. Auth, libraries, and items now
// go through api.c against a real Jellyfin server instead of mock data.

#include "ui.h"
#include "api.h"
#include "mpv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

typedef struct {
    const char* name;
    const char* header;       // top bar / titles
    const char* text;         // normal row text
    const char* dim;          // secondary/muted text
    const char* accent;       // highlights, counts, badges
    const char* cursor_row;   // row under the cursor
    const char* selected_row; // a row that's been drilled into / active
    const char* error;
} ColorTheme;

static const ColorTheme themes[] = {
    {"Slate",   "\033[1;36m", "\033[37m", "\033[90m", "\033[33m", "\033[46;30m", "\033[42;30m", "\033[31m"},
    {"Amber",   "\033[1;33m", "\033[37m", "\033[90m", "\033[36m", "\033[43;30m", "\033[42;30m", "\033[31m"},
};
#define TOTAL_THEMES (int)(sizeof(themes) / sizeof(themes[0]))
static int current_theme_idx = 0;
#define TH (themes[current_theme_idx])
#define RESET "\033[0m"

// ---------------------------------------------------------------------------
// Terminal raw mode (mirrors chess.c's setup/reset/signal handling)
// ---------------------------------------------------------------------------

static struct termios orig_term;
static bool term_modified = false;

void ui_reset_terminal(void) {
    if (term_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
        printf("\033[?25h" RESET "\n");
        term_modified = false;
    }
}

static void handle_signal(int sig) {
    (void)sig;
    ui_reset_terminal();
    exit(0);
}

void ui_setup_terminal(void) {
    struct termios raw_term;
    tcgetattr(STDIN_FILENO, &orig_term);
    raw_term = orig_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    term_modified = true;
    printf("\033[?25l"); // hide cursor while navigating lists
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
}

// Temporarily drop back to cooked mode for line-edited text input (server
// URL, username, password). Restores raw mode on return.
static void read_line(const char* prompt, char* buf, size_t bufsz, bool mask) {
    struct termios cooked = orig_term;
    if (mask) cooked.c_lflag &= ~ECHO; // canonical, but silent (password)
    printf("\033[?25h%s%s" RESET, TH.accent, prompt);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &cooked);

    if (fgets(buf, (int)bufsz, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    if (mask) printf("\n");

    struct termios raw_term = orig_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    printf("\033[?25l");
}

// ---------------------------------------------------------------------------
// Local network server discovery (real, not mocked)
//
// Jellyfin servers listen on UDP port 7359 for the literal message
// "Who is JellyfinServer?" and reply with a small JSON blob like:
//   {"Address":"http://192.168.1.50:8096","Id":"...","Name":"myserver"}
// ---------------------------------------------------------------------------

#define JELLYFIN_DISCOVERY_PORT 7359
#define JELLYFIN_DISCOVERY_MSG  "Who is JellyfinServer?"
#define MAX_DISCOVERED 16

typedef struct {
    char name[128];
    char address[128];
} DiscoveredServer;

static DiscoveredServer discovered[MAX_DISCOVERED];
static int discovered_count = 0;

// Minimal ad hoc extractor for "key":"value" out of a flat JSON object -
// not a general parser, just enough for the discovery reply shape above.
static bool json_extract_string(const char* buf, const char* key, char* out, size_t out_sz) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char* p = strstr(buf, pattern);
    if (!p) return false;
    p += strlen(pattern);
    const char* end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static void add_discovered(const char* name, const char* address) {
    for (int i = 0; i < discovered_count; i++) {
        if (strcmp(discovered[i].address, address) == 0) return; // dedup
    }
    if (discovered_count >= MAX_DISCOVERED) return;
    snprintf(discovered[discovered_count].name, sizeof(discovered[discovered_count].name), "%s", name);
    snprintf(discovered[discovered_count].address, sizeof(discovered[discovered_count].address), "%s", address);
    discovered_count++;
}

static void discover_servers(int timeout_ms) {
    discovered_count = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    int broadcast_enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(JELLYFIN_DISCOVERY_PORT);
    bcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    sendto(sock, JELLYFIN_DISCOVERY_MSG, strlen(JELLYFIN_DISCOVERY_MSG), 0,
           (struct sockaddr*)&bcast_addr, sizeof(bcast_addr));

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining_ms = timeout_ms - elapsed_ms;
        if (remaining_ms <= 0 || discovered_count >= MAX_DISCOVERED) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        struct timeval sel_tv;
        sel_tv.tv_sec = remaining_ms / 1000;
        sel_tv.tv_usec = (remaining_ms % 1000) * 1000;

        int ret = select(sock + 1, &fds, NULL, NULL, &sel_tv);
        if (ret <= 0) break;

        char buf[2048];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from_addr, &from_len);
        if (n <= 0) continue;
        buf[n] = '\0';

        char name[128] = "", address[128] = "";
        json_extract_string(buf, "Name", name, sizeof(name));
        json_extract_string(buf, "Address", address, sizeof(address));
        if (address[0]) add_discovered(name[0] ? name : "Jellyfin Server", address);
    }

    close(sock);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

static void clear_screen(void) {
    printf("\033[H\033[J");
}

static void print_footer(const char* controls) {
    printf("\n%s%s" RESET "\n", TH.dim, controls);
}

static void print_progress_bar(int pct, int width) {
    int filled = (pct * width) / 100;
    printf("%s[", TH.dim);
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("]" RESET " %s%3d%%" RESET, TH.accent, pct);
}

// Truncates src to fit `width` columns (ellipsis if it doesn't), then pads
// with spaces to exactly `width`, so fixed-width columns (e.g. a title next
// to a year) stay aligned instead of a long title pushing later columns out.
static void fit_column(char* dst, size_t dst_sz, const char* src, int width) {
    int len = (int)strlen(src);
    if (len <= width) {
        snprintf(dst, dst_sz, "%-*s", width, src);
        return;
    }
    int keep = width - 3;
    if (keep < 0) keep = 0;
    snprintf(dst, dst_sz, "%.*s...", keep, src);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

typedef enum { SCREEN_LOGIN, SCREEN_LIBRARIES, SCREEN_ITEMS, SCREEN_QUIT } Screen;

// One level of the browse hierarchy: a library, a Series/Season folder
// drilled into from one, Continue Watching, or a search result set.
// screen_items() operates on a stack of these so Series -> Season -> Episode
// can be navigated and 'b' pops back up.
#define MAX_BROWSE_DEPTH 8

typedef enum { BROWSE_NORMAL, BROWSE_RESUME, BROWSE_SEARCH } BrowseKind;

typedef struct {
    char id[64];          // ParentId, for BROWSE_NORMAL
    char name[128];       // display name / breadcrumb label
    BrowseKind kind;
    char search_term[128]; // for BROWSE_SEARCH
} BrowseNode;

static void draw_login_static(const char* server, const char* user) {
    clear_screen();
    printf("%s  medusa " RESET "%s- a lightweight Jellyfin terminal client\n\n" RESET, TH.header, TH.dim);
    printf("  %sServer:   " RESET "%s\n", TH.dim, server[0] ? server : "(not set)");
    printf("  %sUsername: " RESET "%s\n\n", TH.dim, user[0] ? user : "(not set)");
}

static void choose_server(char* server, size_t server_sz) {
    clear_screen();
    printf("%s  medusa " RESET "%s- Login\n\n" RESET, TH.header, TH.dim);
    printf("  %sSearching for Jellyfin servers on your network...\n" RESET, TH.dim);
    fflush(stdout);
    discover_servers(1500);

    int cursor = 0;
    while (1) {
        int option_count = discovered_count + 1; // + "Enter manually"
        clear_screen();
        printf("%s  medusa " RESET "%s- Login\n\n" RESET, TH.header, TH.dim);
        if (discovered_count == 0) {
            printf("  %sNo servers found on this network.\n\n" RESET, TH.dim);
        } else {
            printf("  %sFound %d server%s on this network:\n\n" RESET,
                   TH.dim, discovered_count, discovered_count == 1 ? "" : "s");
        }
        for (int i = 0; i < discovered_count; i++) {
            const char* style = (i == cursor) ? TH.cursor_row : "";
            char name_col[32], addr_col[32];
            fit_column(name_col, sizeof(name_col), discovered[i].name, 24);
            fit_column(addr_col, sizeof(addr_col), discovered[i].address, 28);
            printf("  %s %s%s%s " RESET "\n", style, name_col, TH.dim, addr_col);
        }
        const char* manual_style = (cursor == discovered_count) ? TH.cursor_row : "";
        printf("  %s %-24s" RESET "\n", manual_style, "Enter manually...");

        print_footer("[Up/Down] Move   [Enter] Select   [r] Re-scan   [q] Quit");
        int ch = getchar();
        if (ch == 27) {
            if (getchar() == '[') {
                switch (getchar()) {
                    case 'A': cursor = (cursor - 1 + option_count) % option_count; break;
                    case 'B': cursor = (cursor + 1) % option_count; break;
                    default: break;
                }
            }
        } else if (ch == '\n' || ch == '\r' || ch == ' ') {
            if (cursor < discovered_count) {
                snprintf(server, server_sz, "%s", discovered[cursor].address);
            } else {
                read_line("  Server URL: ", server, server_sz, false);
            }
            return;
        } else if (ch == 'r' || ch == 'R') {
            clear_screen();
            printf("  %sRe-scanning...\n" RESET, TH.dim);
            fflush(stdout);
            discover_servers(1500);
            cursor = 0;
        } else if (ch == 'q') {
            ui_reset_terminal();
            exit(0);
        }
    }
}

static void screen_login(Config* cfg) {
    char username[64] = "";
    char password[128];

    while (1) {
        choose_server(cfg->server, sizeof(cfg->server));
        draw_login_static(cfg->server, username);
        read_line("  Username:   ", username, sizeof(username), false);
        draw_login_static(cfg->server, username);
        read_line("  Password:   ", password, sizeof(password), true);

        clear_screen();
        printf("%s  medusa " RESET "%s- connecting...\n\n" RESET, TH.header, TH.dim);
        printf("  %sConnecting to %s%s%s as %s%s%s...\n\n" RESET,
               TH.dim, TH.text, cfg->server, TH.dim, TH.text, username, TH.dim);
        fflush(stdout);

        char err[256];
        if (api_authenticate(cfg, username, password, err, sizeof(err))) {
            config_save(cfg);
            printf("  %sAuthenticated as %s%s" RESET "\n", TH.accent, TH.text, cfg->user_name);
            print_footer("Press any key to continue...");
            fflush(stdout);
            getchar();
            return;
        }

        printf("  %s%s" RESET "\n", TH.error, err);
        print_footer("Press any key to try again...");
        fflush(stdout);
        getchar();
    }
}

static bool is_browsable_library(const ApiLibrary* lib) {
    return strcmp(lib->collection_type, "movies") == 0 || strcmp(lib->collection_type, "tvshows") == 0;
}

static Screen screen_libraries(const Config* cfg, BrowseNode* out) {
    clear_screen();
    printf("%s  medusa " RESET "%s- Libraries\n\n" RESET, TH.header, TH.dim);
    printf("  %sLoading...\n" RESET, TH.dim);
    fflush(stdout);

    ApiLibrary raw[MAX_LIBRARIES];
    int raw_count = 0;
    char err[256];

    if (!api_get_libraries(cfg, raw, MAX_LIBRARIES, &raw_count, err, sizeof(err))) {
        clear_screen();
        printf("%s  medusa " RESET "%s- Libraries\n\n" RESET, TH.header, TH.dim);
        printf("  %s%s" RESET "\n\n", TH.error, err);
        print_footer("Press any key to go back to login...");
        fflush(stdout);
        getchar();
        return SCREEN_LOGIN;
    }

    ApiLibrary libs[MAX_LIBRARIES];
    int count = 0;
    for (int i = 0; i < raw_count; i++) {
        if (is_browsable_library(&raw[i])) libs[count++] = raw[i];
    }

    if (count == 0) {
        clear_screen();
        printf("%s  medusa " RESET "%s- Libraries\n\n" RESET, TH.header, TH.dim);
        printf("  %sNo movie or TV show libraries found on this server.\n" RESET, TH.dim);
        print_footer("[q] Quit   [Esc] Back to login");
        fflush(stdout);
        int ch = getchar();
        return (ch == 'q') ? SCREEN_QUIT : SCREEN_LOGIN;
    }

    int total_entries = count + 2; // + Continue Watching, + Search
    int cursor = 0;
    while (1) {
        clear_screen();
        printf("%s  medusa " RESET "%s- Libraries      [Theme: %s]\n\n" RESET,
               TH.header, TH.dim, TH.name);

        for (int i = 0; i < total_entries; i++) {
            const char* style = (i == cursor) ? TH.cursor_row : "";
            if (i == 0) {
                printf("  %s %-20s" RESET "\n", style, "Continue Watching");
            } else if (i == 1) {
                printf("  %s %-20s" RESET "\n", style, "Search");
                printf("  %s----------------------------------------" RESET "\n", TH.dim);
            } else {
                int li = i - 2;
                const char* kind = strcmp(libs[li].collection_type, "movies") == 0 ? "Movies" : "TV Shows";
                char name_col[32];
                fit_column(name_col, sizeof(name_col), libs[li].name, 20);
                printf("  %s %s%s%s " RESET "\n", style, name_col, TH.dim, kind);
            }
        }
        print_footer("[Up/Down] Move   [Enter] Open   [t] Theme   [q] Quit");

        int ch = getchar();
        if (ch == 27) {
            if (getchar() == '[') {
                switch (getchar()) {
                    case 'A': cursor = (cursor - 1 + total_entries) % total_entries; break;
                    case 'B': cursor = (cursor + 1) % total_entries; break;
                    default: break;
                }
            }
        } else if (ch == '\n' || ch == '\r' || ch == ' ') {
            if (cursor == 0) {
                out->kind = BROWSE_RESUME;
                out->id[0] = '\0';
                snprintf(out->name, sizeof(out->name), "Continue Watching");
                return SCREEN_ITEMS;
            } else if (cursor == 1) {
                char term[128];
                clear_screen();
                printf("%s  medusa " RESET "%s- Search\n\n" RESET, TH.header, TH.dim);
                read_line("  Search: ", term, sizeof(term), false);
                if (term[0] == '\0') continue; // empty query - stay on this screen
                out->kind = BROWSE_SEARCH;
                out->id[0] = '\0';
                snprintf(out->search_term, sizeof(out->search_term), "%s", term);
                snprintf(out->name, sizeof(out->name), "Search: %s", term);
                return SCREEN_ITEMS;
            } else {
                int li = cursor - 2;
                out->kind = BROWSE_NORMAL;
                snprintf(out->id, sizeof(out->id), "%s", libs[li].id);
                snprintf(out->name, sizeof(out->name), "%s", libs[li].name);
                return SCREEN_ITEMS;
            }
        } else if (ch == 't' || ch == 'T') {
            current_theme_idx = (current_theme_idx + 1) % TOTAL_THEMES;
        } else if (ch == 'q') {
            return SCREEN_QUIT;
        }
    }
}

#define ITEMS_PER_PAGE 12
#define OVERVIEW_MAX_LINES 6
#define OVERVIEW_WRAP_COL 68

// Word-wraps `text` into at most OVERVIEW_MAX_LINES lines, truncating with
// "..." if it doesn't fit, and always printing exactly OVERVIEW_MAX_LINES
// lines (padding with blanks) so the detail pane has a fixed height -
// otherwise everything below it (progress bar, footer) jumps around as the
// cursor moves between items with differently-sized overviews.
static void print_overview_fixed(const char* text) {
    char lines[OVERVIEW_MAX_LINES][OVERVIEW_WRAP_COL + 8];
    int used = 0;
    lines[0][0] = '\0';
    int col = 0;
    bool more_text = false;

    const char* p = text;
    while (*p) {
        const char* next_space = strchr(p, ' ');
        size_t word_len = next_space ? (size_t)(next_space - p) : strlen(p);

        if (col > 0 && col + (int)word_len > OVERVIEW_WRAP_COL) {
            if (used + 1 >= OVERVIEW_MAX_LINES) { more_text = true; break; }
            used++;
            lines[used][0] = '\0';
            col = 0;
        }

        size_t copy_len = word_len > OVERVIEW_WRAP_COL ? (size_t)OVERVIEW_WRAP_COL : word_len;
        strncat(lines[used], p, copy_len);
        strcat(lines[used], " ");
        col += (int)word_len + 1;

        p += word_len;
        while (*p == ' ') p++;
    }
    if (*p) more_text = true;
    used++; // number of lines actually filled

    for (int i = 0; i < used; i++) {
        char* ln = lines[i];
        size_t len = strlen(ln);
        while (len > 0 && ln[len - 1] == ' ') ln[--len] = '\0';

        if (i == used - 1 && more_text) {
            if (len > OVERVIEW_WRAP_COL - 1) len = OVERVIEW_WRAP_COL - 1;
            ln[len] = '\0';
            printf("  %s..." RESET "\n", ln);
        } else {
            printf("  %s\n", ln);
        }
    }
    for (int i = used; i < OVERVIEW_MAX_LINES; i++) printf("\n");
}

// Sort mode persists across screens (like the theme), so drilling from a
// library into a Series/Season keeps whatever order the user picked -
// except a season's episode list forces SORT_EPISODE on entry (see
// screen_items), since alphabetical order on episode titles doesn't match
// watch order ("Chapter 10" sorts before "Chapter 2").
typedef enum { SORT_NAME, SORT_YEAR_NEWEST, SORT_YEAR_OLDEST, SORT_EPISODE, SORT_COUNT } SortMode;
static SortMode sort_mode = SORT_NAME;

static const char* sort_mode_label(void) {
    switch (sort_mode) {
        case SORT_YEAR_NEWEST: return "Year (newest)";
        case SORT_YEAR_OLDEST: return "Year (oldest)";
        case SORT_EPISODE: return "Episode #";
        case SORT_NAME:
        default: return "Name";
    }
}

static int compare_items(const void* a, const void* b) {
    const ApiItem* ia = (const ApiItem*)a;
    const ApiItem* ib = (const ApiItem*)b;
    switch (sort_mode) {
        case SORT_YEAR_NEWEST:
            if (ib->year != ia->year) return ib->year - ia->year;
            return strcasecmp(ia->name, ib->name);
        case SORT_YEAR_OLDEST:
            if (ia->year != ib->year) return ia->year - ib->year;
            return strcasecmp(ia->name, ib->name);
        case SORT_EPISODE:
            if (ia->season_number != ib->season_number) return ia->season_number - ib->season_number;
            if (ia->episode_number != ib->episode_number) return ia->episode_number - ib->episode_number;
            return strcasecmp(ia->name, ib->name);
        case SORT_NAME:
        default:
            return strcasecmp(ia->name, ib->name);
    }
}

static void sort_items(ApiItem* items, int count) {
    qsort(items, (size_t)count, sizeof(ApiItem), compare_items);
}

// Episodes shown in a flat list (Continue Watching, Search) carry no season
// context on their own - "Chapter 1" is meaningless without the show name.
// Within a season's own episode list the show is already implied by the
// breadcrumb, but the episode number ("E3") still isn't - add whichever of
// series name / episode tag actually applies.
static void display_name(const ApiItem* item, char* out, size_t out_sz) {
    char ep_tag[16] = "";
    if (item->episode_number > 0) {
        if (item->season_number > 0) {
            snprintf(ep_tag, sizeof(ep_tag), "S%02dE%02d", item->season_number, item->episode_number);
        } else {
            snprintf(ep_tag, sizeof(ep_tag), "E%02d", item->episode_number);
        }
    }

    if (item->series_name[0] && ep_tag[0]) {
        snprintf(out, out_sz, "%s - %s - %s", item->series_name, ep_tag, item->name);
    } else if (item->series_name[0]) {
        snprintf(out, out_sz, "%s - %s", item->series_name, item->name);
    } else if (ep_tag[0]) {
        snprintf(out, out_sz, "%s - %s", ep_tag, item->name);
    } else {
        snprintf(out, out_sz, "%s", item->name);
    }
}

static void draw_items(const char* breadcrumb, ApiItem* items, int count, int cursor) {
    clear_screen();
    printf("%s  medusa " RESET "%s- %s      [Sort: %s]\n\n" RESET,
           TH.header, TH.dim, breadcrumb, sort_mode_label());

    int window_start = (cursor / ITEMS_PER_PAGE) * ITEMS_PER_PAGE;
    int window_end = window_start + ITEMS_PER_PAGE;
    if (window_end > count) window_end = count;

    for (int i = window_start; i < window_end; i++) {
        const char* style;
        if (i == cursor) style = TH.cursor_row;
        else if (items[i].played) style = TH.dim; // watched - gray it out (folder or not)
        else style = "";
        // "*" always means fully watched; ">" (only for an unwatched
        // folder) means there's more to see inside.
        const char* mark = items[i].played ? "*" : (items[i].is_folder ? ">" : " ");
        char full_name[384];
        display_name(&items[i], full_name, sizeof(full_name));
        char name_col[40];
        fit_column(name_col, sizeof(name_col), full_name, 32);
        if (items[i].year > 0) {
            printf("  %s%s %s%s(%d)" RESET "\n", style, mark, name_col, TH.dim, items[i].year);
        } else {
            printf("  %s%s %s" RESET "\n", style, mark, name_col);
        }
    }

    int total_pages = (count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int current_page = window_start / ITEMS_PER_PAGE + 1;
    printf("  %s%d-%d of %d" RESET "%s  (page %d/%d)" RESET "\n",
           TH.dim, window_start + 1, window_end, count, TH.dim, current_page, total_pages);

    printf("\n  %s--------------------------------------------------------" RESET "\n", TH.dim);
    ApiItem* sel = &items[cursor];
    char sel_display_name[384];
    display_name(sel, sel_display_name, sizeof(sel_display_name));
    printf("  %s%s" RESET, TH.header, sel_display_name);
    if (sel->year > 0) printf("  %s%d" RESET, TH.dim, sel->year);
    if (sel->runtime_min > 0) printf("  %s%d min" RESET, TH.dim, sel->runtime_min);
    printf("\n  %s\n", TH.text);
    print_overview_fixed(sel->overview);
    printf(RESET "\n  ");
    if (sel->progress_pct > 0 && sel->progress_pct < 100) {
        printf("Resume at ");
        print_progress_bar(sel->progress_pct, 30);
        printf("\n");
    } else if (sel->played) {
        printf("%sWatched" RESET "\n", TH.accent);
    } else {
        printf("%sUnwatched" RESET "\n", TH.dim);
    }

    if (sel->is_folder) {
        print_footer("[Up/Down] Move  [Left/Right] Page  [Enter] Open  [w] Watched  [s] Sort  [Esc/b] Back  [t] Theme  [q] Quit");
    } else {
        print_footer("[Up/Down] Move  [Left/Right] Page  [Enter] Info/Play  [w] Watched  [s] Sort  [Esc/b] Back  [t] Theme  [q] Quit");
    }
}

// Toggles played state for `item` via the server, which cascades to all
// children server-side if item is a Series/Season (no need to walk the
// tree ourselves). Reflects the change locally so the list/detail screens
// show it immediately without a refetch.
static void toggle_played(const Config* cfg, ApiItem* item) {
    bool new_state = !item->played;
    char err[256];
    if (api_set_played(cfg, item->id, new_state, err, sizeof(err))) {
        item->played = new_state;
        item->progress_pct = 0;
    } else {
        clear_screen();
        printf("  %sCouldn't update watched state: %s" RESET "\n\n", TH.error, err);
        print_footer("Press any key to continue...");
        fflush(stdout);
        getchar();
    }
}

static void generate_session_id(char* out, size_t out_sz) {
    unsigned char bytes[16] = {0};
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(bytes, 1, sizeof(bytes), f);
        (void)n; // best-effort; zeroed bytes are a harmless fallback
        fclose(f);
    }
    snprintf(out, out_sz, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

// Audio/subtitle choice for a whole binge session (not re-asked per
// episode). TRACK_PREF_LANGUAGE is resolved fresh against each episode's
// own streams by language code, since track layout/order can differ
// episode to episode even within one season.
typedef enum { TRACK_PREF_DEFAULT, TRACK_PREF_NONE, TRACK_PREF_LANGUAGE } TrackPrefKind;

typedef struct {
    bool chosen; // true once the user has been through the picker at least once
    TrackPrefKind audio_kind;
    char audio_language[8];
    TrackPrefKind subtitle_kind; // TRACK_PREF_NONE only meaningful for subtitles
    char subtitle_language[8];
} TrackPreference;

static void format_stream_row(const MediaStreamInfo* s, char* out, size_t out_sz) {
    char base[180];
    if (s->display_title[0]) {
        snprintf(base, sizeof(base), "%s", s->display_title);
    } else if (s->language[0]) {
        snprintf(base, sizeof(base), "%s (%s)", s->language, s->codec);
    } else {
        snprintf(base, sizeof(base), "Track (%s)", s->codec);
    }
    snprintf(out, out_sz, "%s%s%s", base,
             s->is_default ? "  [Default]" : "",
             s->is_forced ? "  [Forced]" : "");
}

// Lets the user pick one Audio or Subtitle stream for `item` (plus a
// "Default" option, and "None" for subtitles). Fetches the item's streams
// itself, so it's safe to call standalone (e.g. from a manual 'a'/'s' key)
// as well as as part of the automatic first-play flow.
static TrackPrefKind screen_select_track_kind(const Config* cfg, const ApiItem* item, const char* type,
                                               char* out_language, size_t out_language_sz) {
    MediaStreamInfo streams[MAX_MEDIA_STREAMS];
    int stream_count = 0;
    char err[256];

    clear_screen();
    printf("%s  medusa " RESET "%s- Loading %s tracks...\n\n" RESET, TH.header, TH.dim, type);
    fflush(stdout);

    if (!api_get_media_streams(cfg, item->id, streams, MAX_MEDIA_STREAMS, &stream_count, err, sizeof(err))) {
        clear_screen();
        printf("  %s%s" RESET "\n\n", TH.error, err);
        print_footer("Press any key to continue...");
        fflush(stdout);
        getchar();
        return TRACK_PREF_DEFAULT;
    }

    int indices[MAX_MEDIA_STREAMS];
    int n = 0;
    for (int i = 0; i < stream_count; i++) {
        if (strcmp(streams[i].type, type) == 0) indices[n++] = i;
    }

    if (n == 0) {
        clear_screen();
        printf("  %sNo %s tracks reported for this item.\n" RESET, TH.dim, type);
        print_footer("Press any key to continue...");
        fflush(stdout);
        getchar();
        return TRACK_PREF_DEFAULT;
    }

    bool is_subtitle = strcmp(type, "Subtitle") == 0;
    int offset = 1 + (is_subtitle ? 1 : 0); // "Default" [+ "None"] before the stream rows
    int option_count = n + offset;
    int cursor = 0;

    while (1) {
        clear_screen();
        printf("%s  medusa " RESET "%s- Select %s Track\n\n" RESET, TH.header, TH.dim, type);
        for (int i = 0; i < option_count; i++) {
            const char* style = (i == cursor) ? TH.cursor_row : "";
            if (i == 0) {
                printf("  %s %-20s" RESET "\n", style, "Default");
            } else if (is_subtitle && i == 1) {
                printf("  %s %-20s" RESET "\n", style, "None");
            } else {
                char row[220];
                format_stream_row(&streams[indices[i - offset]], row, sizeof(row));
                printf("  %s %s" RESET "\n", style, row);
            }
        }
        print_footer("[Up/Down] Move   [Enter] Select   [Esc] Cancel");
        fflush(stdout);

        int ch = getchar();
        if (ch == 27) {
            int next = getchar();
            if (next == '[') {
                switch (getchar()) {
                    case 'A': cursor = (cursor - 1 + option_count) % option_count; break;
                    case 'B': cursor = (cursor + 1) % option_count; break;
                    default: break;
                }
            } else {
                return TRACK_PREF_DEFAULT; // bare Esc - cancel, keep whatever it was
            }
        } else if (ch == '\n' || ch == '\r' || ch == ' ') {
            if (cursor == 0) return TRACK_PREF_DEFAULT;
            if (is_subtitle && cursor == 1) return TRACK_PREF_NONE;
            snprintf(out_language, out_language_sz, "%s", streams[indices[cursor - offset]].language);
            return TRACK_PREF_LANGUAGE;
        }
    }
}

// Applies pref to whatever is currently loaded in mpv, re-resolving by
// language against THIS item's own streams (track order/count can differ
// episode to episode, so we can't just reuse indices from a previous pick).
static void apply_track_preference(const Config* cfg, MpvPlayer* mpv, const ApiItem* item, const TrackPreference* pref) {
    if (pref->audio_kind == TRACK_PREF_DEFAULT && pref->subtitle_kind == TRACK_PREF_DEFAULT) return;

    MediaStreamInfo streams[MAX_MEDIA_STREAMS];
    int stream_count = 0;
    char err[256];
    bool have_streams = (pref->audio_kind == TRACK_PREF_LANGUAGE || pref->subtitle_kind == TRACK_PREF_LANGUAGE)
                            ? api_get_media_streams(cfg, item->id, streams, MAX_MEDIA_STREAMS, &stream_count, err, sizeof(err))
                            : false;

    if (pref->audio_kind == TRACK_PREF_LANGUAGE && have_streams) {
        for (int i = 0; i < stream_count; i++) {
            if (strcmp(streams[i].type, "Audio") == 0 && strcasecmp(streams[i].language, pref->audio_language) == 0) {
                mpv_set_audio_track(mpv, streams[i].track_number);
                break;
            }
        }
    }

    if (pref->subtitle_kind == TRACK_PREF_NONE) {
        mpv_disable_subtitles(mpv);
    } else if (pref->subtitle_kind == TRACK_PREF_LANGUAGE && have_streams) {
        for (int i = 0; i < stream_count; i++) {
            if (strcmp(streams[i].type, "Subtitle") == 0 && strcasecmp(streams[i].language, pref->subtitle_language) == 0) {
                if (streams[i].is_external) {
                    char sub_url[700];
                    api_build_subtitle_url(cfg, item->id, &streams[i], sub_url, sizeof(sub_url));
                    mpv_add_subtitle(mpv, sub_url);
                } else {
                    mpv_set_subtitle_track(mpv, streams[i].track_number);
                }
                break;
            }
        }
    }
}

// Remembers pref in the config file, so a later run doesn't need to re-pick.
static void save_track_preference(Config* cfg, const TrackPreference* pref) {
    if (pref->audio_kind == TRACK_PREF_LANGUAGE) {
        snprintf(cfg->pref_audio_lang, sizeof(cfg->pref_audio_lang), "%s", pref->audio_language);
    } else {
        cfg->pref_audio_lang[0] = '\0';
    }

    if (pref->subtitle_kind == TRACK_PREF_NONE) {
        snprintf(cfg->pref_subtitle_lang, sizeof(cfg->pref_subtitle_lang), "none");
    } else if (pref->subtitle_kind == TRACK_PREF_LANGUAGE) {
        snprintf(cfg->pref_subtitle_lang, sizeof(cfg->pref_subtitle_lang), "%s", pref->subtitle_language);
    } else {
        cfg->pref_subtitle_lang[0] = '\0';
    }

    config_save(cfg);
}

// Called when the user switches audio/subtitle tracks directly in mpv
// (its own default keybindings) rather than through our picker - a
// deliberate action, so we adopt it as the new preference by language (same
// as a picker choice) and persist it, exactly as if they'd picked it from
// our menu. track_number 0 means "off", only meaningful for subtitles.
static void adopt_live_track_change(Config* cfg, const ApiItem* item, const char* type, int track_number,
                                     TrackPreference* pref) {
    bool is_subtitle = strcmp(type, "Subtitle") == 0;

    if (track_number == 0) {
        if (is_subtitle) {
            pref->subtitle_kind = TRACK_PREF_NONE;
            save_track_preference(cfg, pref);
        }
        return; // aid 0 isn't a meaningful "no audio" state worth adopting
    }

    MediaStreamInfo streams[MAX_MEDIA_STREAMS];
    int count = 0;
    char err[256];
    if (!api_get_media_streams(cfg, item->id, streams, MAX_MEDIA_STREAMS, &count, err, sizeof(err))) return;

    for (int i = 0; i < count; i++) {
        if (strcmp(streams[i].type, type) == 0 && streams[i].track_number == track_number) {
            if (is_subtitle) {
                pref->subtitle_kind = TRACK_PREF_LANGUAGE;
                snprintf(pref->subtitle_language, sizeof(pref->subtitle_language), "%s", streams[i].language);
            } else {
                pref->audio_kind = TRACK_PREF_LANGUAGE;
                snprintf(pref->audio_language, sizeof(pref->audio_language), "%s", streams[i].language);
            }
            save_track_preference(cfg, pref);
            return;
        }
    }
}

// Plays item in mpv, reporting progress back to Jellyfin while it runs, and
// mutates item->played/progress_pct to match what was actually watched (so
// the list/detail screens show it immediately, no server round trip).
// Returns true if playback reached (approximately) the end on its own - the
// signal screen_items uses to auto-advance to the next episode - as opposed
// to the user stopping early with 'q'.
//
// *mpv is owned by the caller across a whole binge session, not per item:
// if is_first, this starts mpv fresh (a new window); otherwise it loads
// item into the SAME already-running mpv (same window) via IPC, so moving
// between consecutive episodes doesn't spawn (and re-maximize) a new window
// each time. The caller is responsible for mpv_stop() once the session ends.
//
// Also watches for the user switching audio/subtitle tracks directly in
// mpv while it plays, and adopts that as the new preference (see
// adopt_live_track_change) - a deliberate action should stick, the same as
// picking it from our own menu.
static bool play_item(Config* cfg, MpvPlayer* mpv, bool is_first, ApiItem* item, const char* display,
                      TrackPreference* pref) {
    char stream_url[600];
    api_build_stream_url(cfg, item->id, stream_url, sizeof(stream_url));

    char session_id[40];
    generate_session_id(session_id, sizeof(session_id));

    // Prefer Jellyfin's own runtime (known instantly) over mpv's "duration"
    // property, which depends on mpv fully introspecting the stream - that
    // doesn't reliably resolve over a network stream (container metadata
    // position, HTTP range support), even though it works for a local file.
    double known_duration = item->runtime_ticks > 0 ? (double)item->runtime_ticks / 10000000.0 : 0;

    // Resume where we left off. Ignore a saved position that's implausibly
    // close to (or past) the end - that's effectively "finished", not a
    // real resume point. This resume courtesy is only for the episode the
    // user explicitly chose to play (is_first) - an auto-advanced episode
    // always starts from 0, even if it has old partial-watch progress from
    // some unrelated earlier viewing; jumping into the middle of a episode
    // you didn't ask for isn't what "next episode" should mean mid-binge.
    double resume_seconds = (is_first && item->playback_position_ticks > 0)
                                 ? (double)item->playback_position_ticks / 10000000.0
                                 : 0;
    if (known_duration > 0 && resume_seconds > known_duration - 10) resume_seconds = 0;

    clear_screen();
    printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim,
           is_first ? "Launching mpv..." : "Loading next episode...");
    fflush(stdout);

    if (is_first) {
        char err[256];
        if (!mpv_start(mpv, stream_url, resume_seconds, err, sizeof(err))) {
            printf("  %s%s" RESET "\n\n", TH.error, err);
            print_footer("Press any key to go back...");
            fflush(stdout);
            getchar();
            return false;
        }
        mpv_wait_for_file_loaded(mpv, 5000); // so track selection below has real tracks to work with
    } else {
        mpv_load_file(mpv, stream_url);
        if (mpv_wait_for_file_loaded(mpv, 5000)) {
            // mpv's "--start" option (set once, at the very first mpv_start
            // launch) turns out to be sticky: it silently re-seeks every
            // SUBSEQUENT loadfile back to that same position too, moments
            // after file-loaded fires. Confirmed by direct testing: seeking
            // immediately gets raced and overridden by that internal
            // re-seek; a short settle delay first lets it happen, so our
            // explicit seek (always, even to 0 for an unwatched episode)
            // then sticks instead of being clobbered.
            usleep(500000);
            mpv_seek_absolute(mpv, resume_seconds);
        }
        // "pause" is player-wide, not per-file: if the previous episode
        // ended via --keep-open=yes (which pauses instead of exiting),
        // that leftover paused state would otherwise carry into this one.
        mpv_set_pause(mpv, false);
    }

    mpv_set_title(mpv, display);
    apply_track_preference(cfg, mpv, item, pref);

    char report_err[256];
    long long start_ticks = (long long)(resume_seconds * 10000000.0);
    api_report_playback_start(cfg, item->id, item->id, session_id, start_ticks, report_err, sizeof(report_err));

    double last_reported_pos = -1000;
    double time_pos = 0;
    double mpv_duration = 0;
    bool paused = false;
    bool user_quit = false;
    bool eof_reached = false;
    int last_aid = -1, last_sid = -1; // -1 = not yet observed, so the first poll (which reflects
                                       // whatever apply_track_preference just set) isn't mistaken for a live change

    while (mpv_is_running(mpv) && !eof_reached) {
        int aid = -1, sid = -1;
        bool got_status = mpv_get_status(mpv, &time_pos, &mpv_duration, &paused, &eof_reached, &aid, &sid);
        double duration = known_duration > 0 ? known_duration : mpv_duration;

        if (got_status) {
            if (last_aid == -1) {
                last_aid = aid;
                last_sid = sid;
            } else {
                if (aid != last_aid) {
                    adopt_live_track_change(cfg, item, "Audio", aid, pref);
                    last_aid = aid;
                }
                if (sid != last_sid) {
                    adopt_live_track_change(cfg, item, "Subtitle", sid, pref);
                    last_sid = sid;
                }
            }
        }

        clear_screen();
        printf("%s  medusa " RESET "%s- Now Playing\n\n" RESET, TH.header, TH.dim);
        printf("  %s%s" RESET "\n\n", TH.header, display);
        if (got_status) {
            int mins = (int)time_pos / 60, secs = (int)time_pos % 60;
            printf("  %s%02d:%02d%s" RESET "\n\n", TH.dim, mins, secs, paused ? "  (paused)" : "");
            if (duration > 0) {
                int pct = (int)((time_pos / duration) * 100.0);
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                printf("  Playing ");
                print_progress_bar(pct, 30);
                printf("\n");
            }
        } else {
            printf("  %sConnecting to mpv...\n" RESET, TH.dim);
        }
        print_footer("Video is playing in mpv's window.   [q] Stop and go back");
        fflush(stdout);

        if (got_status && time_pos - last_reported_pos >= 5.0) {
            long long position_ticks = (long long)(time_pos * 10000000.0);
            api_report_playback_progress(cfg, item->id, item->id, session_id, position_ticks, paused,
                                          report_err, sizeof(report_err));
            last_reported_pos = time_pos;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 500000};
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            int ch = getchar();
            if (ch == 'q') {
                user_quit = true;
                const char quit_cmd[] = "{\"command\":[\"quit\"]}\n";
                write(mpv->sock_fd, quit_cmd, strlen(quit_cmd));
            }
        }
    }

    long long final_ticks = (long long)(time_pos * 10000000.0);
    api_report_playback_stopped(cfg, item->id, item->id, session_id, final_ticks, report_err, sizeof(report_err));
    // NOTE: mpv is NOT stopped here - the caller owns it across the whole
    // binge session and stops it once, after the last episode.

    double effective_duration = known_duration > 0 ? known_duration : mpv_duration;
    // eof_reached is the reliable signal (mpv told us directly, via
    // --keep-open=yes so it can't have raced its own exit). Fall back to
    // the proximity check only if we never got a clean status read at all.
    bool reached_end = !user_quit &&
                        (eof_reached || (effective_duration > 0 && time_pos >= effective_duration - 2.0));

    if (effective_duration > 0) {
        int pct = (int)((time_pos / effective_duration) * 100.0);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (reached_end || pct >= 90) {
            item->played = true;
            item->progress_pct = 0;
        } else {
            item->played = false;
            item->progress_pct = pct;
        }
    }

    return reached_end;
}

typedef enum { DETAIL_BACK, DETAIL_PLAYED } DetailResult;

// Dedicated screen for a playable item (Movie/Episode): its full info plus
// a Play action. Returns DETAIL_PLAYED (with *out_reached_end set) as soon
// as playback ends, rather than looping back to itself - the caller decides
// whether that's a cue to auto-advance to the next episode.
static DetailResult screen_item_detail(Config* cfg, MpvPlayer* mpv, ApiItem* item, TrackPreference* pref,
                                        bool* out_reached_end) {
    char full_name[384];
    display_name(item, full_name, sizeof(full_name));

    while (1) {
        clear_screen();
        printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim, full_name);
        printf("  %s%s" RESET, TH.header, full_name);
        if (item->year > 0) printf("  %s%d" RESET, TH.dim, item->year);
        if (item->runtime_min > 0) printf("  %s%d min" RESET, TH.dim, item->runtime_min);
        printf("\n\n  %s\n", TH.text);
        print_overview_fixed(item->overview);
        printf(RESET "\n  ");
        if (item->progress_pct > 0 && item->progress_pct < 100) {
            printf("Resume at ");
            print_progress_bar(item->progress_pct, 30);
            printf("\n");
        } else if (item->played) {
            printf("%sWatched" RESET "\n", TH.accent);
        } else {
            printf("%sUnwatched" RESET "\n", TH.dim);
        }

        print_footer("[Enter/p] Play  [a] Audio  [s] Subtitles  [w] Watched  [Esc/b] Back  [t] Theme");
        fflush(stdout);

        int ch = getchar();
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == 'p' || ch == 'P') {
            if (!pref->chosen) {
                pref->audio_kind = screen_select_track_kind(cfg, item, "Audio", pref->audio_language, sizeof(pref->audio_language));
                pref->subtitle_kind = screen_select_track_kind(cfg, item, "Subtitle", pref->subtitle_language, sizeof(pref->subtitle_language));
                pref->chosen = true;
                save_track_preference(cfg, pref);
            }
            *out_reached_end = play_item(cfg, mpv, true, item, full_name, pref);
            return DETAIL_PLAYED;
        } else if (ch == 'b') {
            return DETAIL_BACK;
        } else if (ch == 27) {
            int next = getchar();
            if (next != '[') return DETAIL_BACK; // bare Esc
            getchar(); // discard the arrow-key byte; nothing to navigate here
        } else if (ch == 'w' || ch == 'W') {
            toggle_played(cfg, item);
        } else if (ch == 'a' || ch == 'A') {
            pref->audio_kind = screen_select_track_kind(cfg, item, "Audio", pref->audio_language, sizeof(pref->audio_language));
            pref->chosen = true;
            save_track_preference(cfg, pref);
        } else if (ch == 's' || ch == 'S') {
            pref->subtitle_kind = screen_select_track_kind(cfg, item, "Subtitle", pref->subtitle_language, sizeof(pref->subtitle_language));
            pref->chosen = true;
            save_track_preference(cfg, pref);
        } else if (ch == 't' || ch == 'T') {
            current_theme_idx = (current_theme_idx + 1) % TOTAL_THEMES;
        }
    }
}

static void build_breadcrumb(const BrowseNode* stack, int depth, char* out, size_t out_sz) {
    out[0] = '\0';
    for (int i = 0; i < depth; i++) {
        if (i > 0) strncat(out, " / ", out_sz - strlen(out) - 1);
        strncat(out, stack[i].name, out_sz - strlen(out) - 1);
    }
}

// Plays items[start_idx] via its info/play confirm screen; if it finishes
// on its own and it's an episode with a next one queued up in a real
// season list (kind == BROWSE_NORMAL), continues straight into the next
// episode with no confirm screen in between - a "binge" flow. Returns the
// index of whatever was last played, so the caller can land its cursor there.
//
// One mpv process/window is used for the whole session: the first item
// gets a fresh mpv_start (a new window), and every subsequent auto-advanced
// episode is loaded into that SAME window via IPC - otherwise every episode
// transition would pop a brand-new (un-maximized) mpv window.
static int play_flow(Config* cfg, ApiItem* items, int count, int start_idx, BrowseKind kind) {
    MpvPlayer mpv;
    memset(&mpv, 0, sizeof(mpv));
    mpv.sock_fd = -1; // mpv_stop()'s guards treat this as "never started"

    TrackPreference pref;
    memset(&pref, 0, sizeof(pref));
    if (cfg->pref_audio_lang[0]) {
        pref.audio_kind = TRACK_PREF_LANGUAGE;
        snprintf(pref.audio_language, sizeof(pref.audio_language), "%s", cfg->pref_audio_lang);
    }
    if (strcmp(cfg->pref_subtitle_lang, "none") == 0) {
        pref.subtitle_kind = TRACK_PREF_NONE;
    } else if (cfg->pref_subtitle_lang[0]) {
        pref.subtitle_kind = TRACK_PREF_LANGUAGE;
        snprintf(pref.subtitle_language, sizeof(pref.subtitle_language), "%s", cfg->pref_subtitle_lang);
    }
    // Don't re-prompt the picker if we already have a saved preference from
    // a previous run.
    if (cfg->pref_audio_lang[0] || cfg->pref_subtitle_lang[0]) pref.chosen = true;

    int play_idx = start_idx;
    bool first = true;
    while (1) {
        ApiItem* playing = &items[play_idx];
        bool reached_end = false;

        if (first) {
            screen_item_detail(cfg, &mpv, playing, &pref, &reached_end);
            first = false;
        } else {
            char next_display[384];
            display_name(playing, next_display, sizeof(next_display));
            reached_end = play_item(cfg, &mpv, false, playing, next_display, &pref);
        }

        // Only auto-advance inside a real season's episode list
        // (BROWSE_NORMAL) - Continue Watching/Search are flat, unordered
        // lists that can mix different shows, so "the next item" there is
        // meaningless as a "next episode".
        bool is_episode = strcmp(playing->type, "Episode") == 0;
        bool has_next = play_idx + 1 < count && !items[play_idx + 1].is_folder;
        if (reached_end && is_episode && has_next && kind == BROWSE_NORMAL) {
            play_idx++;
            continue;
        }
        break;
    }

    mpv_stop(&mpv); // no-op if mpv was never actually started
    return play_idx;
}

// Reconstructs the real browse path (Library [> Series > Season]) for an
// item found via a flat list (Continue Watching, Search), fetches that
// location's items, and locates the item among them - so back-navigation
// and episode auto-advance behave as if the user had drilled down there
// normally, instead of relative to an unrelated flat list. On any failure
// (unsupported server, item missing from the refetched list, ...) returns
// false and leaves everything untouched; the caller falls back to playing
// the item in place.
static bool redirect_to_real_location(const Config* cfg, const ApiItem* item,
                                       BrowseNode* stack, int* depth,
                                       ApiItem** out_items, int* out_count, int* out_cursor,
                                       char* fail_reason, size_t fail_reason_sz) {
    fail_reason[0] = '\0';

    AncestorNode ancestors[MAX_ANCESTORS];
    int ancestor_count = 0;
    char err[256];

    if (!api_get_ancestors(cfg, item->id, ancestors, MAX_ANCESTORS, &ancestor_count, err, sizeof(err))) {
        snprintf(fail_reason, fail_reason_sz, "ancestors lookup failed: %s", err);
        return false;
    }
    if (ancestor_count == 0) {
        snprintf(fail_reason, fail_reason_sz, "server returned no ancestors for this item");
        return false;
    }

    // Ancestors come back immediate-parent-first (Season, Series, Library,
    // ...). Keep everything up to and including the first CollectionFolder
    // (the top-level library) and drop anything above it.
    int keep = 0;
    for (int i = 0; i < ancestor_count; i++) {
        keep = i + 1;
        if (strcmp(ancestors[i].type, "CollectionFolder") == 0) break;
    }
    if (keep == 0) {
        snprintf(fail_reason, fail_reason_sz, "no CollectionFolder found among ancestors");
        return false;
    }

    BrowseNode new_stack[MAX_BROWSE_DEPTH];
    int new_depth = 0;
    for (int i = keep - 1; i >= 0 && new_depth < MAX_BROWSE_DEPTH; i--) {
        memset(&new_stack[new_depth], 0, sizeof(BrowseNode));
        new_stack[new_depth].kind = BROWSE_NORMAL;
        snprintf(new_stack[new_depth].id, sizeof(new_stack[new_depth].id), "%s", ancestors[i].id);
        snprintf(new_stack[new_depth].name, sizeof(new_stack[new_depth].name), "%s", ancestors[i].name);
        new_depth++;
    }
    if (new_depth == 0) {
        snprintf(fail_reason, fail_reason_sz, "could not build a browse path from ancestors");
        return false;
    }

    ApiItem* new_items = NULL;
    int new_count = 0;
    if (!api_get_items(cfg, new_stack[new_depth - 1].id, &new_items, &new_count, err, sizeof(err))) {
        snprintf(fail_reason, fail_reason_sz, "fetching %s failed: %s", new_stack[new_depth - 1].name, err);
        free(new_items);
        return false;
    }
    if (new_count == 0) {
        snprintf(fail_reason, fail_reason_sz, "%s came back empty", new_stack[new_depth - 1].name);
        free(new_items);
        return false;
    }

    if (strcmp(item->type, "Episode") == 0 && strcmp(new_items[0].type, "Episode") == 0) {
        sort_mode = SORT_EPISODE;
    }
    sort_items(new_items, new_count);

    int found_idx = -1;
    for (int i = 0; i < new_count; i++) {
        if (strcmp(new_items[i].id, item->id) == 0) { found_idx = i; break; }
    }

    // Some libraries treat Season as a virtual/metadata-only grouping, not
    // a physical ancestor - Jellyfin's Ancestors chain then skips straight
    // from Episode to Series, so we land one level too high (at the
    // Series, whose children are Season folders, not episodes). If what
    // came back is folders rather than our item, drill one more level:
    // the single folder if there's only one, or the one whose own season
    // number (a Season's IndexNumber) matches the episode's season number.
    if (found_idx < 0 && new_items[0].is_folder) {
        int folder_idx = -1;
        if (new_count == 1) {
            folder_idx = 0;
        } else if (item->season_number > 0) {
            for (int i = 0; i < new_count; i++) {
                if (new_items[i].episode_number == item->season_number) { folder_idx = i; break; }
            }
        }

        if (folder_idx >= 0 && new_depth < MAX_BROWSE_DEPTH) {
            char folder_id[64], folder_name[128];
            snprintf(folder_id, sizeof(folder_id), "%s", new_items[folder_idx].id);
            snprintf(folder_name, sizeof(folder_name), "%s", new_items[folder_idx].name);
            free(new_items);
            new_items = NULL;

            memset(&new_stack[new_depth], 0, sizeof(BrowseNode));
            new_stack[new_depth].kind = BROWSE_NORMAL;
            snprintf(new_stack[new_depth].id, sizeof(new_stack[new_depth].id), "%s", folder_id);
            snprintf(new_stack[new_depth].name, sizeof(new_stack[new_depth].name), "%s", folder_name);
            new_depth++;

            if (!api_get_items(cfg, folder_id, &new_items, &new_count, err, sizeof(err)) || new_count == 0) {
                snprintf(fail_reason, fail_reason_sz, "fetching %s failed", folder_name);
                free(new_items);
                return false;
            }
            if (strcmp(item->type, "Episode") == 0 && strcmp(new_items[0].type, "Episode") == 0) {
                sort_mode = SORT_EPISODE;
            }
            sort_items(new_items, new_count);
            for (int i = 0; i < new_count; i++) {
                if (strcmp(new_items[i].id, item->id) == 0) { found_idx = i; break; }
            }
        }
    }

    if (found_idx < 0) {
        snprintf(fail_reason, fail_reason_sz, "item not found among %d refetched item(s)", new_count);
        free(new_items);
        return false;
    }

    for (int i = 0; i < new_depth; i++) stack[i] = new_stack[i];
    *depth = new_depth;
    *out_items = new_items;
    *out_count = new_count;
    *out_cursor = found_idx;
    return true;
}

// Browses whatever is on top of *stack: a library, a Series/Season drilled
// into from one, Continue Watching, or a search result set. Enter on a
// folder item (Series/Season) pushes it and loops to fetch its children;
// Enter on a playable item (Movie/Episode) opens the detail/play screen.
// 'b'/Esc pops one level, returning to SCREEN_LIBRARIES once empty.
static Screen screen_items(Config* cfg, BrowseNode* stack, int* depth) {
    while (1) {
        BrowseNode node = stack[*depth - 1];
        char breadcrumb[256];
        build_breadcrumb(stack, *depth, breadcrumb, sizeof(breadcrumb));

        clear_screen();
        printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim, breadcrumb);
        printf("  %sLoading...\n" RESET, TH.dim);
        fflush(stdout);

        ApiItem* items = NULL;
        int count = 0;
        char err[256];
        bool fetch_ok;

        switch (node.kind) {
            case BROWSE_RESUME:
                fetch_ok = api_get_continue_watching(cfg, &items, &count, err, sizeof(err));
                break;
            case BROWSE_SEARCH:
                fetch_ok = api_search_items(cfg, node.search_term, &items, &count, err, sizeof(err));
                break;
            default:
                fetch_ok = api_get_items(cfg, node.id, &items, &count, err, sizeof(err));
                break;
        }

        if (!fetch_ok) {
            clear_screen();
            printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim, breadcrumb);
            printf("  %s%s" RESET "\n\n", TH.error, err);
            print_footer("Press any key to go back...");
            fflush(stdout);
            getchar();
            (*depth)--;
            return (*depth <= 0) ? SCREEN_LIBRARIES : SCREEN_ITEMS;
        }

        if (count == 0) {
            free(items);
            clear_screen();
            printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim, breadcrumb);
            printf("  %sThis is empty.\n" RESET, TH.dim);
            print_footer("Press any key to go back...");
            fflush(stdout);
            getchar();
            (*depth)--;
            return (*depth <= 0) ? SCREEN_LIBRARIES : SCREEN_ITEMS;
        }

        // A season's episode list defaults to episode order, not whatever
        // sort mode was left over from browsing something else.
        if (node.kind == BROWSE_NORMAL && strcmp(items[0].type, "Episode") == 0) {
            sort_mode = SORT_EPISODE;
        }
        sort_items(items, count);

        int cursor = 0;
        bool go_deeper = false;
        Screen leave_as = SCREEN_ITEMS; // set on any exit path other than "drill deeper"
        bool leaving = false;

        while (!leaving) {
            draw_items(breadcrumb, items, count, cursor);
            int ch = getchar();
            if (ch == 27) {
                int next = getchar();
                if (next == '[') {
                    switch (getchar()) {
                        case 'A': cursor = (cursor - 1 + count) % count; break;
                        case 'B': cursor = (cursor + 1) % count; break;
                        case 'C':
                            cursor = (cursor + ITEMS_PER_PAGE < count) ? cursor + ITEMS_PER_PAGE : count - 1;
                            break;
                        case 'D':
                            cursor = (cursor - ITEMS_PER_PAGE >= 0) ? cursor - ITEMS_PER_PAGE : 0;
                            break;
                        default: break;
                    }
                } else {
                    (*depth)--;
                    leave_as = (*depth <= 0) ? SCREEN_LIBRARIES : SCREEN_ITEMS;
                    leaving = true;
                }
            } else if (ch == 'b') {
                (*depth)--;
                leave_as = (*depth <= 0) ? SCREEN_LIBRARIES : SCREEN_ITEMS;
                leaving = true;
            } else if (ch == '\n' || ch == '\r' || ch == ' ') {
                ApiItem* sel = &items[cursor];
                if (sel->is_folder) {
                    if (*depth < MAX_BROWSE_DEPTH) {
                        stack[*depth].kind = BROWSE_NORMAL;
                        snprintf(stack[*depth].id, sizeof(stack[*depth].id), "%s", sel->id);
                        snprintf(stack[*depth].name, sizeof(stack[*depth].name), "%s", sel->name);
                        (*depth)++;
                    }
                    go_deeper = true;
                    leaving = true;
                } else if (node.kind != BROWSE_NORMAL) {
                    // Flat list (Continue Watching/Search): jump to the
                    // item's real location - Library, or Library/Series/
                    // Season for an episode - so from here on this behaves
                    // exactly like normal browsing (back-navigation, episode
                    // auto-advance) instead of relative to an unrelated list.
                    ApiItem* new_items = NULL;
                    int new_count = 0, new_cursor = 0;
                    char redirect_err[256];
                    if (redirect_to_real_location(cfg, sel, stack, depth, &new_items, &new_count, &new_cursor,
                                                   redirect_err, sizeof(redirect_err))) {
                        free(items);
                        items = new_items;
                        count = new_count;
                        node = stack[*depth - 1];
                        build_breadcrumb(stack, *depth, breadcrumb, sizeof(breadcrumb));
                        cursor = play_flow(cfg, items, count, new_cursor, node.kind);
                    } else {
                        clear_screen();
                        printf("%s  medusa " RESET "%s- %s\n\n" RESET, TH.header, TH.dim, breadcrumb);
                        printf("  %sCouldn't jump to %s's real location:" RESET "\n  %s\n\n",
                               TH.dim, sel->name, redirect_err);
                        print_footer("Playing here instead. Press any key to continue...");
                        fflush(stdout);
                        getchar();
                        cursor = play_flow(cfg, items, count, cursor, node.kind);
                    }
                } else {
                    cursor = play_flow(cfg, items, count, cursor, node.kind);
                }
            } else if (ch == 's' || ch == 'S') {
                sort_mode = (sort_mode + 1) % SORT_COUNT;
                sort_items(items, count);
                cursor = 0;
            } else if (ch == 'w' || ch == 'W') {
                toggle_played(cfg, &items[cursor]);
            } else if (ch == 'D') {
                char debug_err[256];
                if (api_dump_raw_item(cfg, items[cursor].id, "medusa-debug-item.json", debug_err, sizeof(debug_err))) {
                    clear_screen();
                    printf("  %sWrote raw item JSON to medusa-debug-item.json" RESET "\n\n", TH.dim);
                } else {
                    clear_screen();
                    printf("  %s%s" RESET "\n\n", TH.error, debug_err);
                }
                print_footer("Press any key to continue...");
                fflush(stdout);
                getchar();
            } else if (ch == 't' || ch == 'T') {
                current_theme_idx = (current_theme_idx + 1) % TOTAL_THEMES;
            } else if (ch == 'q') {
                leave_as = SCREEN_QUIT;
                leaving = true;
            }
        }

        free(items);
        if (!go_deeper) return leave_as;
        // go_deeper: loop back around and fetch the newly-pushed node's children
    }
}

void ui_run(Config* cfg) {
    Screen screen = (cfg->server[0] && cfg->access_token[0] && cfg->user_id[0]) ? SCREEN_LIBRARIES : SCREEN_LOGIN;
    BrowseNode stack[MAX_BROWSE_DEPTH];
    int depth = 0;

    while (screen != SCREEN_QUIT) {
        switch (screen) {
            case SCREEN_LOGIN:
                screen_login(cfg);
                screen = SCREEN_LIBRARIES;
                break;
            case SCREEN_LIBRARIES:
                screen = screen_libraries(cfg, &stack[0]);
                depth = 1;
                break;
            case SCREEN_ITEMS:
                screen = screen_items(cfg, stack, &depth);
                break;
            default:
                screen = SCREEN_QUIT;
                break;
        }
    }
}
