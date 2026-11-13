#include "bb_disc.h"
#include "../../core/deps/libchdr/include/libchdr/chd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static chd_header header;
static unsigned char image[1024 * 1024];
static int opens, closes, reads, fail_read, fail_open, null_header;
static uint32_t read_at[4], gdtag;
static int dummy;

chd_error chd_open_file(FILE *path, int mode, chd_file *parent, chd_file **out)
{
    assert(path && mode == CHD_OPEN_READ && !parent);
    ++opens;
    if (fail_open) return CHDERR_FILE_NOT_FOUND;
    *out = (chd_file *)&dummy;
    return CHDERR_NONE;
}
void chd_close(chd_file *chd) { assert(chd == (chd_file *)&dummy); ++closes; }
const chd_header *chd_get_header(chd_file *chd)
{
    assert(chd == (chd_file *)&dummy);
    return null_header ? NULL : &header;
}
chd_error chd_read(chd_file *chd, uint32_t index, void *out)
{
    assert(chd == (chd_file *)&dummy);
    assert(reads < 3); /* hunk zero plus at most two PVD hunks */
    assert(index < header.totalhunks);
    read_at[reads++] = index;
    if (reads == fail_read) return CHDERR_READ_ERROR;
    assert((uint64_t)(index + 1) * header.hunkbytes <= sizeof(image));
    memcpy(out, image + (size_t)index * header.hunkbytes, header.hunkbytes);
    return CHDERR_NONE;
}
#ifndef NO_METADATA
chd_error chd_get_metadata(chd_file *chd, uint32_t tag, uint32_t index,
    void *out, uint32_t len, uint32_t *resultlen, uint32_t *resulttag, uint8_t *flags)
{
    assert(chd == (chd_file *)&dummy && index == 0 && out && len == 1);
    (void)resultlen; (void)resulttag; (void)flags;
    return gdtag && tag == gdtag ? CHDERR_NONE : CHDERR_METADATA_NOT_FOUND;
}
#endif

static void reset(uint32_t unit, uint32_t hunk)
{
    memset(&header, 0, sizeof(header));
    memset(image, 0, sizeof(image));
    header.unitbytes = unit;
    header.hunkbytes = hunk;
    header.logicalbytes = (uint64_t)128 * unit;
    header.totalhunks = (uint32_t)((header.logicalbytes + hunk - 1) / hunk);
    opens = closes = reads = fail_read = fail_open = null_header = 0;
    gdtag = 0;
}
static void pvd(unsigned offset)
{
    unsigned char *p = image + 16 * header.unitbytes + offset;
    p[0] = 1;
    memcpy(p + 1, "CD001", 5);
    p[6] = 1;
    memset(p + 8, ' ', 32);
    memcpy(p + 8, "PLAYSTATION", 11);
}
static void expect(bool expected)
{
    assert(bb_disc_is_ps1_chd("game.chd") == expected);
    assert(opens == 1 && closes == !fail_open);
}

int main(void)
{
    FILE *fixture=fopen("game.chd","wb"); assert(fixture); fclose(fixture);
    const unsigned units[] = {2048, 2352, 2448};
    for (unsigned u = 0; u < 3; ++u) {
        unsigned unit = units[u];
        for (unsigned mode = 0; mode < (unit == 2048 ? 1u : 2u); ++mode) {
            unsigned off = unit == 2048 ? 0 : (mode ? 24 : 16);
            reset(unit, unit * 8); pvd(off); expect(true);
            assert(reads == 2 && read_at[0] == 0 && read_at[1] == 2);
            /* A non-unit-aligned hunk deliberately splits the PVD window. */
            reset(unit, (16 * unit + 32) / 2); pvd(off); expect(true);
            assert(reads == 3 && read_at[1] == 1 && read_at[2] == 2);
            reset(unit, 256 * 1024); pvd(off); expect(true);
            assert(reads == 1);
            reset(unit, unit); pvd(off); expect(true);
            assert(reads == 2 && read_at[1] == 16);
        }
    }
    reset(2448, 19584); pvd(24); expect(true); /* typical CD CHD */
    reset(2048, 16384); expect(false);
    reset(2048, 16384); pvd(0); image[16 * 2048 + 1] = 'X'; expect(false);
    reset(2048, 16384); pvd(0); image[16 * 2048 + 6] = 2; expect(false);
    reset(2048, 16384); pvd(0); image[16 * 2048 + 19] = 'X'; expect(false);
    reset(2048, 16384); pvd(0);
    memcpy(image + 16 * 2048 + 8, "PSP GAME   ", 11); expect(false);
    reset(2048, 16384); pvd(0);
    memcpy(image + 16 * 2048 + 8, "OTHER      ", 11); expect(false);
    reset(2048, 16384);
    memcpy(image + 100, "Sony Computer Entertainment", 27); expect(true);
    reset(2048, 16384);
    memcpy(image + 100, "Sony Computer EntertainmenX", 27); expect(false);
    /* The license search must stop before sector 16 even with a large hunk. */
    reset(2048, 256 * 1024);
    memcpy(image + 16 * 2048 + 100, "Sony Computer Entertainment", 27); expect(false);
    /* A signature in hunk 16 is irrelevant when ISO sector 16 is in hunk 2. */
    reset(2048, 16384);
    memcpy(image + 16 * 16384, "\1CD001\1\0PLAYSTATION", 19); expect(false);
    reset(2352, 18816); pvd(24);
    memcpy(image + 24, "SEGA SEGAKATANA", 15); expect(false);
    reset(2048, 16384); pvd(0); fail_open = 1; expect(false); assert(!reads);
    reset(2048, 16384); null_header = 1; expect(false); assert(!reads);
    for (int f = 1; f <= 3; ++f) {
        reset(2048, (16 * 2048 + 32) / 2); pvd(0); fail_read = f;
        expect(false); assert(reads == f);
    }
#ifndef NO_METADATA
    const uint32_t tags[] = {GDROM_OLD_METADATA_TAG, GDROM_TRACK_METADATA_TAG};
    for (unsigned i = 0; i < 2; ++i) {
        reset(2448, 19584); pvd(24); gdtag = tags[i];
        expect(false); assert(!reads);
    }
#endif
    const uint32_t badsizes[] = {0, 1, 2047, 262145, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(badsizes) / sizeof(badsizes[0]); ++i) {
        reset(2048, 16384); header.hunkbytes = badsizes[i];
        expect(false); assert(!reads);
    }
    reset(512, 16384); expect(false); assert(!reads);
    reset(2048, 16384); header.totalhunks = 0; expect(false); assert(!reads);
    reset(2048, 16384); header.logicalbytes = 16 * 2048; expect(false); assert(!reads);
    reset(2048, 16384); header.logicalbytes = UINT64_MAX; expect(false); assert(!reads);
    reset(2048, 16384); header.totalhunks = 1; expect(false); assert(!reads);
    reset(2048, 16384); header.totalhunks = UINT32_MAX;
    header.logicalbytes = (uint64_t)UINT32_MAX * 16384 + 1;
    expect(false); assert(!reads); /* capacity arithmetic must not wrap */
    reset(2048, 16384);
    assert(!bb_disc_is_ps1_chd(NULL) && !bb_disc_is_ps1_chd(""));
    assert(!opens && !closes && !reads);
    puts("bb_disc: bounded CHD identification passed");
    return 0;
}
