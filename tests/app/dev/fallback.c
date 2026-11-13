#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../include/breadbox/devswitch_source.h"

static FILE *text_file(const char *text)
{
	FILE *f = tmpfile(); assert(f);
	fputs(text, f); rewind(f); return f;
}
int main(void)
{
	int remote = 0; char token[96] = "";
	assert(!devswitch_open_config("missing-primary", "missing-fallback", &remote));
	assert(!remote);
	FILE *f = text_file("stats oldmode game=Crash token=fake-credential remote nosound");
	devswitch_parse_fallback(f, &remote, token, sizeof(token)); fclose(f);
	assert(remote && !strcmp(token, "fake-credential"));
	remote = 0; token[0] = 0;
	f = text_file("stats game=Other"); devswitch_parse_fallback(f, &remote, token, sizeof(token)); fclose(f);
	assert(!remote && !token[0]);
	FILE *primary = fopen("breadbox-switches", "w"); assert(primary);
	fputs("nosound remote token=primary-fake", primary); fclose(primary);
	FILE *legacy = fopen("psx3d-switches", "w"); assert(legacy);
	fputs("remote token=fallback-fake", legacy); fclose(legacy);
	int used_fallback = 1;
	f = devswitch_open_config("breadbox-switches", "psx3d-switches", &used_fallback);
	assert(f && !used_fallback);
	remote = 0; token[0] = 0;
	/* Primary contents go through the ordinary parser; the legacy token is never read. */
	char word[96]; while (fscanf(f, "%95s", word) == 1) {
		if (!strcmp(word,"remote")) remote=1;
		else if (!strncmp(word,"token=",6)) snprintf(token,sizeof(token),"%s",word+6);
	}
	fclose(f);
	assert(remote && !strcmp(token,"primary-fake"));
	used_fallback = 0;
	f = devswitch_open_config("missing-primary", "psx3d-switches", &used_fallback);
	assert(f && used_fallback);
	remote = 0; token[0] = 0;
	devswitch_parse_fallback(f, &remote, token, sizeof(token)); fclose(f);
	assert(remote && !strcmp(token,"fallback-fake"));
	puts("fallback allowlist, missing files and primary precedence: PASS");
}
