// The room's place cards before A: what Collection, Settings and Quit say at a glance (place_cards.h).
#include <stdio.h>
#include <string.h>
#include "place_cards.h"
#include "ui_format.h"
#include "cold_code.h"

static place_line *add(place_lines *out, const char *label, const char *value)
{
	if (out->n >= PLACE_LINES) return NULL;
	place_line *l = &out->line[out->n++];
	snprintf(l->label, sizeof(l->label), "%s", label ? label : "");
	snprintf(l->value, sizeof(l->value), "%s", value ? value : "");
	return l;
}

// label + gap + value on one ruled line (place_fit)
static bool fits(const place_fit *fit, const char *label, const char *value)
{
	if (!fit || !fit->width) return true;
	return fit->width(label, fit->ud) + (value && value[0] ? fit->gap + fit->width(value, fit->ud) : 0) <= fit->box;
}
// the label, or its short form when the line does not fit with it
static place_line *add_fit(place_lines *out, const place_fit *fit, const char *label, const char *short_label, const char *value)
{
	return add(out, short_label && !fits(fit, label, value) ? short_label : label, value);
}

PSX3D_COLD void place_count(char *out, size_t cap, long n)
{
	char digits[24];
	unsigned long v = n < 0 ? 0 : (unsigned long)n;
	int len = snprintf(digits, sizeof(digits), "%lu", v), o = 0;
	char grouped[32];
	for (int i = 0; i < len; i++) {
		if (i && (len - i) % 3 == 0) grouped[o++] = ',';
		grouped[o++] = digits[i];
	}
	grouped[o] = 0;
	snprintf(out, cap, "%s", grouped);
}

PSX3D_COLD int place_collection(place_lines *out, const place_fit *fit, const place_system *sys, int nsys, int favorites, bool known, bool *empty)
{
	out->n = 0;
	*empty = false;
	long total = 0;
	int with = 0;
	char n[24];
	for (int i = 0; i < nsys; i++) {
		if (sys[i].games <= 0 || !sys[i].name) continue;
		total += sys[i].games;
		if (with++ < PLACE_SYSTEMS_MAX) {
			place_count(n, sizeof(n), sys[i].games);
			add_fit(out, fit, sys[i].name, sys[i].short_name, n);
		}
	}
	if (!total) {   // nothing found: empty only once the scan has said so
		*empty = known;
		return 0;
	}
	if (with > 1) {
		place_count(n, sizeof(n), total);
		add(out, "Total", n);
	}
	if (favorites > 0) {
		place_count(n, sizeof(n), favorites);
		add(out, "Favorites", n);
	}
	return out->n;
}

PSX3D_COLD int place_settings(place_lines *out, const char *version, const char *update, const char *style)
{
	out->n = 0;
	if (version && version[0]) add(out, "Version", version);
	if (update && update[0]) add(out, update, NULL);
	if (style && style[0]) add(out, "Theme", style);
	return out->n;
}

// A name on up to two lines, broken at the last space that lets the first line fit (the second is cut when drawn if
// it is still too long); one line when it fits or has no space to break at.
static void add_name(place_lines *out, const place_fit *fit, const char *name)
{
	if (fits(fit, name, NULL) || out->n + 2 > PLACE_LINES) { add(out, name, NULL); return; }
	char first[sizeof(out->line[0].label)];
	const char *cut = NULL;
	for (const char *sp = strchr(name, ' '); sp; sp = strchr(sp + 1, ' ')) {
		snprintf(first, sizeof(first), "%.*s", (int)(sp - name), name);
		if (!fits(fit, first, NULL)) break;
		cut = sp;
	}
	if (!cut) { add(out, name, NULL); return; }
	snprintf(first, sizeof(first), "%.*s", (int)(cut - name), name);
	add(out, first, NULL);
	add(out, cut + 1, NULL);
}

PSX3D_COLD int place_quit(place_lines *out, const place_fit *fit, unsigned today_seconds, const char *last_game)
{
	out->n = 0;
	if (today_seconds) {
		char t[24];
		ui_format_time(t, sizeof(t), today_seconds < 86400u ? today_seconds : 86400u - 60u);   // a broken log never says more than a day
		add_fit(out, fit, "Played today", "Today", t);
	}
	if (last_game && last_game[0]) {   // the name on its own lines: beside a label it would be cut short
		add(out, "Last played", NULL);
		add_name(out, fit, last_game);
	}
	return out->n;
}
