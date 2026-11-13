/* Display order of the systems: release date newest first, ties by name.
 * Menus ask here and never sort themselves. */
#include <strings.h>
#include "bb_registry.h"

static unsigned released(const bb_system_t *s) {return s->release_year*12u+(s->release_month?s->release_month-1u:0u);}
static bool before(const bb_system_t *a,const bb_system_t *b)
{
 if(released(a)!=released(b))return released(a)>released(b);
 return strcasecmp(a->name?a->name:"",b->name?b->name:"")<0;
}
unsigned bb_system_display_order(const bb_system_t **out,unsigned cap,bool with_games)
{
 unsigned n=0;
 for(unsigned i=0;i<bb_system_count && n<cap;i++) {
  const bb_system_t *s=bb_systems[i];
  if(with_games && bb_system_library_count(s)<=0)continue;
  unsigned at=n++;
  while(at && before(s,out[at-1])) {out[at]=out[at-1];at--;}
  out[at]=s;
 }
 return n;
}
