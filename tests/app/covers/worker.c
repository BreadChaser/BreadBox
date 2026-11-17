#include "sdk.h"
#include "../../../app/ui/covers.c"
#include "../../../app/ui/covers_wait.c"
#include "../../../app/ui/covers_match.c"
#include "../../../app/services/covers_fetch.c"
#include "shelf-drain.inc"
bool breadbox_import_pause(void){return true;}
void breadbox_import_resume(void){}
static bool cart_cover(int index,bb_cover_item *out) {assert(index==0);*out=(bb_cover_item){.title=game.title,.key=game.cover_key,.code=game.group,.path=game.ndiscs?game.discs[0]:NULL};return true;}
extern int gba_boxart_candidates(const char *,const char *,char out[][256],int);
static const bb_system_t cart={.id="gba",.cover_db="Nintendo - Game Boy Advance",.cover_item=cart_cover,.cover_candidates=gba_boxart_candidates};
static const bb_system_t generic={.id="snes",.cover_db="Nintendo - Super Nintendo Entertainment System"};
static const bb_system_t marked={.id="mk",.logo_url=THUMB_URL "logo.png"},unmarked={.id="um",.logo_url=THUMB_URL "missing.png"};
const bb_system_t *const bb_systems[]={&cart,&generic,&marked,&unmarked};
const unsigned bb_system_count=4;
bool bb_index_get(const bb_system_t *s,int i,char *p,size_t cap,bb_entry *e) {if(s!=&generic || i)return false;snprintf(p,cap,"roms/Game.sfc");memset(e,0,sizeof(*e));strcpy(e->title,"Game");strcpy(e->cover,"Box");return true;}
const bb_system_t *bb_system_by_id(const char *id) {return !strcmp(id,"gba")?&cart:NULL;}
static bool fresh;
static void svcSleepThread(long long ns) { if(cancel_after && ++sleeps>=cancel_after) covers_paused=true;usleep(ns/1000); }
int main(void)
{
	covers_init();
 cover_record record;const game_t *g=cover_game(2*65536,&record);
 assert(g && !strcmp(g->title,"Game") && !strcmp(g->cover_key,"snes/Box") && !strcmp(g->discs[0],"roms/Game.sfc"));
 // Actual shared fetch/pack/cache path, including a transfer beyond the old 2s timeout.
 {
  covers_paused=false;bool retry=true;int w=0,h=0;
  u64 start=osGetTime();u32 *px=download("gba/delayed",&retry,&w,&h);
  assert(px && w==1 && h==1 && osGetTime()-start>=2500);free(px);
  char path[PATH_LEN];covers_cache_path("gba/Kirby & The Amazing Mirror (USA)",".cv2",path,sizeof(path));
  assert(!strcmp(path,"./covers/gba/Kirby _ The Amazing Mirror (USA).cv2"));
  // User PNGs are authoritative with networking disabled, even without a folder scan match.
  sw_nonet=true;strcpy(game.cover_key,"gba/Own");game.ndiscs=1;game.discs[0]="own.gba";
  cover_t own={0};bool fresh=false;
  assert(load_cover(&game,&retry,false,&own,&fresh) && own.w==2 && fresh);free(own.data);
  assert(!access("covers/gba/Own.cv2",F_OK));
  strcpy(game.cover_key,"cover");game.ndiscs=0;sw_nonet=false;covers_paused=true;
 }
 // A system's logo (not in the app): asked for once, fetched over the cover connection, cached on the SD card.
 {
  int w=0,h=0;struct stat st;
  assert(!covers_logo(&cart,&w,&h) && !covers_logo(NULL,&w,&h));   // no logo_url: the generic mark
  assert(!covers_logo(&marked,&w,&h) && covers_logos[2].wanted && !covers_logos[2].have);
  sw_nonet=true;covers_paused=false;assert(!logos_work());assert(stat("logos/mk.png",&st));   // offline: nothing fetched
  sw_nonet=false;covers_net_retry_at=0;assert(logos_work() && covers_logos[2].have);
  assert(!stat("logos/mk.png",&st) && stat("logos/mk.tmp",&st));
  const u32 *px=covers_logo(&marked,&w,&h);assert(px && w==1 && h==1 && px[0]==0xff8000ffu);
  assert(!logos_work());   // cached: no second request
  // the server has no picture: tried once, never again this session, nothing on the card
  assert(!covers_logo(&unmarked,&w,&h) && logos_work() && covers_logos[3].tried && !covers_logos[3].have && stat("logos/um.png",&st));
  assert(!logos_work());
  // a damaged cache file is removed and asked for again
  free(covers_logos[2].px);memset(&covers_logos[2],0,sizeof(covers_logos[2]));
  FILE *lf=fopen("logos/mk.png","wb");assert(lf);fputs("not a png",lf);fclose(lf);
  // the worker decodes it (never the UI thread): it finds it damaged, removes it, and it is asked for again
  assert(!covers_logo(&marked,&w,&h) && covers_logos[2].decode);
  assert(logos_work() && stat("logos/mk.png",&st) && !covers_logos[2].have);
  assert(!covers_logo(&marked,&w,&h) && covers_logos[2].wanted);
  // an interrupted transfer (HOME) keeps nothing and is tried again
  unsigned was_aborted=covers_aborted_count;cancel_after=1;sleeps=0;covers_logos[2].wanted=true;assert(logos_work() && !covers_logos[2].have && !covers_logos[2].tried);cancel_after=0;covers_aborted_count=was_aborted;
  covers_paused=true;
  for(int i=0;i<BB_MAX_SYSTEMS;i++){free(covers_logos[i].px);memset(&covers_logos[i],0,sizeof(covers_logos[i]));}
 }
assert(covers_paused);covers_resume();assert(!covers_paused);covers_get(0,NULL);assert(covers_slots[0].state==1);
	// Transient network backoff must return immediately instead of sleeping 15s.
	covers_net_retry_at=osGetTime()+15000;bool retry=true;
	cover_t c={0};
	u64 start=osGetTime();assert(!load_cover(&game,&retry,false,&c,&fresh));assert(osGetTime()-start<100 && retry);
	covers_net_retry_at=0;
	LightLock_Lock(&covers_net_lock);
	int w,h;
	u32 *px=download("cover",&retry,&w,&h);assert(px && w==1 && h==1);free(px);
	px=download("missing",&retry,&w,&h);assert(!px && !retry && covers_last_http==404);
	px=download("error",&retry,&w,&h);assert(!px && retry && covers_last_http==503);
	cancel_after=1;sleeps=0;
	start=osGetTime();px=download("slow",&retry,&w,&h);assert(!px && retry && covers_aborted_count==1);assert(osGetTime()-start<100);
	cancel_after=0;
	LightLock_Unlock(&covers_net_lock);
	covers_stop_wait();assert(covers_paused && !covers_net_open && !covers_busy_workers);
	// A cover cached by an older version shows at once; the full size one replaces it.
	{
		covers_net_retry_at=0;
		u32 *old=calloc(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,4);assert(old);
		for(int i=0;i<COVER_LEGACY_SIZE*COVER_LEGACY_SIZE;i++) old[i]=0x336699ffu;
		old[5]=0x10203000u; // transparent: flattened onto the card colour
		FILE *f=fopen("covers/cover.bin","wb");assert(f&&fwrite(old,4,COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,f)==(size_t)(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE));fclose(f);
		f=fopen("covers/missing.bin","wb");assert(f&&fwrite(old,4,COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,f)==(size_t)(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE));fclose(f);
		free(old);
		covers_paused=false;
		retry=true;cover_t lc={0};
		assert(load_cover(&game,&retry,false,&lc,&fresh) && lc.lowres && lc.w==128 && lc.h==128);
		free(lc.data);
		struct stat st;
		assert(stat("covers/cover.cv2",&st) && !stat("covers/cover.bin",&st)); // not cached yet: still the old file only
		cover_t up={0};
		assert(upgrade_cover(&game,&retry,&up) && !up.lowres && up.w==1 && up.h==1);
		free(up.data);
		assert(!stat("covers/cover.cv2",&st) && st.st_size==CV2_BYTES && stat("covers/cover.bin",&st));
		cover_t hit={0};
		assert(load_cover(&game,&retry,false,&hit,&fresh) && !hit.lowres && hit.w==1 && hit.h==1);free(hit.data);
		// the database has no such picture: the old one becomes final
		strcpy(game.cover_key,"missing");
		cover_t fin={0};
		assert(upgrade_cover(&game,&retry,&fin) && !fin.lowres && fin.w==128 && fin.h==128 && !retry);
		free(fin.data);
		assert(!stat("covers/missing.cv2",&st) && st.st_size==CV2_BYTES && stat("covers/missing.bin",&st));
		strcpy(game.cover_key,"cover");
		// a short or foreign cache file is never trusted
		f=fopen("covers/cover.cv2","wb");assert(f);fwrite("PCV2 short",1,10,f);fclose(f);
		cover_t bad={0};
		assert(!read_cv2("covers/cover.cv2",&bad) && !bad.data);
		covers_stop_wait();
	}
	// Box art matching, the memory of missing pictures, the player's own PNG.
	{
		covers_paused=false;covers_net_retry_at=0;covers_net_backoff=0;
		bool r=true;cover_t m={0};struct stat st;
		// a file called anything: found by the serial in its name; the server has no picture under the file name
		game_t odd={.cover_key="SLUS-00001 backup",.title="Odd"};
		unsigned other=covers_other_names;
		assert(fetch_cover(&odd,&r,&m) && m.w==1);free(m.data);assert(covers_other_names==other+1);
		// the database has nothing: asked once, then remembered
		game_t none={.cover_key="Nothing Here",.title="None"};
		unsigned before=covers_attempts;r=true;m=(cover_t){0};
		assert(!fetch_cover(&none,&r,&m) && !r && covers_attempts==before+1);
		before=covers_attempts;unsigned skips=covers_miss_skips;r=true;
		assert(!fetch_cover(&none,&r,&m) && !r && covers_attempts==before && covers_miss_skips==skips+1);
		assert(!stat("covers/state.txt",&st));
		// ... also after a restart (the file is read again) ...
		boxart_misses_free();boxart_misses_load(COVER_STATE,BB_VERSION);
		before=covers_attempts;r=true;
		assert(!fetch_cover(&none,&r,&m) && !r && covers_attempts==before);
		// ... but a rescan asks again
		covers_reset();
		before=covers_attempts;r=true;
		assert(!fetch_cover(&none,&r,&m) && !r && covers_attempts==before+1);
		// no network is not "the database has none": nothing remembered, backoff doubles
		game_t down={.cover_key="Down Unit",.title="Down"};
		CURLcode dummy=0;(void)dummy;
		covers_net_backoff=0;net_failed();assert(covers_net_backoff==15000);net_failed();assert(covers_net_backoff==30000);
		for(int i=0;i<10;i++) net_failed();
		assert(covers_net_backoff==600000);
		r=true;assert(!fetch_cover(&down,&r,&m) && r); // still inside the backoff: no request
		assert(!boxart_miss_known("Down Unit",osGetTime()));
		covers_net_retry_at=0;covers_net_backoff=0;
		// the player's own picture wins over the cached download, follows changes and removal
		cover_t a={0};
		strcpy(game.cover_key,"cover");
		remove("covers/cover.png");
		assert(load_cover(&game,&r,false,&a,&fresh) && a.w==1);free(a.data);              // cache from the download
		before=covers_attempts;
		assert(rename("user.png","covers/cover.png")==0);
		a=(cover_t){0};assert(load_cover(&game,&r,false,&a,&fresh) && a.w==2 && a.h==2);free(a.data);
		a=(cover_t){0};assert(load_cover(&game,&r,false,&a,&fresh) && a.w==2);free(a.data); // from the cache, marked
		assert(covers_attempts==before);
		FILE *pf=fopen("covers/cover.png","ab");assert(pf);fputc(0,pf);fclose(pf);      // changed file
		long sz,mt;assert(boxart_user_get("cover",&sz,&mt));
		a=(cover_t){0};assert(load_cover(&game,&r,false,&a,&fresh) && a.w==2);free(a.data);
		long sz2,mt2;assert(boxart_user_get("cover",&sz2,&mt2) && sz2==sz+1);
		remove("covers/cover.png");                                                   // PNG removed: download again
		a=(cover_t){0};assert(load_cover(&game,&r,false,&a,&fresh) && a.w==1);free(a.data);
		assert(covers_attempts==before+1 && !boxart_user_get("cover",&sz,&mt));
		// a PNG that cannot be read does not hide the cover
		pf=fopen("covers/cover.png","wb");assert(pf);fputs("not a png",pf);fclose(pf);
		a=(cover_t){0};assert(load_cover(&game,&r,false,&a,&fresh) && a.w==1);free(a.data);
		remove("covers/cover.png");
		for(int i=0;i<SLOTS;i++) covers_slots[i].last_used=0;   // the rescan above emptied the slots: slot 0 is the oldest again
		covers_get(0,NULL);
		covers_stop_wait();
	}
	// HOME restore must not release an in-progress incremental publication.
	covers_resume();covers_library_begin();assert(covers_paused);
	on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONRESTORE,NULL);
	assert(covers_paused);covers_library_end();assert(!covers_paused);covers_stop_wait();
	blocked=true;covers_resume();assert(covers_paused);blocked=false;
	on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);on_apt(APTHOOK_ONWAKEUP,NULL);assert(covers_paused); // gameplay stays stopped
	covers_resume();on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);assert(covers_paused);on_apt(APTHOOK_ONWAKEUP,NULL);assert(!covers_paused);
	for(int i=0;i<SLOTS;i++) covers_slots[i].last_used=0; // transaction emptied the queue
	assert(covers_refresh(0));assert(covers_slots[0].refresh && covers_slots[0].state==1);
	covers_slots[0].pixels=malloc(COVER_PACK_BYTES);covers_slots[0].state=3;covers_slots[0].lowres=true;
	covers_release_gpu();assert(!covers_slots[0].pixels && covers_slots[0].state==1 && !covers_slots[0].lowres);
	// Launch releases the GPU cache. A background publication can be draining
	// while the player re-enters Library; its eventual end must honor that resume.
	for (int model=0;model<2;model++) {
		g_new3ds=model;
		covers_resume();
		covers_library_begin();covers_library_end();
		assert(!covers_get(0,NULL));
		slot_t *live=NULL;for(int i=0;i<SLOTS;i++)if(covers_slots[i].game==0)live=&covers_slots[i];assert(live);
		live->pixels=calloc(1,COVER_PACK_BYTES);assert(live->pixels);
		live->w=live->h=32;live->state=2;
		covers_pump();video_pre_draw();assert(covers_full(0));
		live->retry_at=osGetTime()+15000;unsigned epoch=gpu_generation;
		covers_stop_wait();covers_release_gpu();assert(!live->tex_ok && !live->shown);
		assert(gpu_generation==epoch+1 && live->state==1);
		live->retry_at=0; // fixture permits the next decode; release preserves network backoff on New
		// Favorite/sort rebuilt the catalog during the return drain: the old
		// generation predicate no longer asks for a rebuild on the next frame.
		covers_resume();covers_busy_workers=1;assert(!cover_drain(true) && covers_paused);
		assert(!cover_drain(false) && !covers_paused && !cover_drain_pending);
		covers_busy_workers=0;covers_stop_wait();
		// A deferred reader is still draining when the visible menu resumes.
		covers_busy_workers=1;assert(!covers_library_try_begin() && try_open && covers_paused);
		covers_resume();covers_busy_workers=0;
		assert(covers_library_try_begin());covers_library_end();
		printf("lifecycle model=%d paused=%d depth=%u pending=%d\n",model,(int)covers_paused,library_changes,try_open);
		assert(!covers_paused && !library_changes && !try_open);
		assert(!covers_get(0,NULL));
		for(int i=0;i<SLOTS;i++)if(covers_slots[i].game==0)live=&covers_slots[i];
		assert(live->state==1);
		bool retry=true,fresh=false;cover_t c={0};
		assert(load_cover(&game,&retry,false,&c,&fresh));
		live->pixels=c.data;live->w=c.w;live->h=c.h;live->state=2;
		covers_pump();video_pre_draw();Tex3DS_SubTexture sub;
		C3D_Tex *tex=covers_get(0,&sub);
		assert(tex && tex->data && live->tex_ok && live->shown && covers_full(0));
		covers_stop_wait();covers_release_gpu();
		assert(!cover_drain(false) && covers_paused); // an idle owner never resumes gameplay
		live->pixels=calloc(1,COVER_PACK_BYTES);assert(live->pixels);
		live->state=2;live->lowres=true;u8 *decoded=live->pixels;
		covers_release_gpu();
		if(model)assert(live->pixels==decoded && live->state==2 && !live->lowres);
		else assert(!live->pixels && live->state==1 && !live->lowres);
	}
	g_new3ds=false;
	assert(!covers_refresh(-1) && !covers_refresh(1));
	// Small covers: made from a cover, saved, read back the same; a damaged
	// file gives what was whole and is written again; a player's PNG is found
	// by the folder listing (no lookup for the others).
	{
		covers_paused=false;
		covers_thumbs_free();covers_thumbs_loaded=true;
		cover_t one={malloc(COVER_PACK_BYTES),200,180,false};
		memset(one.data,0x5a,COVER_PACK_BYTES);
		thumb_make(game.cover_key,&one,false);assert(covers_nthumbs==1 && covers_thumbs_dirty);
		u8 first[COVER_THUMB_BYTES];memcpy(first,covers_thumbs[0]->px,sizeof(first));
		assert(covers_thumbs[0]->w==200 && covers_thumbs[0]->h==180);
		memset(one.data,0x11,COVER_PACK_BYTES);
		thumb_make(game.cover_key,&one,false);assert(!memcmp(first,covers_thumbs[0]->px,sizeof(first)));   // kept
		thumb_make("other",&one,true);assert(covers_nthumbs==2);
		thumbs_save();assert(!covers_thumbs_dirty);
		covers_thumbs_free();assert(!covers_nthumbs);
		assert(thumbs_load() && covers_nthumbs==2 && !covers_thumbs_dirty);
		int k=covers_thumb_find(covers_name_hash(game.cover_key));assert(k>=0 && !memcmp(first,covers_thumbs[k]->px,sizeof(first)));
		// cut the file in the middle of the second record
		FILE *f=fopen(THUMB_FILE,"r+b");assert(f);assert(!ftruncate(fileno(f),sizeof(thumb_head_t)+sizeof(thumb_t)+100));fclose(f);
		covers_thumbs_free();assert(thumbs_load() && covers_nthumbs==1 && covers_thumbs_dirty);
		thumbs_save();covers_thumbs_free();assert(thumbs_load() && covers_nthumbs==1 && !covers_thumbs_dirty);
		// a pause stops the reading: nothing half-read stays
		covers_thumbs_free();covers_paused=true;assert(!thumbs_load() && !covers_nthumbs && !covers_thumbs_loaded);covers_paused=false;
		free(one.data);
		f=fopen(COVER_DIR "/cover.png","wb");fputs("x",f);fclose(f);
		own_pngs_scan();assert(covers_own_pngs_ok && covers_nown_pngs==1 && own_png_maybe("cover") && !own_png_maybe("other"));
		remove(COVER_DIR "/cover.png");own_pngs_scan();assert(!own_png_maybe("cover"));
		remove(THUMB_FILE);
	}
	// HOME during a cover download (one helper for Home and the Library): held back while a worker is busy;
	// a press waits for the workers to stop, then goes to the HOME Menu itself; idle, HOME is simply allowed
	{
		home_jumps = 0; home_rejected = false;
		assert(!covers_home_frame() && home_allowed && !home_jumps);
		__atomic_add_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
		assert(!covers_home_frame() && !home_allowed);
		home_rejected = true;
		assert(!covers_home_frame() && !home_jumps && !home_allowed);   // pressed: the download is still under way
		assert(!covers_home_frame() && !home_jumps && !home_allowed);   // held, the press remembered
		__atomic_sub_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
		assert(covers_home_frame() && home_jumps == 1 && home_allowed);
		assert(!covers_home_frame() && home_jumps == 1 && home_allowed);
	}
	covers_exit();
	puts("PASS small covers, remote covers, core0 priority, backoff, HTTPS driver cancellation, HTTP retries, nested APT gameplay pause, HOME held during downloads");
}

bool bb_update_installing(void){return false;}
bool bb_update_committed(void){return false;}
bool bb_update_net_busy(void){return false;}
bool bb_update_net_stop(void){return true;}
