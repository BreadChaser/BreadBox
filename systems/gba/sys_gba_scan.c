/* Cartridge scan/identity belongs to the module, never the index worker. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "bb_system.h"
#include "bb_index.h"
#include "gba_cart.h"
#include "gba_saveid.h"
bb_probe_result bb_gba_probe(const bb_probe_input *in,bb_entry *out)
{
 unsigned char h[192];
 if(!gba_cart_header(in->path,h))return BB_PROBE_NO;
 const char *name=in->name;snprintf(out->title,sizeof(out->title),"%s",name);
 char *dot=strrchr(out->title,'.');if(dot)*dot=0;
 uint32_t hash=2166136261u;for(const unsigned char *p=(const void *)in->path;*p;p++)hash=(hash^*p)*16777619u;
 gba_saveid_header(h,hash,out->key);
 snprintf(out->cover,sizeof(out->cover),"%.4s",h+0xac);
 return BB_PROBE_YES;
}
/* Home reopens frequently. Resolve legacy ownership once per unchanged ROM;
 * invalidate before a launch can migrate/create its saves. Never cache failures. */
static char identity_path[512];
static struct stat identity_stat;
static bb_content_id identity;
void bb_gba_identity_invalidate(void) {identity_path[0]=0;}
bool bb_gba_identify(const char *path,const bb_entry *e,bb_content_id *id,char *error,size_t cap)
{
 (void)e;unsigned char h[192];struct stat st;
 bool exists=!stat(path,&st);
 if(exists && !strcmp(path,identity_path) && st.st_size==identity_stat.st_size &&
    st.st_mtime==identity_stat.st_mtime) {*id=identity;return true;}
 if(gba_saveid_resolve(path,h,&id->crc,id->key)) {
  if(exists && strlen(path)<sizeof(identity_path)) {
   strcpy(identity_path,path);identity_stat=st;identity=*id;
  }
  return true;
 }
 if(error && cap)snprintf(error,cap,"%s",gba_cart_too_large(path)?gba_cart_error(GBA_CART_TOO_LARGE):"Could not identify cartridge");
 return false;
}
int bb_index_gba_count(void) {return bb_index_count(bb_system_by_id("gba"));}
