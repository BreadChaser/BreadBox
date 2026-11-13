#ifndef DEVSWITCH_SOURCE_H
#define DEVSWITCH_SOURCE_H
#include <stdio.h>
#include <string.h>

static inline FILE *devswitch_open_config(const char *primary, const char *fallback, int *used_fallback)
{
	*used_fallback = 0;
	FILE *f = fopen(primary, "r");
	if (f || !fallback) return f;
	f = fopen(fallback, "r");
	*used_fallback = f != NULL;
	return f;
}

/* Parse the deliberately restricted PSX3D-to-BreadBox developer fallback. */
static inline void devswitch_parse_fallback(FILE *f, int *remote, char *token, size_t token_size)
{
	char word[384];
	while (fscanf(f, "%383s", word) == 1) {
		if (!strcmp(word, "remote")) *remote = 1;
		else if (!strncmp(word, "token=", 6)) {
			snprintf(token, token_size, "%.95s", word + 6);
		}
	}
}
#endif
