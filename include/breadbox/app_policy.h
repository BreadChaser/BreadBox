// Compile-time app policy. No dispatch in PS1's per-frame path.
// BreadBox is the only product; PSX3D (title 000400000C3D3D00) is frozen at v0.5.2.
#ifndef APP_POLICY_H
#define APP_POLICY_H
#include "../../apps/breadbox/version.h"
#define APP_NAME "BreadBox"
#define APP_SLUG "BreadBox"
#define APP_WORDMARK_LEFT "Bread"
#define APP_WORDMARK_RIGHT "Box"
#define APP_WORDMARK_WIDTH 62
#define APP_TITLE_ID 0x000400000B0B0B00ULL
// BreadBox may import the user-selected library root from the old app.
#define APP_LEGACY_PSX3D_ROOT "sdmc:/3ds/PSX3D"
#define APP_SD_PATH "/3ds/" APP_SLUG
#define APP_SD_ROOT "sdmc:" APP_SD_PATH
#define APP_CIA APP_SLUG ".cia"
#define APP_RELEASE_PAGE "github.com/BreadChaser/" APP_SLUG "/releases"
#define APP_RELEASE_URL "https://" APP_RELEASE_PAGE "/latest/download/" APP_CIA
// The PS1 preset pack is shared, but lives under BreadBox's own SD root. The
// URL stays on the frozen PSX3D release page until the pack is hosted elsewhere.
#define APP_PRESETS_URL "https://github.com/BreadChaser/PSX3D/releases/download/presets/"

// Save-state slots the player sees. PSX3D: ten, shown 0..9. BreadBox: twelve, shown 1..12 (bb_slots.h);
// the number in the code (card index, PS1 file .stN) stays 0-based, SLOT_NO() is what is written on screen.
#include "bb_slots.h"
#define SLOT_COUNT BB_SLOTS
#define SLOT_RANGE "0..11 (the number shown minus 1)"
#define SLOT_NO(n) bb_slot_shown(n)
#endif
