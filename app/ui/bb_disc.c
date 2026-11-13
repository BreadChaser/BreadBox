#include "bb_disc.h"
#include "../../core/deps/libchdr/include/libchdr/chd.h"
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

bool bb_disc_is_ps1_chd(const char *path)
{
    chd_file *chd = NULL;
    unsigned char *hunk = NULL;
    bool result = false;
    if (!path || !*path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (chd_open_file(file, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE) {
        fclose(file); return false;
    }
    const chd_header *h = chd_get_header(chd);
    if (!h || gdrom(chd)) goto done;
    const uint32_t unit = h->unitbytes, size = h->hunkbytes;
    if ((unit != 2048 && unit != 2352 && unit != 2448) ||
        size < unit || size > MAX_HUNK || !h->totalhunks ||
        h->logicalbytes < (uint64_t)17 * unit ||
        h->logicalbytes > (uint64_t)h->totalhunks * size)
        goto done;
    hunk = malloc(size);
    if (!hunk || chd_read(chd, 0, hunk) != CHDERR_NONE) goto done;
    size_t first = size;
    if (first > (size_t)16 * unit) first = (size_t)16 * unit;
    /* Reject other CD platforms even if an incidental Sony string is present. */
    if (contains(hunk, first, "SEGADISCSYSTEM") ||
        contains(hunk, first, "SEGA SEGASATURN") ||
        contains(hunk, first, "SEGA SEGAKATANA")) goto done;
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
        if (pos + count > h->logicalbytes || index >= h->totalhunks) goto done;
        if (index != cached) {
            if (chd_read(chd, index, hunk) != CHDERR_NONE) goto done;
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
            result = true;
            for (unsigned i = 19; i < 40; ++i)
                if (p[i] != ' ' && p[i] != 0) result = false;
            if (!result) continue;
            break;
        }
    }
    result = result || license;
done:
    free(hunk);
    chd_close(chd);
    fclose(file);
    return result;
}
