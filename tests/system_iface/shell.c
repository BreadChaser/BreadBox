/* What the shell may know about systems: the registry, nothing else.
 * Systems page, counts, settings pages and dispatch iterate bb_systems[]. */
#include <stdio.h>
#include <string.h>
#include "bb_registry.h"

int main(void)
{
	int bad = 0;
	for (unsigned i = 0; i < bb_system_count; i++) {
		const bb_system_t *s = bb_systems[i];
		if (s->abi != BB_SYSTEM_ABI || !s->id || !s->name || !s->run || !s->extensions) {
			printf("FAIL %u incomplete\n", i); bad = 1; continue;
		}
		for (unsigned r = 0; r < s->nsettings; r++)
			if (!s->settings[r].key || !s->settings[r].label) { printf("FAIL %s row %u\n", s->id, r); bad = 1; }
		if (s->budget[BB_OLD].keep_kib || s->budget[BB_NEW].keep_kib) { printf("FAIL %s keeps memory\n", s->id); bad = 1; }
		printf("system %-4s %-20s settings=%u\n", s->id, s->name, s->nsettings);
	}
	if (!bb_system_by_id("snes") || bb_system_by_id("n64")) bad = 1;
	puts(bad ? "FAIL" : "PASS: registry iterated with no system names in shared code");
	return bad;
}
