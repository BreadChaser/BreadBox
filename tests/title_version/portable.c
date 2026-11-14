#include <assert.h>
#include <string.h>
#include "../../include/breadbox/title_version.h"

int main(void)
{
    unsigned major, minor, patch;
    for (unsigned value = 0; value <= 65536; ++value) {
        major = minor = patch = 999;
        int valid = bb_title_version_decode(value, &major, &minor, &patch);
        assert(valid == (value >= 25615 && value <= 65535 && (value & 15) == 15));
        if (valid)
            assert(bb_title_version_encode(major, minor, patch) == value);
        else
            assert(major == 999 && minor == 999 && patch == 999);
    }
    assert(bb_title_version_encode(0, 9, 0) == 25615);
    assert(bb_title_version_encode(0, 9, 1) == 25631);
    assert(bb_title_version_encode(1, 0, 0) == 32783);
    assert(bb_title_version_encode(3, 0, 0) == BB_TITLE_VERSION_INVALID);
    assert(bb_title_version_encode(0, 16, 0) == BB_TITLE_VERSION_INVALID);
    assert(bb_title_version_encode(0, 0, 64) == BB_TITLE_VERSION_INVALID);
    assert(bb_title_version_encode(~0u, 0, 0) == BB_TITLE_VERSION_INVALID);
    char text[32];
    assert(!strcmp(bb_title_version_text(text, sizeof(text), 25615), "0.9.0"));
    assert(!strcmp(bb_title_version_text(text, sizeof(text), 25631), "0.9.1"));
    assert(!strcmp(bb_title_version_text(text, sizeof(text), 32783), "1.0.0"));
    for (unsigned micro = 0; micro < 15; ++micro) {
        char want[32];
        snprintf(want, sizeof(want), "test build %u", 25616 + micro);
        assert(!strcmp(bb_title_version_text(text, sizeof(text), 25616 + micro), want));
    }
    assert(!strcmp(bb_title_version_text(text, sizeof(text), 16642), "test build 16642"));
    return 0;
}
