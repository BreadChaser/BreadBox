#include "app_policy.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "bb_system.h"
#include "bb_index.h"
#include "bb_disc.h"
int bb_index_ps1_count(void) {return bb_index_count(bb_system_by_id("ps1"));}
bool bb_index_ps1_get(int i,char *path,size_t cap) {return bb_index_get(bb_system_by_id("ps1"),i,path,cap,NULL);}
bb_probe_result bb_ps1_probe(const bb_probe_input *in,bb_entry *out)
{
 const char *ext=strrchr(in->path,'.');if(!ext)return BB_PROBE_NO;
 if(!strcasecmp(ext,".chd") && !bb_disc_is_ps1_chd(in->path))return BB_PROBE_NO;
 /* Raw discs have no universal magic. They stay MAYBE so a cartridge's
  * validated header beats them, and equal ambiguous scores are refused. */
 snprintf(out->title,sizeof(out->title),"%s",in->name);
 char *dot=strrchr(out->title,'.');if(dot)*dot=0;
 return (!strcasecmp(ext,".cue")||!strcasecmp(ext,".m3u")||!strcasecmp(ext,".chd")||!strcasecmp(ext,".pbp"))?BB_PROBE_YES:BB_PROBE_MAYBE;
}
bool bb_ps1_identify(const char *path,const bb_entry *e,bb_content_id *id,char *error,size_t cap)
{
 (void)e;(void)error;(void)cap;
 /* The core confirms the serial after load. This is the provisional path key. */
 unsigned h=2166136261u;for(const unsigned char *p=(const void *)path;*p;p++)h=(h^*p)*16777619u;
 snprintf(id->key,sizeof(id->key),"DISC-%08X",h);id->crc=h;return true;
}
