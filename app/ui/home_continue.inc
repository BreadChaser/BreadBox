// Continue snapshots: the UI captures names only. All SD reads run on the
// index worker's low lane; an unavailable worker leaves the last snapshot up.
typedef struct {
 unsigned serial;
 int done;
 bool ok, thumb, picture;
 u32 saved;
 char path[PATH_LEN], prefix[PATH_LEN], save[PATH_LEN], title[PATH_LEN];
 bb_path_snapshot moved;
 u16 *pixels, *tv, *card;
 bool baked;
} continue_job;
static continue_job *continue_work;
static unsigned continue_serial;
static bool continue_pending;
static u16 *continue_pixels;
static bool continue_picture, continue_same;
static u16 *continue_tv, *continue_card;

static bool continue_cancel(void *arg)
{
 continue_job *j=arg;
 bb_index_visible_checkpoint();
 return bb_index_cancelled() || j->serial!=__atomic_load_n(&continue_serial,__ATOMIC_ACQUIRE);
}

// Worker only. Never touches configuration, library storage, textures or room.
static void cont_probe(void *arg)
{
 continue_job *j=arg;u64 start=svcGetSystemTick();char missing[PATH_LEN];
 if(continue_cancel(j))goto cancelled;
 if(!j->prefix[0] || !j->path[0])goto picture;
 if(library_missing_file_cancel(j->path,missing,sizeof(missing),continue_cancel,j)) {
  j->path[0]=0;
  if(continue_cancel(j))goto cancelled;
  if(j->moved.resolve && !j->moved.resolve(j->moved.data,j->path,sizeof(j->path)))j->path[0]=0;
  if(continue_cancel(j))goto cancelled;
  if(!j->path[0] || library_missing_file_cancel(j->path,missing,sizeof(missing),continue_cancel,j))goto picture;
 }
 if(continue_cancel(j))goto cancelled;
 const bb_system_t *system=bb_system_for_path(j->path);
 if(system && system->shell && system->shell->legacy_continue) {
  // Legacy save metadata and thumbnail, using the captured prefix.
  j->ok=states_auto_read(j->prefix,&j->saved,j->pixels,&j->thumb,continue_cancel,j);
 } else if(system && system->identify && (system->continue_available || system->save)) {
  bb_content_id id={0};
  bool identified=system->identify(j->path,NULL,&id,NULL,0);
  if(continue_cancel(j))goto cancelled;
  j->ok=identified && (system->continue_available?system->continue_available(&id,&j->saved):bb_save_continue_read(system,&id,&j->saved,continue_cancel,j));
 }
picture:
 if(continue_cancel(j))goto cancelled;
 j->picture=j->thumb || (j->pixels && bb_last_picture_read(j->ok?j->path:NULL,j->pixels,continue_cancel,j));
 if(continue_cancel(j))goto cancelled;
 if(j->picture && j->pixels) {
  j->tv=malloc(256*256*2);j->card=malloc(256*256*2);
  if(j->tv && j->card)j->baked=room_scene_tv_bake(j->pixels,j->tv,j->card);
 }
 log_printf("home: Continue worker ms=%lu\n",(unsigned long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000)));
 __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);return;
cancelled:
 __atomic_store_n(&j->done,2,__ATOMIC_RELEASE);
}

static void cont_request_refresh(void)
{
 __atomic_add_fetch(&continue_serial,1,__ATOMIC_RELEASE);
 continue_pending=true;
}

// Main thread: immutable inputs in, completed snapshot out. No filesystem or
// synchronous fallback here, including on entry, reveal and Settings return.
static bool cont_poll(void)
{
 bool changed=false;
 if(continue_work && __atomic_load_n(&continue_work->done,__ATOMIC_ACQUIRE)) {
  continue_job *j=continue_work;
  if(j->done==1 && j->serial==__atomic_load_n(&continue_serial,__ATOMIC_ACQUIRE)) {
   u64 start=svcGetSystemTick();
   if(cont_tex_ok){video_gpu_idle();C3D_TexDelete(&cont_tex);}
   cont_tex_ok=false;cont_ok=j->ok;cont_saved=j->saved;
   snprintf(cont_path,sizeof(cont_path),"%s",j->path);
   snprintf(continue_save,sizeof(continue_save),"%s",j->save);
   snprintf(continue_title,sizeof(continue_title),"%s",j->title);
   continue_same=continue_picture && j->picture && continue_pixels && j->pixels &&
    !memcmp(continue_pixels,j->pixels,BB_PICTURE_W*BB_PICTURE_H*2);
   free(continue_pixels);continue_pixels=j->pixels;j->pixels=NULL;
   free(continue_tv);free(continue_card);continue_tv=continue_card=NULL;
   if(j->baked){continue_tv=j->tv;continue_card=j->card;j->tv=j->card=NULL;}
   continue_picture=j->picture;
   if(j->thumb && continue_card)cont_tex_ok=room_scene_picture_upload(&cont_tex,continue_card);
   changed=true;
   log_printf("home: Continue publish us=%lu\n",(unsigned long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000000)));
  }
  if(j->moved.release)j->moved.release(j->moved.data);
  free(j->pixels);free(j->tv);free(j->card);free(j);continue_work=NULL;
 }
 if(continue_pending && !continue_work) {
  continue_job *j=calloc(1,sizeof(*j));if(!j)return changed;
  j->serial=__atomic_load_n(&continue_serial,__ATOMIC_ACQUIRE);
  snprintf(j->path,sizeof(j->path),"%s",g_cfg.last_game_path);
  snprintf(j->save,sizeof(j->save),"%s",g_cfg.last_game_save);
  snprintf(j->title,sizeof(j->title),"%s",g_cfg.last_game);
  cont_capture_prefix(j->prefix,sizeof(j->prefix));
  // The adapter captures owned inputs only; path resolution stays on the worker.
  const bb_system_t *system=bb_system_for_path(j->path);
  if(system && system->shell && system->shell->continue_capture)
   system->shell->continue_capture(g_cfg.last_game,&j->moved);
  j->pixels=malloc(BB_PICTURE_W*BB_PICTURE_H*2);
  if(!bb_index_defer_low(cont_probe,j)){
   if(j->moved.release)j->moved.release(j->moved.data);
   free(j->pixels);free(j);return changed;
  }
  continue_work=j;continue_pending=false;
 }
 return changed;
}

// Called after index shutdown, never on a Library/Settings visit. A timed-out
// shutdown may still own a callback: cancel it but leave its buffers/map alive.
bool home_release_cache(void)
{
 __atomic_add_fetch(&continue_serial,1,__ATOMIC_RELEASE);continue_pending=false;
 if(continue_work && !__atomic_load_n(&continue_work->done,__ATOMIC_ACQUIRE))return false;
 if(continue_work){
  continue_job *j=continue_work;
  if(j->moved.release)j->moved.release(j->moved.data);
  free(j->pixels);free(j->tv);free(j->card);free(j);continue_work=NULL;
 }
 // Keep one converted 96 KiB picture across gameplay. The return can queue it
 // immediately on the visible lane instead of showing snow behind cont_probe.
 free(continue_tv);free(continue_card);
 continue_tv=continue_card=NULL;continue_same=false;
 return true;
}
