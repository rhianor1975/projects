// main.c - entry point: load config, run the UI, persist on exit.

#include "config.h"
#include "api.h"
#include "ui.h"
#include <signal.h>
#include <string.h>
#include <stdio.h>

static void print_help(const char* prog) {
    printf(
        "medusa - a lightweight terminal client for Jellyfin\n"
        "\n"
        "Usage:\n"
        "  %s              Launch the client\n"
        "  %s --debug      Also log every server request/response to\n"
        "                  medusa-debug.log (written next to wherever medusa runs)\n"
        "  %s --help       Show this help and exit\n"
        "\n"
        "Config:\n"
        "  Server, login, and playback (audio/subtitle) preferences are saved in\n"
        "  ~/.config/medusa/config (or $XDG_CONFIG_HOME/medusa/config).\n"
        "\n"
        "Requirements:\n"
        "  mpv must be installed and on PATH - it's what actually plays video,\n"
        "  in its own window, controlled by medusa.\n"
        "\n"
        "Each screen shows its own available keys at the bottom while running.\n",
        prog, prog, prog);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // Writing to mpv's IPC socket after it has exited (window closed, or
    // right after we send it "quit") would otherwise raise SIGPIPE, whose
    // default disposition kills the whole process - ignore it and let the
    // failed write()/send() just return an error instead.
    signal(SIGPIPE, SIG_IGN);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) api_set_debug(true);
    }

    api_init();
    ui_setup_terminal();

    Config cfg;
    config_load(&cfg);
    ui_run(&cfg);

    ui_reset_terminal();
    api_cleanup();
    return 0;
}
