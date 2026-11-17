#include <math.h>
#include "app_policy.h"
#include "app_main.h"
#include "app_config.h"
#include "ui_format.h"
#include "ui_style.h"
#include "platform_video.h"
#include "theme.h"
#include "ui.h"
#include "libui.h"
#include "bb_registry.h"
#include "dev_switches.h"
#include "cold_code.h"
#include "home_presentation.h"
static PSX3D_COLD u32 accent(int i,u8 a){return theme_alpha(theme()->accent[i&3],a);}
#include "components/home_presentation.inc"

PSX3D_COLD const char *home_empty_help(bool at_library)
{
 static char text[96];ui_empty_help(text,sizeof(text),g_cfg.games_dir,at_library);return text;
}
