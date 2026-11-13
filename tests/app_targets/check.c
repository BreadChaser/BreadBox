// Exercise the real backup writer with BreadBox's root, without an SDK.
#include "app_policy.h"
#include "history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void log_printf(const char *fmt, ...) { (void)fmt; }
int main(void)
{
	mkdir("sdmc:", 0700); mkdir("sdmc:/3ds", 0700);
	mkdir("sdmc:/3ds/PSX3D", 0700); mkdir("sdmc:/3ds/BreadBox", 0700);
	assert(APP_TITLE_ID == 0x000400000B0B0B00ULL);
	assert(!strcmp(APP_SD_ROOT, "sdmc:/3ds/BreadBox"));
	// The frozen PSX3D folder is only ever read (import), never written.
	const char *other = "sdmc:/3ds/PSX3D/backups";
	char next[512];
	assert(history_next("Fixture", HIST_EXIT, ".sta", 123456, next, sizeof(next)));
	assert(!strncmp(next, APP_SD_ROOT "/backups/", strlen(APP_SD_ROOT "/backups/")));
	assert(history_add("Fixture", HIST_EXIT, ".sta", "state", 5));
	assert(access(APP_SD_ROOT "/backups/Fixture", F_OK) == 0);
	assert(access(other, F_OK) != 0);
	puts(APP_NAME " backup isolation PASS");
}
