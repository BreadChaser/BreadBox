#include "bb_disc.h"
#include "../../core/deps/libchdr/include/libchdr/chd.h"
/* On the 3DS the PS1 core builds libchdr with USE_LIBRETRO_VFS (its Makefile.libretro,
 * platform ctr), and build/3ds/Makefile builds this file the same way: chd.h then maps
 * FILE/fopen/fclose to the core's RFILE/rfopen/rfclose, the only handle its
 * chd_open_file can read. A C FILE there made every CHD "invalid" (issue #1). */
#include "app_log.h"
#include <stdlib.h>
#include <string.h>

/* Metadata is optional: the shared core currently localizes this entry point.
 * A weak reference must not pull more code into its validated layout. */
#pragma weak chd_get_metadata

#define MAX_HUNK (256u * 1024u)

static bool contains(const unsigned char *p, size_t n, const char *signature)
{
    size_t len = strlen(signature);
    for (size_t i = 0; i + len <= n; ++i)
        if (!memcmp(p + i, signature, len)) return true;
    return false;
}

static bool gdrom(chd_file *chd)
{
    char meta[1];
    if (!chd_get_metadata) return false;
    return chd_get_metadata(chd, GDROM_OLD_METADATA_TAG, 0, meta, sizeof(meta),
                            NULL, NULL, NULL) == CHDERR_NONE ||
           chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, 0, meta, sizeof(meta),
                            NULL, NULL, NULL) == CHDERR_NONE;
}

/* The ISO primary volume descriptor's identity at sector 16, read from hunk zero
 * plus at most two more hunks. */
static bb_disc_kind content(chd_file *chd, const chd_header *h, unsigned char *hunk)
{
    const uint32_t unit = h->unitbytes, size = h->hunkbytes;
    if (chd_read(chd, 0, hunk) != CHDERR_NONE) return BB_DISC_UNREAD;
    size_t first = size;
    if (first > (size_t)16 * unit) first = (size_t)16 * unit;
    /* Reject other CD platforms even if an incidental Sony string is present. */
    if (contains(hunk, first, "SEGADISCSYSTEM") ||
        contains(hunk, first, "SEGA SEGASATURN") ||
        contains(hunk, first, "SEGA SEGAKATANA")) return BB_DISC_OTHER;
    bool license = contains(hunk, first, "Sony Computer Entertainment");

    /* ISO sector 16, not hunk 16. A 64-byte window covers cooked data and
     * both raw Mode 1 (+16) and Mode 2 (+24) headers, including subchannel CDs.
     * At most two hunks cover it; no scan or decompression of the whole disc. */
    unsigned char pvd[64];
    uint64_t pos = (uint64_t)16 * unit;
    size_t copied = 0;
    uint32_t cached = 0;
    while (copied < sizeof(pvd)) {
        uint32_t index = (uint32_t)(pos / size);
        size_t offset = (size_t)(pos % size);
        size_t count = size - offset;
        if (count > sizeof(pvd) - copied) count = sizeof(pvd) - copied;
        if (pos + count > h->logicalbytes || index >= h->totalhunks) return BB_DISC_OTHER;
        if (index != cached) {
            if (chd_read(chd, index, hunk) != CHDERR_NONE) return BB_DISC_UNREAD;
            cached = index;
        }
        memcpy(pvd + copied, hunk + offset, count);
        copied += count;
        pos += count;
    }
    for (unsigned off = unit == 2048 ? 0 : 16; off <= (unit == 2048 ? 0u : 24u); off += 8) {
        const unsigned char *p = pvd + off;
        if (p[0] == 1 && !memcmp(p + 1, "CD001", 5) && p[6] == 1 &&
            !memcmp(p + 8, "PLAYSTATION", 11)) {
            bool exact = true;
            for (unsigned i = 19; i < 40; ++i)
                if (p[i] != ' ' && p[i] != 0) exact = false;
            if (exact) return BB_DISC_PS1;
        }
    }
    return license ? BB_DISC_PS1 : BB_DISC_OTHER;
}

static bb_disc_kind classify(chd_file *chd)
{
    const chd_header *h = chd_get_header(chd);
    if (!h) return BB_DISC_UNREAD;
    if (gdrom(chd)) return BB_DISC_OTHER;
    const uint32_t unit = h->unitbytes, size = h->hunkbytes;
    /* Not CD sectors (a hard disk, for one) or too short to hold sector 16:
     * positively not a PS1 disc. */
    if ((unit != 2048 && unit != 2352 && unit != 2448) ||
        h->logicalbytes < (uint64_t)17 * unit)
        return BB_DISC_OTHER;
    /* Geometry this bounded check will not read: the core may still play it. */
    if (size < unit || size > MAX_HUNK || !h->totalhunks ||
        h->logicalbytes > (uint64_t)h->totalhunks * size)
        return BB_DISC_UNREAD;
    unsigned char *hunk = malloc(size);
    if (!hunk) return BB_DISC_UNREAD;
    bb_disc_kind kind = content(chd, h, hunk);
    free(hunk);
    return kind;
}

bb_disc_kind bb_disc_chd_kind(const char *path)
{
    if (!path || !*path) return BB_DISC_OTHER;
    FILE *file = fopen(path, "rb");
    if (!file) {
        log_printf("disc: %s cannot be opened; listed as PS1\n", path);
        return BB_DISC_UNREAD;
    }
    chd_file *chd = NULL;
    chd_error err = chd_open_file(file, CHD_OPEN_READ, NULL, &chd);
    if (err != CHDERR_NONE) {
        fclose(file);
        log_printf("disc: %s is not a readable CHD here (error %d); listed as PS1\n", path, (int)err);
        return BB_DISC_UNREAD;
    }
    bb_disc_kind kind = classify(chd);
    chd_close(chd);
    fclose(file);
    if (kind == BB_DISC_UNREAD) log_printf("disc: %s could not be checked; listed as PS1\n", path);
    return kind;
}
