/* breadbox_formats.c against a registry like the real one (PS1 + GBA, NES only with BB_EXPERIMENTAL_NES). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bb_registry.h"
#include "breadbox_formats.h"

static const char *const ps1_ext[]={".cue",".chd",".pbp",".iso",".img",".mdf",".m3u",".bin",NULL};
static const char *const gba_ext[]={".gba",".agb",".zip",NULL};
const bb_system_t bb_system_ps1={.abi=BB_SYSTEM_ABI,.id="ps1",.name="PlayStation",.short_name="PS1",.release_year=1994,.release_month=12,.extensions=ps1_ext};
const bb_system_t bb_system_gba={.abi=BB_SYSTEM_ABI,.id="gba",.name="Game Boy Advance",.short_name="GBA",.release_year=2001,.release_month=3,.extensions=gba_ext};
#if WITH_NES
static const char *const nes_ext[]={".nes",NULL};
const bb_system_t bb_system_nes={.abi=BB_SYSTEM_ABI,.id="nes",.name="Nintendo Entertainment System",.short_name="NES",.release_year=1983,.release_month=7,.extensions=nes_ext};
const bb_system_t *const bb_systems[]={&bb_system_ps1,&bb_system_gba,&bb_system_nes};
const unsigned bb_system_count=3;
#else
const bb_system_t *const bb_systems[]={&bb_system_ps1,&bb_system_gba};
const unsigned bb_system_count=2;
#endif
int bb_platform_count(const bb_system_t *s){(void)s;return 0;}

int main(void)
{
 /* the browser lists every registered system's files, case-insensitively, and nothing else */
 assert(bb_formats_match("Crash.cue") && bb_formats_match("Crash.CHD") && bb_formats_match("Kirby.gba"));
 assert(bb_formats_match("Pack.ZIP") && bb_formats_match("Game.agb") && bb_formats_match("disc.m3u"));
 assert(!bb_formats_match("readme.txt") && !bb_formats_match("noextension") && !bb_formats_match("save.sav"));
 assert(!bb_formats_match("Game.exe") && !bb_formats_match("Game.toc"));   /* the old PS1-only list had these; no registered system starts them */
 assert(bb_formats_match("Mario.nes")==WITH_NES);                           /* NES shows up only when it is registered */

 char sys[128],ext[128];
 bb_formats_systems(sys,sizeof(sys));
 bb_formats_extensions(ext,sizeof(ext));
#if WITH_NES
 assert(!strcmp(sys,"Game Boy Advance, PlayStation, Nintendo Entertainment System"));
 assert(strstr(ext,".nes"));
#else
 assert(!strcmp(sys,"Game Boy Advance, PlayStation"));
 assert(!strstr(ext,".nes") && !strstr(sys,"Nintendo Entertainment"));
#endif
 assert(!strncmp(ext,".cue .chd .pbp",14) && strstr(ext," .gba .agb .zip"));
 int zips=0;for(const char *p=ext;(p=strstr(p,".zip"));p++)zips++;
 assert(zips==1);
 for(unsigned i=0;i<bb_system_count;i++)assert(!strstr(sys,"PS1 only") && bb_systems[i]->name);

 /* a short buffer ends on a whole word, never in the middle of one, and always terminates */
 for(size_t cap=1;cap<sizeof(ext);cap++) {
  char small[128];memset(small,'x',sizeof(small));
  bb_formats_extensions(small,cap);
  assert(strlen(small)<cap);
  if(small[0]) {assert(small[0]=='.');const char *last=strrchr(small,' ');last=last?last+1:small;
   assert(last[0]=='.' && strlen(last)>=4);}
  char s2[128];memset(s2,'x',sizeof(s2));
  bb_formats_systems(s2,cap);
  assert(strlen(s2)<cap && !strncmp(s2,sys,strlen(s2)));
 }
 bb_formats_systems(sys,0);bb_formats_extensions(ext,0);   /* no room: no write, no crash */
 puts("bb_formats: ok");
 return 0;
}
