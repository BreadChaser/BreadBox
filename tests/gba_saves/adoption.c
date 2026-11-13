/* Legacy ownership/adoption remains adapter policy; keep its regression apart
 * from the production shell persistence tests. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "gba_saveid.h"
#include "gba_settings.h"
#include "safefile.h"
/* App preference service is separate from GBA adoption/storage policy. */
void control_side_set(int left){(void)left;}
bool gba_cart_fingerprint(const char *path,unsigned char h[192],uint32_t *crc)
{*crc=strstr(path,"other")?2:1;memset(h,0,192);memcpy(h+0xac,"TEST",4);if(strstr(path,"other"))h[0xa0]=1;return true;}
bool gba_cart_identity(const char *path,unsigned char h[192],uint32_t *crc,bool *legacy,uint32_t *sample)
{*legacy=true;bool ok=gba_cart_fingerprint(path,h,crc);*sample=*crc;return ok;}
int main(void)
{
 const char *dirs[]={"sdmc:","sdmc:/3ds","sdmc:/3ds/BreadBox","sdmc:/3ds/BreadBox/saves","sdmc:/3ds/BreadBox/saves/gba","sdmc:/3ds/BreadBox/states","sdmc:/3ds/BreadBox/states/gba","sdmc:/3ds/BreadBox/config"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++)mkdir(dirs[i],0700);
 unsigned char h[192]={0},ram[128];memcpy(h+0xac,"TEST",4);char a[GBA_ID_LEN],b[GBA_ID_LEN],p[512];
 gba_saveid_header(h,1,a);gba_saveid_header(h,2,b);assert(strcmp(a,b));
 safe_format_t fmt={.size=sizeof(ram)};memset(ram,4,sizeof(ram));
 assert(safe_write("sdmc:/3ds/BreadBox/saves/gba/Same.sav",&fmt,ram,true));
 assert(gba_saveid_migrate("folder-a/Same.gba",a));assert(gba_saveid_migrate("folder-b/Same.gba",b));
 snprintf(p,sizeof(p),"sdmc:/3ds/BreadBox/saves/gba/%s.sav",a);assert(!access(p,F_OK));
 snprintf(p,sizeof(p),"sdmc:/3ds/BreadBox/saves/gba/%s.sav",b);assert(access(p,F_OK));
 /* J-14: the ownership record is completed in place, never left as <game>.id.bad */
 assert(access("sdmc:/3ds/BreadBox/saves/gba/Same.id.bad",F_OK) && access("sdmc:/3ds/BreadBox/saves/gba/Same.id.bak",F_OK));
 {FILE *f=fopen("sdmc:/3ds/BreadBox/saves/gba/Same.id","r");char line[64]={0};assert(f && fgets(line,sizeof(line),f));fclose(f);
  assert(!strncmp(line,a,strlen(a)) && line[strlen(a)]=='\n');}
 assert(!access("sdmc:/3ds/BreadBox/saves/gba/Same.sav",F_OK));
 assert(gba_saveid_migrate("folder-a/Same.gba",a));
 snprintf(p,sizeof(p),"sdmc:/3ds/BreadBox/saves/gba/%s.sav",a);assert(!remove(p));
 assert(gba_saveid_migrate("folder-a/Same.gba",a));assert(access(p,F_OK));
 gba_settings_for_game("folder-a/Same.gba");gba_settings_set(1,1);assert(gba_settings.smoothing);
 gba_settings_for_game("other/Same.gba");assert(!gba_settings.smoothing);
 gba_settings_for_game("moved/Renamed.gba");assert(gba_settings.smoothing);
 puts("Legacy GBA collision ownership, one-time adoption and moved-game settings PASS");
}
