/* Cover names: a game's cover key -> the libretro-thumbnails file name, its URL
 * escape, its cache file and its hash (covers.c, covers_fetch.c). Host-testable:
 * no 3DS headers (tests/app/covers/match.py). */
#ifndef BB_COVERS_MATCH_H
#define BB_COVERS_MATCH_H
#include <stddef.h>
#include <stdint.h>
#include "app_paths.h"

#define COVER_DIR PSX3D_DIR "/covers"

void covers_thumb_name(const char *key, char *out, size_t len);
void covers_url_escape(const char *in, char *out, size_t len);
void covers_cache_path(const char *key, const char *ext, char *out, size_t len);
// FNV-1a of the key; inline in every caller, as before the split
static inline uint64_t covers_name_hash(const char *s)
{
	uint64_t h = 1469598103934665603ull;   // FNV-1a
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 1099511628211ull;
	return h;
}

#endif
