#pragma once
#include <stdbool.h>

static inline bool system_3d_screen_model(unsigned model, unsigned old_2ds, unsigned new_2ds)
{
	/* Use the caller's libctru model constants instead of copying SDK IDs here. */
	return model != old_2ds && model != new_2ds;
}

static inline bool system_ps1_3d_available(bool screen, bool new_model)
{
	return screen && new_model;
}

static inline bool system_gba_3d_available(bool screen, bool new_model, bool ps1_3d_setting)
{
	/* GBA depth follows its physical slider, independently of PS1 settings. */
	(void)new_model; (void)ps1_3d_setting;
	return screen;
}
