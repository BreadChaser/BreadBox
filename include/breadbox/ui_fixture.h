#pragma once
#include <stdbool.h>
#ifdef BB_UI_FIXTURES
bool ui_fixture_command(const char *command,const char *args);
const char *ui_fixture_notice(void);
int ui_fixture_notes(void);
bool ui_fixture_problem(void);
#else
static inline bool ui_fixture_command(const char *command,const char *args){(void)command;(void)args;return false;}
static inline const char *ui_fixture_notice(void){return 0;}
static inline int ui_fixture_notes(void){return 0;}
static inline bool ui_fixture_problem(void){return false;}
#endif
