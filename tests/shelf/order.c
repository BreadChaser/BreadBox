// Display order of the systems (app/ui/bb_order.c): release date newest
// first, ties by name, empty systems hidden on request. The registry array
// itself keeps its order: its indexes are persisted.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bb_registry.h"

#define SYS(var, ident, label, y, m) static const bb_system_t var = { .id = ident, .name = label, .short_name = ident, .release_year = y, .release_month = m }
SYS(ps1, "ps1", "PlayStation", 1994, 12);
SYS(gba, "gba", "Game Boy Advance", 2001, 3);
SYS(nes, "nes", "Nintendo Entertainment System", 1983, 7);
SYS(snes, "snes", "Super Nintendo", 1990, 11);
SYS(nds, "nds", "Nintendo DS", 2004, 11);
SYS(empty, "new", "Newest Without Games", 2017, 3);
SYS(tie_b, "tb", "Zeta Same Month", 1990, 11);
SYS(tie_a, "ta", "alpha Same Month", 1990, 11);
SYS(undated, "und", "Undated", 0, 0);
const bb_system_t *const bb_systems[] = { &ps1, &gba, &nes, &snes, &nds, &empty, &tie_b, &tie_a, &undated };
const unsigned bb_system_count = sizeof(bb_systems) / sizeof(*bb_systems);
int bb_system_library_count(const bb_system_t *s) { return s == &empty ? 0 : 5; }

int main(void)
{
	const bb_system_t *o[BB_MAX_SYSTEMS];
	unsigned n = bb_system_display_order(o, BB_MAX_SYSTEMS, true);   // shelf and Systems page: no empty systems
	const bb_system_t *want[] = { &nds, &gba, &ps1, &tie_a, &snes, &tie_b, &nes, &undated };
	assert(n == sizeof(want) / sizeof(*want));
	for (unsigned i = 0; i < n; i++) assert(o[i] == want[i]);
	n = bb_system_display_order(o, BB_MAX_SYSTEMS, false);   // counts line lists every system
	assert(n == bb_system_count && o[0] == &empty && o[1] == &nds && o[n - 1] == &undated);
	assert(bb_system_display_order(o, 3, true) == 3);   // never writes past the caller's array
	assert(bb_systems[0] == &ps1 && bb_systems[1] == &gba && bb_systems[2] == &nes);   // the registry order is untouched
	puts("system display order: date newest first, name ties, empty hidden, registry untouched: PASS");
	return 0;
}
