#pragma once
#include <stdbool.h>

#define BB_SEEN_VERSION_CAP 64
extern char config_seen_version[BB_SEEN_VERSION_CAP];
bool bb_whatsnew_changed(bool had_data, const char *seen, const char *version, const char *text);
void bb_whatsnew_boot(bool had_config);
bool bb_whatsnew_take(void);
void bb_whatsnew_prepare(void);
void bb_whatsnew_exit(void);
void bb_whatsnew_open(void);
const char *bb_whatsnew_label(void);
void bb_whatsnew_room(int screen, bool ready, void (*present)(void (*)(void *)));
