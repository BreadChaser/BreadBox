#include <string.h>
#include "bb_registry.h"
extern bb_probe_result bb_gba_probe(const bb_probe_input *,bb_entry *);
extern bb_probe_result bb_ps1_probe(const bb_probe_input *,bb_entry *);
static const char *const a[]={".gba",".agb",".zip",NULL},*const b[]={".cue",".bin",".iso",NULL};
static const char *const ah[]={"gba","game boy advance","nintendo game boy advance","2001",NULL},*const bh[]={"ps1","psx","sony playstation","playstation","1994",NULL};
#include <unistd.h>
static bb_probe_result slow_disc(const bb_probe_input *in,bb_entry *out)
{
 if(strstr(in->path,"slow-root"))usleep(20000);
 return bb_ps1_probe(in,out);
}
const bb_system_t bb_system_gba={.abi=BB_SYSTEM_ABI,.id="gba",.extensions=a,.folder_hints=ah,.probe=bb_gba_probe,.probe_bytes=0};
const bb_system_t bb_system_ps1={.abi=BB_SYSTEM_ABI,.id="ps1",.scan_priority=1,.extensions=b,.folder_hints=bh,.probe=slow_disc};
const bb_system_t *const bb_systems[]={&bb_system_gba,&bb_system_ps1};
const unsigned bb_system_count=2;
const bb_system_t *bb_system_by_id(const char *id) {for(unsigned i=0;i<bb_system_count;i++)if(!strcmp(id,bb_systems[i]->id))return bb_systems[i];return NULL;}

/* The CHD content probe has its own suite (tests/bb_disc). Here the answer follows the file name,
 * and every call is counted so the scan mapping (and the no-open rule for bare names) can be asserted. */
#include "../../include/breadbox/bb_disc.h"
unsigned test_chd_reads;
bb_disc_kind bb_disc_chd_kind(const char *path)
{
 test_chd_reads++;
 return strstr(path,"other")?BB_DISC_OTHER:strstr(path,"unread")?BB_DISC_UNREAD:BB_DISC_PS1;
}
