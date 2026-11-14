#!/usr/bin/env python3
"""PS1 Back at Continue? runs nothing (bb-uitest-fix-r1c, re-review R1b-4; the reviewer's ps1-card / HLE-card probes
made a test). run_game()'s launch section, from the memory cards' start to the end of the Continue question, and
leave_unplayed() are compiled verbatim from systems/ps1/emu.c and run with the real memory card code (cards.c,
final files, safe files, history) and a model guest: one that writes its memory card before its first vblank,
one that stalls in its first frame (an alarm ends the test if it ever runs). After the launch section comes a
model of the rest of run_game: the APT hook, guest frames and the end's card finalization.

Back (B), and Close from the HOME Menu (no GPU right), must leave with zero guest frames, no APT hook, no play
session and no room note (config.ini byte-identical), the memory card and the Continue point byte-identical, and
no core/renderer teardown without the GPU right. Quit at the memory card question leaves the same way. A
(Continue) is the positive control: frames run and the guest's card write lands, so the harness sees a frame.
The source check: every launch exit comes before home_note_game, playlog_begin, the APT hook and retro_run().
PS1's own Start over (continue_declined, verbatim, real history): an SD error looking at the point or its backup,
or a backup that could not be moved, keeps the old spot (the point stays when its backup does, as on GBA), freezes
the Continue point and says no (the question stays up: tests/save_service/ps1_continue_question.py; R1c-2)."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]
emu = (root / 'systems/ps1/emu.c').read_text()


def function(text, name):
    m = re.search(r'(?:PSX3D_COLD_FN\(\w+\) )?(?:static )?(?:bool|void|int) ' + name + r'\([^;{}]*?\)\s*\{', text)
    assert m, name
    i = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[m.start():i]


bad = []
run_game = function(emu, 'run_game')
start = run_game.index('\t{\n\t\tchar srm[PATH_LEN];')
end = run_game.index('\tif (clip_requested<0 && game_capabilities().rewind')
launch = run_game[start:end]
rest = run_game[end:]
# every way out of the launch section before play is a leave_unplayed(), and nothing that plays or notes the game
# comes before the last of them
exits = [m.start() for m in re.finditer(r'core_initialized = leave_unplayed\(\);\s*return true;', launch)]
if len(exits) != 2:
    bad.append(f'run_game: {len(exits)} early leaves in the launch section, expected the card question and Continue?')
for what in ('home_note_game(', 'playlog_begin(', 'aptHook(', 'retro_run('):
    first = run_game.find(what)
    if first < 0 or first < start + max(exits or [0]):
        bad.append(f'run_game: {what} comes before the Continue question\'s leave')
if 'retro_run(' in launch or 'aptHook(' in launch:
    bad.append('run_game: the launch section runs the guest or hooks APT')
if 'continue_prompt(prefix)' not in launch:
    bad.append('run_game: the Continue question moved out of the launch section')

code = r'''
#define main legacy_cards_main
#include "cards.c"
#undef main
#include <signal.h>
#include <errno.h>
#define PSX3D_COLD_FN(x)
#define PSX3D_SAVE_DIR "saves"
#define PSX3D_STATE_DIR "states"
#define STATE_AUTO 10
#define PATH_LEN 512
static char game_name[64]="game";
static int sw_wsscreen=-1,sw_state=-1;int clip_requested=-1;static bool sw_prof,psx3d_prof_on;
bool emu_continue_now;static bool core_initialized,gpu_right,quit_at_card,answer_back,went_back;
static int guest;   /* 0: writes its memory card before its first vblank; 1: stalls in its first frame */
static int frames,hooks,notes,sessions,unloads,deinits,video_ends,card_ends,config_ends;
static void make_path(char *out,size_t n,const char *dir,const char *ext){(void)dir;snprintf(out,n,"%s%s",game_name,ext);}
static bool dev_remote_game(void){return false;}
/* the real card question (systems/ps1/card_question.c, J-02 Restore) for the combined cases; its dialog answers
 * from a script (1: A, 0: B); otherwise the stub's answer */
bool real_cards_question(void);   /* card_question.c, included at the end (its headers redefine this file's paths) */
static bool real_card;static int script[8],asked;
bool ui_confirm_choice(const char *t,const char *m,const char *a,const char *b,bool g){(void)t;(void)m;(void)a;(void)b;(void)g;return script[asked++];}
bool cards_question(void){return real_card?real_cards_question():!quit_at_card;}
static void cheats_unload(void){}
static void cheats_load(const char *g){(void)g;}
void retro_cheat_reset(void){}
static void config_set_screen_mode(int m){(void)m;}
static void config_ws_sync(void){}
static bool state_load_from(int s){(void)s;return true;}
static void osd(const char *f,...){(void)f;}
/* continue_prompt() itself: tests/save_service/ps1_continue_question.py (B and Close answer false) */
static bool continue_prompt(const char *prefix){(void)prefix;if(answer_back){went_back=true;return false;}return true;}
static void home_note_game(const char *path,const char *save_name,const char *title,const char *state)
{(void)save_name;(void)title;(void)state;notes++;FILE *f=fopen("config.ini","w");assert(f);fprintf(f,"last_game_path=%s\n",path);fclose(f);}
static void playlog_begin(const char *g){(void)g;sessions++;}
static bool gspHasGpuRight(void){return gpu_right;}
void retro_unload_game(void){unloads++;}
static void config_game_end(void){config_ends++;}
static void audio_exit(void){}
void retro_deinit(void){deinits++;}
static void video_game_end(void){video_ends++;}
void clip_close(void){}
void retro_run(void)
{
 frames++;
 if(guest==1)for(;;)pause();   /* a game that never reaches its vblank: SIGALRM ends the test */
 pattern(ram,7);   /* the guest's own card write, before its first vblank */
}
''' + function(emu, 'leave_unplayed').replace('cards_end();', 'cards_end();card_ends++;') + r'''
/* PS1's own Start over, verbatim, on the real history; access() says EIO for one file */
#define PSX3D_COLD
#define STATE_WHY_TIMER 2
typedef struct { hist_entry_t e; unsigned id; } save_ref_t;
enum { UNDO_START = 1 };
static bool continue_frozen;static int undos,alerts_before;
static const char *access_fails;
static int test_access(const char *p,int m){if(access_fails && !strcmp(p,access_fails)){errno=EIO;return -1;}return access(p,m);}
static void state_file_info(const char *p,unsigned *r,unsigned *s,unsigned *id){(void)p;*r=0;*s=1;*id=0;}
static unsigned saved_at(const char *p){(void)p;return 1;}
static save_ref_t ref_entry(const hist_entry_t *e){save_ref_t r={*e,0};return r;}
static void undo_set(int kind,int slot,const save_ref_t *r){(void)kind;(void)slot;(void)r;undos++;}
#define access(p,m) test_access(p,m)
''' + function(emu, 'maybe_there') + '\n' + function(emu, 'continue_declined') + r'''
#undef access
/* run_game from its launch section on: the section verbatim, then a model of the rest (APT hook, frames, the end) */
static bool launch(const char *path,const char *save_name,const char *title)
{
''' + launch + r'''
	hooks++;
	for(int i=0;i<3;i++)retro_run();
	cards_finalize();cards_end();card_ends++;
	return true;
}
static unsigned char card_old[CARD],cont[64],cfg[64];
static bool same_file(const char *p,const unsigned char *want,size_t n)
{
 unsigned char now[64];FILE *f=fopen(p,"rb");if(!f)return false;size_t k=fread(now,1,sizeof now,f);fclose(f);
 return k==n && !memcmp(now,want,n);
}
static void setup(const char *name)
{
 static char top[512];if(!top[0])assert(getcwd(top,sizeof top));
 assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("hist");
 pattern(card_old,3);put("game.srm",card_old,CARD);blank(ram);
 memset(cont,0x99,sizeof cont);put("game.sta",cont,sizeof cont);
 memcpy(cfg,"last_game_path=sdmc:/roms/other.gba\n",36);put("config.ini",cfg,36);
 frames=hooks=notes=sessions=unloads=deinits=video_ends=card_ends=config_ends=0;core_initialized=true;went_back=false;
}
static int check(const char *name,bool played)
{
 bool nothing=!frames && !hooks && !notes && !sessions && file_is("game.srm",card_old) && same_file("config.ini",cfg,36);
 bool torn=card_ends==1 && config_ends==1 && !core_initialized && unloads==(gpu_right?1:0) && deinits==unloads && video_ends==unloads;
 if(!same_file("game.sta",cont,sizeof cont) || (played?nothing:!(nothing && torn))){
  fprintf(stderr,"%s: frames %d hooks %d notes %d sessions %d card same %d config same %d torn %d (unload %d deinit %d video %d)\n",
   name,frames,hooks,notes,sessions,file_is("game.srm",card_old),same_file("config.ini",cfg,36),torn,unloads,deinits,video_ends);
  return 1;
 }
 return 0;
}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));
 alarm(20);
 int fails=0;char name[64];
 for(guest=0;guest<2;guest++)for(int gpu=1;gpu>=0;gpu--){
  gpu_right=gpu;
  snprintf(name,sizeof name,"back-guest%d-gpu%d",guest,gpu);setup(name);answer_back=true;quit_at_card=false;
  launch("sdmc:/roms/game.cue",NULL,"Game");fails+=check(name,false);fails+=!went_back;
  snprintf(name,sizeof name,"card-quit-guest%d-gpu%d",guest,gpu);setup(name);answer_back=false;quit_at_card=true;
  launch("sdmc:/roms/game.cue",NULL,"Game");fails+=check(name,false);
 }
 /* PS1 Start over with SD errors: the old spot stays where it is or in the backups, the point frozen */
 static const struct {const char *name,*fails;bool frozen;} over[]={{"over",NULL,false},{"over-eio-backup","game.sta.bak",true},{"over-eio-point","game.sta",true}};
 for(unsigned i=0;i<3;i++){
  setup(over[i].name);put("game.sta.bak",cont,sizeof cont-1);continue_frozen=false;undos=0;alerts_before=alerts;
  access_fails=over[i].fails;bool started=continue_declined();access_fails=NULL;
  int left=(access("game.sta",F_OK)==0)+(access("game.sta.bak",F_OK)==0),in_backups;{hist_entry_t e[8];in_backups=history_list("game",false,e,8);}
  bool point_stays=!strcmp(over[i].name,"over-eio-backup")?access("game.sta",F_OK)==0 && !undos:true;
  if(continue_frozen!=over[i].frozen || started==over[i].frozen || alerts>alerts_before || !point_stays || left+in_backups!=2 || (!over[i].frozen && (left || undos!=1))){
   fprintf(stderr,"%s: frozen %d left %d in backups %d undos %d\n",over[i].name,continue_frozen,left,in_backups,undos);fails++;
  }
 }
 /* merge/v1-fixes: a damaged card with a good copy in Backups (J-02), then Continue? (UI branch). Quit at the card
  * question, start again, Restore, then Back (or Close without the GPU right) at Continue?: nothing played, the card
  * the restored copy, the damaged file kept as .bad, Continue and config.ini byte-identical; Restore then a Start
  * over that hits an SD error keeps the old spot and the restored card */
 static unsigned char good[CARD];pattern(good,5);
 for(int gpu=1;gpu>=0;gpu--){
  gpu_right=gpu;guest=0;snprintf(name,sizeof name,"card-quit-restore-back-gpu%d",gpu);setup(name);
  history_fake_now=2000;assert(history_add("game",HIST_CARD_SESSION,".srm",good,CARD));history_fake_now=0;
  put("game.srm",card_old,40000);real_card=true;answer_back=true;
  asked=0;script[0]=0;script[1]=0;   /* B Other choices, B Quit */
  launch("sdmc:/roms/game.cue",NULL,"Game");
  bool quit_ok=asked==2 && !frames && !hooks && !notes && !sessions && size_of("game.srm")==40000 && size_of("game.srm.bad")<0 && same_file("game.sta",cont,sizeof cont) && !went_back;
  frames=hooks=notes=sessions=unloads=deinits=video_ends=card_ends=config_ends=0;core_initialized=true;
  asked=0;script[0]=1;   /* A Restore; then B (or Close) at Continue? */
  launch("sdmc:/roms/game.cue",NULL,"Game");
  bool back_ok=asked==1 && went_back && !frames && !hooks && !notes && !sessions && file_is("game.srm",good) && size_of("game.srm.bad")==40000 &&
   same_file("game.sta",cont,sizeof cont) && same_file("config.ini",cfg,36) && card_ends==1 && unloads==(gpu?1:0);
  if(!quit_ok || !back_ok){fprintf(stderr,"%s: quit %d back %d (asked %d frames %d bad %ld)\n",name,quit_ok,back_ok,asked,frames,size_of("game.srm.bad"));fails++;}
 }
 gpu_right=true;setup("card-restore-continue");
 history_fake_now=2000;assert(history_add("game",HIST_CARD_SESSION,".srm",good,CARD));history_fake_now=0;
 put("game.srm",card_old,40000);real_card=true;answer_back=false;asked=0;script[0]=1;
 launch("sdmc:/roms/game.cue",NULL,"Game");
 {unsigned char seven[CARD];pattern(seven,7);
  if(frames!=3 || hooks!=1 || notes!=1 || !file_is("game.srm",seven) || size_of("game.srm.bad")!=40000){fprintf(stderr,"card-restore-continue: frames %d card written %d\n",frames,file_is("game.srm",seven));fails++;}}
 setup("card-restore-over-eio");
 history_fake_now=2000;assert(history_add("game",HIST_CARD_SESSION,".srm",good,CARD));history_fake_now=0;
 put("game.srm",card_old,40000);put("game.sta.bak",cont,sizeof cont-1);real_card=true;answer_back=true;asked=0;script[0]=1;
 launch("sdmc:/roms/game.cue",NULL,"Game");
 continue_frozen=false;undos=0;access_fails="game.sta.bak";bool started=continue_declined();access_fails=NULL;
 if(started || !continue_frozen || undos || !same_file("game.sta",cont,sizeof cont) || !file_is("game.srm",good) || size_of("game.srm.bad")!=40000){
  fprintf(stderr,"card-restore-over-eio: started %d frozen %d\n",started,continue_frozen);fails++;}
 real_card=false;
 /* the positive control: Continue plays, the guest's card write lands, the room notes the game */
 guest=0;gpu_right=true;setup("continue");answer_back=false;quit_at_card=false;
 launch("sdmc:/roms/game.cue",NULL,"Game");
 if(frames!=3 || hooks!=1 || notes!=1 || sessions!=1 || file_is("game.srm",card_old)){fprintf(stderr,"positive control: the harness did not see the frames\n");fails++;}
 if(fails)return 1;
 puts("PS1 Back / Close at Continue? and Quit at the card question: zero guest frames (a card-writing and a stalling guest), no APT hook, no session, no room note; card, Continue and config.ini byte-identical; no teardown without the GPU right; Continue plays (positive control); PS1 Start over with SD errors keeps the old spot; a damaged card restored from Backups then Back / Close / Continue / failed Start over (after a Quit at the card question): PASS");
 return 0;
}
#undef PSX3D_COLD
#undef PSX3D_SAVE_DIR
#undef PSX3D_STATE_DIR
#define cards_question real_cards_question
#include "card_question.c"
'''
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    src = td / 'ps1_back.c'
    src.write_text(code)
    (td / 'sd').mkdir()
    r = subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-format-truncation',
                        '-fsanitize=address,undefined', '-DSAFEFILE_TEST', '-include', 'tests/app/save/host.h', '-Itests/app/save',
                        '-Itests/app/host', '-Isystems/ps1', '-Iapp/ui', '-Iinclude/breadbox', '-Iplatform/3ds', '-Icore/deps/libretro-common/include',
                        str(src), 'systems/ps1/cards.c', 'app/ui/bb_final.c', 'app/ui/safefile.c', 'app/ui/history.c',
                        '-Wl,--wrap=stat,--wrap=fopen', '-o', str(td / 'ps1_back')], cwd=root)   # cards.c test: SD error hooks
    if r.returncode:
        sys.exit('ps1_back: the launch section did not compile')
    if subprocess.run([str(td / 'ps1_back'), str(td / 'sd')]).returncode:
        bad.append('PS1 Back ran the game or changed a file')
if bad:
    sys.exit('\n'.join(bad))
print('PS1 Back PASS: run_game leaves before anything plays (source order checked; launch section run on the real card code)')
