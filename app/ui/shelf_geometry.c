// Package dimensions and spine identity used by the room presentation.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "shelf_geometry.h"
#include "cold_code.h"
// A PlayStation product code in the file name ("Game [SLUS-00594]", "SLPS_012.34",
// "SCES 01234"): out "SLUS-00594", false when there is none. No file is read.
static PSX3D_COLD bool serial_of(const char *path, char out[12])
{
	static const char *const prefix[] = { "SLUS", "SCUS", "SLES", "SCES", "SCED", "SLPS", "SLPM", "SCPS", "SCPM", "SIPS", "SLKA" };
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	for (const char *p = name; *p; p++) {
		if (p > name && ((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= '0' && p[-1] <= '9'))) continue;
		size_t k = 0;
		while (k < sizeof(prefix) / sizeof(prefix[0]) && strncasecmp(p, prefix[k], 4)) k++;
		if (k == sizeof(prefix) / sizeof(prefix[0])) continue;
		const char *q = p + 4;
		if (*q == '-' || *q == '_' || *q == ' ') q++;
		char d[6];
		int n = 0;
		bool dot = false;
		for (; n < 5; q++) {
			if (*q >= '0' && *q <= '9') d[n++] = *q;
			else if (*q == '.' && n == 3 && !dot) dot = true;
			else break;
		}
		if (n < 5 || (*q >= '0' && *q <= '9')) continue;
		d[5] = 0;
		snprintf(out, 12, "%.4s-%s", prefix[k], d);
		return true;
	}
	return false;
}
// The region of a product code: SLUS/SCUS North America, SLES/SCES/SCED PAL, the rest Japan (and Asia).
static PSX3D_COLD u8 serial_region(const char *serial)
{
	if (!strncmp(serial, "SLUS", 4) || !strncmp(serial, "SCUS", 4)) return SHELF_REGION_US;
	if (!strncmp(serial, "SLES", 4) || !strncmp(serial, "SCES", 4) || !strncmp(serial, "SCED", 4)) return SHELF_REGION_PAL;
	return SHELF_REGION_JP;
}

// The release region from No-Intro/Redump style tags in the file name: "(USA)",
// "(Europe)", "(Japan)", "(E)", "(J)"... North America when there is none.
static PSX3D_COLD u8 region_of(const char *path)
{
	static const char *const pal[] = { "Europe", "UK", "Germany", "France", "Spain", "Italy", "Australia", "Netherlands", "Sweden", "PAL" };
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	for (const char *p = strchr(name, '('); p; p = strchr(p + 1, '(')) {
		const char *q = p + 1;
		if (!strncmp(q, "USA", 3) || !strncmp(q, "U)", 2) || !strncmp(q, "NTSC-U", 6)) return SHELF_REGION_US;
		if (!strncmp(q, "Japan", 5) || !strncmp(q, "J)", 2) || !strncmp(q, "NTSC-J", 6)) return SHELF_REGION_JP;
		if (!strncmp(q, "E)", 2)) return SHELF_REGION_PAL;
		for (size_t i = 0; i < sizeof(pal) / sizeof(pal[0]); i++)
			if (!strncmp(q, pal[i], strlen(pal[i]))) return SHELF_REGION_PAL;
	}
	return SHELF_REGION_US;
}

// More than one disc in the case: the library's disc count (the system's own: its playlists
// too), or a "(Disc N)" / "(Disk N)" tag (a set the library could not group). Names only.
static PSX3D_COLD bool multi_disc(const bb_catalog_item *item)
{
	if (item->discs > 1) return true;
	const char *name = strrchr(item->path, '/');
	name = name ? name + 1 : item->path;
	for (const char *p = strchr(name, '('); p; p = strchr(p + 1, '('))
		if ((!strncasecmp(p + 1, "Disc ", 5) || !strncasecmp(p + 1, "Disk ", 5)) && p[6] >= '1' && p[6] <= '9') return true;
	return false;
}
// A jewel case's thickness: two or more discs 25 mm (any region), else PAL 17, else 10.5.
static PSX3D_COLD float case_depth(const shelf_box *b)
{
	if (b->discs > 1) return SHELF_CASE_MULTI;
	return b->region == SHELF_REGION_PAL ? SHELF_CASE_PAL : SHELF_CASE_SINGLE;
}

PSX3D_COLD void shelf_geometry_fill(shelf_box *b, const bb_system_t *s, int row, const bb_catalog_item *item, int number)
{
	static const bb_package plain = { .width = 130, .height = 180, .depth = 16, .style = BB_PACKAGE_CARTON,
		.spine_head = 0xff2a2a2a, .band = 0xff2a2a2a };
	const bb_package *p = s->package ? s->package : &plain;
	bb_library_fill(b, s, row, item);
 b->w=p->width; b->h=p->height; b->t=p->depth;
	b->region = region_of(item->path);
	if (p->style == BB_PACKAGE_CASE) {   // a jewel case: the product code's region, the case for its discs
		if (serial_of(item->path, b->serial)) b->region = serial_region(b->serial);
		if (multi_disc(item) && b->discs < 2) b->discs = 2;
		b->t = case_depth(b);
	}
	snprintf(b->tag, sizeof(b->tag), "%s\xc2\xb7%02d", s->short_name ? s->short_name : s->id, number);
}
