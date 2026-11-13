#include "app_policy.h"
/* About / Credits and the install help: pages of text in the shared dialog (ui_confirm_choice,
 * the same card and button bar as every question), opened from Settings and from Share. A: next
 * page, B: previous page (the first page: close). HOME > Close ends the dialog; the callers then
 * stop (no more drawing once aptMainLoop() is false). */
#include <stdio.h>
#include "ui.h"
#include "breadbox_credits.h"

#define COLD __attribute__((noinline,section(".psx3d_cold.text")))   /* menus only: the per-frame code keeps its place */

/* Every page fits the dialog whole (7 lines; tests/ui_fit/strings.py), broken at sentence ends: one page
 * counter in the title and no half-sentence second pages (deep UI test F12 / Old P1). Full notices: CREDITS
 * and tools/art/CREDITS.md in the source. */
static const char *const credits_pages[] = {
 "BreadBox " PSX3D_VERSION " by BreadChaser. Free software, GPL-2.0-or-later. No Sony or Nintendo system files and no games are included.",
 "PlayStation: PCSX-ReARMed by notaz, Exophase, the PCSX team and libretro contributors. Game Boy Advance: gpSP by Exophase and libretro contributors.",
 "System files: the Game Boy Advance uses the open replacement by Normmatt and the VBA / VBA-M team (GPLv2), the PlayStation the one built into PCSX-ReARMed.",
 "Libraries: libctru, citro2d and citro3d by devkitPro; libchdr, LZMA, libcurl, libpng, mbedTLS and zlib, each under its own license. DSP firmware dump by zoogie (DSP1).",
 "Fonts: Michroma by Vernon Adams and Archivo Black by the Archivo Black Project Authors / Omnibus-Type, both SIL OFL 1.1.",
 "Menu icons by Dreamstale from Flaticon (Flaticon license, not GPL). BreadClip logo and artwork by BreadChaser, GPL-2.0-or-later.",
 "Hand model: 'Hand (low poly)' by scribbletoad (Sketchfab), CC BY 4.0, creativecommons.org /licenses/by/4.0. No endorsement implied.",
 "Modified by BreadChaser: scaled, rigged and remapped, with a cuff, sleeve and mittens added. Palm artwork by Algot Runeman and public domain silhouettes, CC0.",
 "Cheats and cover names: libretro contributors, CC BY-SA 4.0. The room's models, textures and lighting were made for BreadBox in Blender, GPL-2.0-or-later.",
 "Menu sounds: paper recordings by Joseph Sardin (BigSoundBank), Kenney and OwlishMedia, CC0 1.0. The launch sound and music are made by the project.",
 "The PlayStation logo is downloaded when needed (a Sony trademark). Not affiliated with Sony, Nintendo or libretro.",
};

static const char *const install_pages[] = {
 "To share BreadBox with a friend, open FBI on their 3DS, then Remote Install > Scan QR Code, and scan a code from Share.",
 "Official release needs the internet. From this 3DS needs BreadBox.cia on this 3DS's SD card (in the cias folder, for example) and both 3DS on the same Wi-Fi.",
};

static void pages_run(const char *title, const char *const *pages, unsigned n)
{
 char head[48];
 for(unsigned page=0;page<n;) {
  if(n>1)snprintf(head,sizeof(head),"%s %u/%u",title,page+1,n);
  else snprintf(head,sizeof(head),"%s",title);
  bool next=ui_confirm_choice(head,pages[page],page+1==n?"A  Done":"A  Next",page?"B  Back":"B  Close",false);
  if(!aptMainLoop())return;   /* HOME > Close: nothing more to draw */
  if(next)page++;
  else if(page)page--;
  else return;
 }
}

COLD void bb_credits_run(void) {pages_run("About / Credits",credits_pages,sizeof(credits_pages)/sizeof(credits_pages[0]));}
COLD void bb_install_help_run(void) {pages_run("How to install",install_pages,sizeof(install_pages)/sizeof(install_pages[0]));}
