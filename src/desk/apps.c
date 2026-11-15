#include "desk.h"
App *APPS[] = { &app_terminal, &app_files, &app_browser, &app_store, &app_monitor, &app_network, &app_settings, &app_about };
int NAPPS = sizeof APPS / sizeof APPS[0];
