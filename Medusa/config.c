// config.c - see config.h

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void config_dir(char* buf, size_t sz) {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdg && xdg[0]) snprintf(buf, sz, "%s/medusa", xdg);
    else snprintf(buf, sz, "%s/.config/medusa", home ? home : ".");
}

static void config_path(char* buf, size_t sz) {
    char dir[256];
    config_dir(dir, sizeof(dir));
    snprintf(buf, sz, "%s/config", dir);
}

static void generate_device_id(char* out, size_t out_sz) {
    unsigned char bytes[16] = {0};
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(bytes, 1, sizeof(bytes), f);
        (void)n; // best-effort; zeroed bytes are a harmless fallback
        fclose(f);
    }
    snprintf(out, out_sz,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static void trim_newline(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
}

bool config_load(Config* cfg) {
    memset(cfg, 0, sizeof(*cfg));

    char path[300];
    config_path(path, sizeof(path));

    FILE* f = fopen(path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            trim_newline(line);
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            const char* key = line;
            const char* val = eq + 1;
            if (strcmp(key, "server") == 0) snprintf(cfg->server, sizeof(cfg->server), "%s", val);
            else if (strcmp(key, "access_token") == 0) snprintf(cfg->access_token, sizeof(cfg->access_token), "%s", val);
            else if (strcmp(key, "user_id") == 0) snprintf(cfg->user_id, sizeof(cfg->user_id), "%s", val);
            else if (strcmp(key, "user_name") == 0) snprintf(cfg->user_name, sizeof(cfg->user_name), "%s", val);
            else if (strcmp(key, "device_id") == 0) snprintf(cfg->device_id, sizeof(cfg->device_id), "%s", val);
            else if (strcmp(key, "pref_audio_lang") == 0) snprintf(cfg->pref_audio_lang, sizeof(cfg->pref_audio_lang), "%s", val);
            else if (strcmp(key, "pref_subtitle_lang") == 0) snprintf(cfg->pref_subtitle_lang, sizeof(cfg->pref_subtitle_lang), "%s", val);
        }
        fclose(f);
    }

    if (cfg->device_id[0] == '\0') {
        generate_device_id(cfg->device_id, sizeof(cfg->device_id));
        config_save(cfg);
    }

    return cfg->server[0] && cfg->access_token[0] && cfg->user_id[0];
}

void config_save(const Config* cfg) {
    char dir[256];
    config_dir(dir, sizeof(dir));

    char parent[256];
    snprintf(parent, sizeof(parent), "%s", dir);
    char* slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        mkdir(parent, 0700);
    }
    mkdir(dir, 0700);

    char path[300];
    config_path(path, sizeof(path));

    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "server=%s\n", cfg->server);
    fprintf(f, "access_token=%s\n", cfg->access_token);
    fprintf(f, "user_id=%s\n", cfg->user_id);
    fprintf(f, "user_name=%s\n", cfg->user_name);
    fprintf(f, "device_id=%s\n", cfg->device_id);
    fprintf(f, "pref_audio_lang=%s\n", cfg->pref_audio_lang);
    fprintf(f, "pref_subtitle_lang=%s\n", cfg->pref_subtitle_lang);
    fclose(f);
    chmod(path, 0600); // contains an access token
}

void config_clear_session(Config* cfg) {
    cfg->server[0] = '\0';
    cfg->access_token[0] = '\0';
    cfg->user_id[0] = '\0';
    cfg->user_name[0] = '\0';
    config_save(cfg);
}
