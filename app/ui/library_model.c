#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "library_model.h"
#include "bb_registry.h"
#include "bb_index.h"
#include "playlog.h"
#include "cold_code.h"

PSX3D_COLD bool bb_library_item(const bb_system_t *s, int row, bb_catalog_item *item)
{
	memset(item, 0, sizeof(*item));
	const bb_shell_ops *ops = s->shell;
	if (ops && ops->catalog) return ops->catalog(row, item);
	bb_entry e;
	if (!bb_index_get(s, row, item->path, sizeof(item->path), &e)) return false;
	snprintf(item->title, sizeof(item->title), "%s", e.title);
	snprintf(item->key, sizeof(item->key), "%s", e.key);
	item->cover_id = bb_system_cover_id(s, row);
	return true;
}
// Favorites and the play log use "<system>:<key>" (breadbox_favorites.c).
PSX3D_COLD void bb_library_key(const bb_system_t *s, bb_catalog_item *item)
{
	if (strchr(item->key, ':')) return;
	char key[sizeof(item->key)];
	snprintf(key, sizeof(key), "%s:%.240s", s->id, item->key);
	snprintf(item->key, sizeof(item->key), "%s", key);
}

// Resolve actions against the current catalog, never an index saved before a publication.
// This runs only on an explicit action, with no filesystem work.
PSX3D_COLD int bb_library_resolve(const bb_library_game *b, bb_catalog_item *item)
{
 int count=bb_system_library_count(b->system);
 for(int row=0;row<count;row++) {
  if(!bb_library_item(b->system,row,item))continue;
  bb_library_key(b->system,item);
  if(!strcmp(item->key,b->key))return row;
 }
 return -1; // removed or not published yet: never launch a different game
}

PSX3D_COLD void bb_library_fill(bb_library_game *b, const bb_system_t *s, int row, const bb_catalog_item *item)
{
 *b=(bb_library_game){.system=s,.row=row,.cover=item->cover_id,.spine=-1,
  .fav=playlog_favourite(item->key),.last=playlog_last(item->key),.seconds=playlog_seconds(item->key),
  .discs=item->discs>0 ? (unsigned char)(item->discs<255 ? item->discs : 255) : 1};
 snprintf(b->key,sizeof(b->key),"%s",item->key);
 snprintf(b->title,sizeof(b->title),"%s",item->title);
}
static int sort_mode;
static PSX3D_COLD int compare(const void *a, const void *b)
{
 const bb_library_game *x=a,*y=b;
 if(sort_mode==BB_LIBRARY_SORT_RECENT && x->last!=y->last)return x->last>y->last ? -1 : 1;
 if(sort_mode==BB_LIBRARY_SORT_MOST && x->seconds!=y->seconds)return x->seconds>y->seconds ? -1 : 1;
 return strcasecmp(x->title,y->title);
}
PSX3D_COLD void bb_library_sort(bb_library_row *row, int mode)
{
 sort_mode=mode;
 qsort(row->box,row->n,sizeof(*row->box),compare);
}
PSX3D_COLD int bb_library_build(bb_library_row *rows, int max_rows, bb_library_game *games, int max_games,
 int *nrows, int *total, char *last_title, size_t title_cap, bb_library_prepare prepare)
{
 int count=0;
 *nrows=*total=0;
 if(title_cap)last_title[0]=0;
 if(max_rows<1 || max_games<1)return 0;
 u32 newest=0;
 bb_library_row *fav=&rows[(*nrows)++];
 *fav=(bb_library_row){.name="Favorites"};
 snprintf(fav->plate,sizeof(fav->plate),"FAVORITES");
 const bb_system_t *order[BB_MAX_SYSTEMS];
 unsigned n=bb_system_display_order(order,BB_MAX_SYSTEMS,true);
 for(unsigned i=0;i<n && *nrows<max_rows;i++) {
  const bb_system_t *s=order[i];
  bb_library_row *r=&rows[(*nrows)++];
  *r=(bb_library_row){.system=s,.name=s->name,.box=games+count};
  snprintf(r->plate,sizeof(r->plate),"%s",s->name);
  for(char *c=r->plate;*c;c++)if(*c>='a' && *c<='z')*c-=32;
  int size=bb_system_library_count(s);
  for(int k=0;k<size && count<max_games;k++) {
   bb_catalog_item item;
   if(!bb_library_item(s,k,&item))continue;
   bb_library_key(s,&item);
   bb_library_game *b=&games[count++];
   if(prepare)prepare(b,s,k,&item,r->n+1);
   else bb_library_fill(b,s,k,&item);
   r->n++;(*total)++;
   if(b->last>newest){newest=b->last;snprintf(last_title,title_cap,"%s",b->title);}
  }
 }
 fav->box=games+count;
 int end=count;
 for(int i=0;i<end && count<max_games;i++)if(games[i].fav){games[count++]=games[i];fav->n++;}
 return count;
}
PSX3D_COLD bool bb_library_favorite(bb_library_game *games, int count, const bb_library_game *game, int *live_row)
{
 bb_catalog_item item;
 int row=bb_library_resolve(game,&item);
 if(row<0)return false;
 bb_library_key(game->system,&item);
 bool on=playlog_toggle_favourite(item.key);
 for(int i=0;i<count;i++)if(games[i].system==game->system && games[i].row==game->row)games[i].fav=on;
 if(live_row)*live_row=row;
 return true;
}
PSX3D_COLD bool bb_library_launch(const bb_library_game *game, char *path, size_t cap, const char **why, int *live_row)
{
 *why=BB_LIBRARY_LAUNCH_GONE;
 bb_catalog_item item;
 int row=bb_library_resolve(game,&item);
 if(live_row)*live_row=row;
 if(row<0)return false;
 const bb_shell_ops *ops=game->system->shell;
 bool ok=ops && ops->launch ? ops->launch(row,path,cap) : snprintf(path,cap,"%s",item.path)<(int)cap;
 if(!ok && item.discs>1)*why=BB_LIBRARY_LAUNCH_DISCS;
 return ok;
}
