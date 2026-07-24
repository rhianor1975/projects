// config.h - persisted client state: ~/.config/medusa/config
#ifndef MEDUSA_CONFIG_H
#define MEDUSA_CONFIG_H

#include <stdbool.h>

typedef struct {
    char server[128];
    char access_token[128];
    char user_id[64];
    char user_name[64];
    char device_id[64];
    // Preferred audio/subtitle language for playback, remembered across
    // runs so you don't have to re-pick every time you come back. Empty
    // means "no preference" (mpv/container default). pref_subtitle_lang
    // holds the literal string "none" for an explicit "no subtitles" choice
    // - safe since real language codes are always alphabetic.
    char pref_audio_lang[8];
    char pref_subtitle_lang[8];
} Config;

// Loads the config file if present. Always fills device_id (generating and
// persisting a new one on first run). Returns true if a saved session
// (server + token + user_id) was found.
bool config_load(Config* cfg);
void config_save(const Config* cfg);
void config_clear_session(Config* cfg); // keep device_id, drop server/token/user

#endif
