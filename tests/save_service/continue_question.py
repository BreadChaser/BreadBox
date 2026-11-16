#!/usr/bin/env python3
"""The Continue question against the real save service (review fix r1b, finding 2; the reviewer's lifecycle
probe made a test). The production bb_pause_prompt(), bb_continue_input(), bb_pause_suspend()/restore() and the
GBA apt_event() run on scripted frames and APT events, linked with the real bb_save.c, final files, later thread,
safe files and history on a temporary SD card. Before every case the game has a Continue point (99) and its
backup (98); B, sleep/wake again and again, HOME > Close and power-off at the unanswered question, then the GBA
teardown's own calls, must leave both byte for byte. A plays on (points are made again); X + confirm moves both
to the backups."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]


def function(text, name):
    m = re.search(r'(?:static )?(?:bool|void|int) ' + name + r'\([^;{}]*?\)\s*\{', text)
    assert m, name
    i = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[m.start():i]


pause = (root / 'app/ui/bb_pause.c').read_text()
gba = (root / 'systems/gba/gba.c').read_text()
went_back = re.search(r'static bool went_back;\n.*?\n.*?\n', pause)[0]
code = r'''
#define main legacy_check_main
#include "check.c"
#undef main
typedef uint32_t u32;
typedef enum {APTHOOK_ONSUSPEND,APTHOOK_ONSLEEP,APTHOOK_ONEXIT,APTHOOK_ONRESTORE,APTHOOK_ONWAKEUP} APT_HookType;
enum {KEY_A=1,KEY_B=2,KEY_X=4,KEY_Y=8,SFX_SELECT,SFX_MOVE,SFX_BACK};
#include "bb_pause_answers.h"
typedef struct {int unused;} pc_card;
typedef struct {u32 key;const char *label;int icon;bool on;} ui_button_t;
static const ui_button_t bb_continue_bar[4];
static struct {pc_card card;u32 saved;} continue_view;
static struct {void (*pause)(bool);void (*rebase)(void);} pause_config;   /* bb_pause.c's config */
static bool game_active=true,run_stalled,paused;
/* the script: one letter a frame. B/A/X a key; S sleep and wake; H HOME and back; C HOME > Close; P power-off */
static const char *script;static unsigned frame;static bool confirm_yes;
static void apt_event(APT_HookType,void*);
static bool aptMainLoop(void)
{
 char c=script[frame];
 if(c=='S'){apt_event(APTHOOK_ONSLEEP,NULL);apt_event(APTHOOK_ONWAKEUP,NULL);}
 if(c=='H'){apt_event(APTHOOK_ONSUSPEND,NULL);apt_event(APTHOOK_ONRESTORE,NULL);}
 if(c=='C'){apt_event(APTHOOK_ONSUSPEND,NULL);apt_event(APTHOOK_ONEXIT,NULL);return false;}
 if(c=='P'){apt_event(APTHOOK_ONEXIT,NULL);return false;}
 return c!=0;
}
static void input_poll(void){}
static u32 input_keys_down(void){char c=script[frame++];return c=='A'?KEY_A:c=='B'?KEY_B:c=='X'?KEY_X:0;}
static void gba_touch_reset(void){}
static void game_pause(bool p){(void)p;}
static void game_rebase(void){}
static void *linearAlloc(size_t n){return malloc(n);}
static void linearFree(void *p){free(p);}
static void pc_picture(pc_card *c,void *p,unsigned w,unsigned h){(void)c;(void)p;(void)w;(void)h;}
static void pc_card_clear(pc_card *c){(void)c;}
static void dev_tick_keys(void){}
static u32 ui_bar_input(const ui_button_t *b,int n,u32 k){(void)b;(void)n;return k;}
static u32 ui_swap_keys(u32 k,u32 m){(void)m;return k;}
static void ui_swap_enter(void){}
static void ui_pause_frame(void){}
static void ui_begin(void){}
static void draw_continue_top(void *p){(void)p;}
static bool shown_failed;   /* the question's words said Start over did not happen */
static void draw_continue(void *p){shown_failed=*(const bool *)p;}
#include <errno.h>
static const char *stat_fails,*next_stat_fails;   /* this file's stat says EIO (an SD error, not "not there"): set at the question */
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s){if(stat_fails && !strcmp(p,stat_fails)){errno=EIO;return -1;}return __real_stat(p,s);}
static void video_present_vsync(bool b,void (*t)(void*),void (*d)(void*),void *p){(void)b;(void)t;if(d==draw_continue)d(p);}
static void sfx_play(int s){(void)s;}
static void sfx_now(int s){(void)s;}
static void sfx_flush(void){}
static bool ui_confirm_action(const char *a,const char *b,const char *c,bool d){(void)a;(void)b;(void)c;(void)d;return confirm_yes;}
''' + went_back + '\n'.join(function(pause, n) for n in ('bb_continue_input', 'bb_pause_prompt', 'bb_pause_suspend', 'bb_pause_restore')).replace('config.', 'pause_config.') + '\n' + function(gba, 'apt_event') + r'''
static int file_bytes(const char *path,unsigned char *out){FILE *f=fopen(path,"rb");if(!f)return -1;int n=(int)fread(out,1,4096,f);fclose(f);return n;}
static unsigned char kept[2][4096];static int kept_n[2];
static char top[512];
static void setup(const char *name)
{
 assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("history");
 begin();position=98;assert(bb_save_checkpoint(true));assert(bb_save_end(false));
 begin();position=99;assert(bb_save_checkpoint(true));assert(bb_save_end(false));bb_save_settle();
 kept_n[0]=file_bytes("game.state10",kept[0]);kept_n[1]=file_bytes("game.state10.bak",kept[1]);assert(kept_n[0]>0 && kept_n[1]>0);
}
static void teardown(void)   /* gba.c after the run loop: the Continue state (deferred) and the session's end */
{
 bb_save_queue();(void)bb_save_state_end(BB_SLOT_CONTINUE,true);bb_save_flush();assert(bb_save_end(false));bb_save_settle();
}
static bool same(void)
{
 unsigned char now[4096];
 for(int i=0;i<2;i++){int n=file_bytes(i?"game.state10.bak":"game.state10",now);if(n!=kept_n[i] || memcmp(now,kept[i],(size_t)n))return false;}
 return true;
}
static int ask(const char *name,const char *keys,bool yes)
{
 setup(name);script=keys;frame=0;confirm_yes=yes;
 begin();position=0;   /* the core just loaded: the boot state */
 stat_fails=next_stat_fails;
 int answer=bb_pause_prompt();
 stat_fails=NULL;
 if(answer==BB_CONTINUE_YES)position=99;   /* the Continue point loaded */
 return answer;
}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]) && getcwd(top,sizeof(top)));
 pause_config.pause=game_pause;pause_config.rebase=game_rebase;
 static const struct {const char *name,*keys;} back[]={
  {"b","..B"},{"sleep","SSSSB"},{"home","HHB"},{"close","..C"},{"sleep-close","SSC"},{"power","P"},{"sleep-power","S.SP"}};
 for(unsigned i=0;i<sizeof(back)/sizeof(*back);i++){
  int answer=ask(back[i].name,back[i].keys,false);
  if(answer!=BB_CONTINUE_BACK || !bb_continue_went_back_take()){fprintf(stderr,"%s: answer %d, not Back\n",back[i].name,answer);return 1;}
  teardown();
  if(!same()){fprintf(stderr,"%s: the Continue point or its backup changed at the unanswered question\n",back[i].name);return 1;}
 }
 /* X, then No at "Start over?": still asking; then B */
 assert(ask("x-no","XB",false)==BB_CONTINUE_BACK);teardown();assert(same());
 /* A: points are made again (the hold is let go): HOME writes the loaded game */
 assert(ask("a","SA",false)==BB_CONTINUE_YES);position=55;apt_event(APTHOOK_ONSUSPEND,NULL);
 assert(file_position("game.state10")==55);teardown();
 /* X + Start over: the old point and its backup in the backups, the new game's own from then on */
 assert(ask("over","X",true)==BB_CONTINUE_OVER);
 struct stat st;assert(stat("game.state10",&st) && stat("game.state10.bak",&st));
 hist_entry_t e[4];assert(history_list("test-test-unique",false,e,4)==2);
 position=7;teardown();assert(file_position("game.state10")==7);
 /* X + Start over while the SD card cannot say whether the backup (or the point) is there: nothing starts over,
  * the question says so and stays; B then goes back with both kept (R1b-3) */
 static const char *const eio[]={"game.state10.bak","game.state10"};
 for(int i=0;i<2;i++){
  shown_failed=false;next_stat_fails=eio[i];
  int answer=ask(i?"over-eio-point":"over-eio-backup","XB",true);next_stat_fails=NULL;
  if(answer!=BB_CONTINUE_BACK || !shown_failed || !bb_continue_went_back_take()){fprintf(stderr,"Start over with %s unreadable: answer %d, said %d\n",eio[i],answer,shown_failed);return 1;}
  teardown();
  assert(file_position("game.state10")==99 || history_list("test-test-unique",false,e,4)==1);
  assert(file_position("game.state10.bak")==98 || history_list("test-test-unique",false,e,4)==1);
  if(!i)assert(same() && history_list("test-test-unique",false,e,4)==0);
 }
 puts("Continue question (production prompt + GBA APT hook, real save service): B, sleep/wake, HOME, HOME > Close, power-off keep Continue + backup byte for byte; A makes points again; Start over keeps both in the backups, and when the SD card errs it does not start over and says so: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    answers = re.search(r'enum \{ BB_CONTINUE_ASK[^;]*;\n#define BB_CONTINUE_BUTTONS[^\n]*\n', (root / 'include/breadbox/bb_pause.h').read_text())[0]
    (td / 'bb_pause_answers.h').write_text(answers)
    src = td / 'question.c'
    src.write_text(code.replace('static int file_bytes', 'static int file_position(const char *path){FILE *f=fopen(path,"rb");if(!f)return -1;int p=fseek(f,28,SEEK_SET)?-1:fgetc(f);fclose(f);return p;}\nstatic int file_bytes'))
    sd = td / 'sd'
    sd.mkdir()
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-misleading-indentation',
                    '-DSAVE_WRITER_TEST', '-fsanitize=address,undefined', '-pthread', '-Wl,--wrap=stat', '-I' + str(td), '-Itests/save_service',
                    '-Iapp/ui', '-Iinclude/breadbox', '-Iplatform/3ds', '-Itests/gba_saves', str(src),
                    'app/ui/bb_save.c', 'app/ui/bb_final.c', 'app/ui/save_writer.c', 'app/ui/safefile.c', 'app/ui/history.c',
                    '-lz', '-o', str(td / 'question')], cwd=root, check=True)
    sys.exit(subprocess.run([str(td / 'question'), str(sd)]).returncode)
