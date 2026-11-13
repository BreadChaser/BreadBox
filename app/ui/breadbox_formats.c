#include "app_policy.h"
/* What the registered systems can play, for the first-run text and the folder
 * browser: the lists come from bb_systems[] (apps/<app>.mk SYSTEMS), so a
 * system added to or left out of the build shows up here without a change. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "bb_registry.h"
#include "breadbox_formats.h"

#define COLD __attribute__((noinline,section(".psx3d_cold.text")))   /* menus only: the per-frame code keeps its place */

/* The browser's file filter: any registered system lists the file. Which system
 * runs it (and whether it is valid) is decided when it starts. */
COLD bool bb_formats_match(const char *name)
{
 for(unsigned i=0;i<bb_system_count;i++)if(bb_system_extension(bb_systems[i],name))return true;
 return false;
}

/* the registered systems' display names in menu order */
COLD void bb_formats_systems(char *out,size_t cap)
{
 const bb_system_t *order[BB_MAX_SYSTEMS];
 unsigned n=bb_system_display_order(order,BB_MAX_SYSTEMS,false);
 size_t used=0;
 if(!cap)return;
 out[0]=0;
 for(unsigned i=0;i<n && used<cap-1;i++) {
  int w=snprintf(out+used,cap-used,"%s%s",i?", ":"",order[i]->name?order[i]->name:order[i]->id);
  if(w<0)break;
  used+=(size_t)w<cap-used?(size_t)w:cap-used-1;
 }
}

/* `word` is one of the space separated words of `list` */
static bool listed(const char *list,const char *word)
{
 size_t n=strlen(word);
 for(const char *p=list;*p;) {
  size_t len=strcspn(p," ");
  if(len==n && !strncasecmp(p,word,n))return true;
  p+=len+(p[len]?1:0);
 }
 return false;
}

/* the registered file types, each once, in registry order */
COLD void bb_formats_extensions(char *out,size_t cap)
{
 size_t used=0;
 if(!cap)return;
 out[0]=0;
 for(unsigned i=0;i<bb_system_count;i++)
  for(const char *const *e=bb_systems[i]->extensions;e && *e;e++) {
   char dotted[16];
   snprintf(dotted,sizeof(dotted),"%s%s",**e=='.'?"":".",*e);
   if(listed(out,dotted))continue;
   int w=snprintf(out+used,cap-used,"%s%s",used?" ":"",dotted);
   if(w<0 || (size_t)w>=cap-used){out[used]=0;return;}   /* never cut an extension in half */
   used+=(size_t)w;
  }
}
