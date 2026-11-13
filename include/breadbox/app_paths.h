// The app's folders on the SD card and the path buffer size
#pragma once
#include "app_policy.h"

// BB_VERSION and BB_BUILD come from apps/breadbox/version.h (app_policy.h).
#define PSX3D_DIR      APP_SD_ROOT
#define PSX3D_BIOS_DIR PSX3D_DIR "/bios"
#define PSX3D_SAVE_DIR PSX3D_DIR "/saves"
#define PSX3D_STATE_DIR PSX3D_DIR "/states"
#define PSX3D_CONFIG   PSX3D_DIR "/config.ini"
#define PSX3D_LOG      PSX3D_DIR "/log.txt"

#define PATH_LEN 512
