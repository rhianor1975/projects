// ui.h - terminal UI: raw mode, screens, navigation.
#ifndef MEDUSA_UI_H
#define MEDUSA_UI_H

#include "config.h"

void ui_setup_terminal(void);
void ui_reset_terminal(void);

// Runs the full screen loop (login -> libraries -> items -> ...) until the
// user quits. Persists/reloads session state into *cfg via config_save().
void ui_run(Config* cfg);

#endif
