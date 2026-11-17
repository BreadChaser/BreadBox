// Play together and Share BreadBox screens from the main menu (mp_ui.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>

// mp_ui.c - Play together and Share BreadBox screens, opened from the main menu
void mp_hub_run(void);
void mp_host_run(void);   // main menu > Play together > Host (the developer interface notes)
void mp_join_run(void);   // main menu > Play together > Join
// after mp_hub_run: a game to start (Host a game), path filled
bool mp_launch_request(char *path, size_t len);
void share_run(void);
