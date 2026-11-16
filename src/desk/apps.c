#include "desk.h"
/* The first seven apps are pinned to the panel; the others appear there while running. */
App *APPS[] = { &app_terminal, &app_files, &app_browser, &app_store, &app_monitor, &app_network, &app_settings, &app_notes, &app_calc, &app_viewer, &app_clock, &app_about };
int NAPPS = sizeof APPS / sizeof APPS[0];
