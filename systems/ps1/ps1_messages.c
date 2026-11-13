#include "app_log.h"
#include "cold_code.h"
#include "emu.h"
#include "ps1_messages.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PSX3D_COLD void ps1_core_message(const char *fmt, ...)
{
	char text[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	/* HLE is supported, and automatic game fixes need no player action. */
	if (!strncmp(text, "Enabled hacks:", sizeof("Enabled hacks:") - 1) ||
	    !strcmp(text, "No PlayStation BIOS file found - add for better compatibility") ||
	    !strcmp(text, "BIOS set to 'hle'")) {
		log_printf("core: %s\n", text);
		return;
	}
	emu_osd(text);
}
