#!/usr/bin/env python3
"""The question for a save that may be from another emulator, as the player meets it. The
production ui_confirm_pick_n() (app/ui/ui.c) and the GBA question (systems/gba/gba.c save_damaged_ask) run on
scripted frames and APT events, linked with the real bb_save.c, safe files and history on a temporary SD card,
with a 128 KiB battery and a first-seen 32 KiB .sav. B, HOME > Close, power-off, sleep/wake again and again and
A pressed twice from the screen before must leave every file byte for byte (and draw nothing once the GPU is
gone); Restore (or Back) is chosen first; only moving to "Use it" and pressing A converts. The picker runs behind
the real shared input gate (ui_swap_confirm_keys and the swap/enter code from app/ui/ui.c) at 60 frames a second,
with the menu-only mask: a press in its first UI_MOTION_CONFIRM_S never answers, nothing dims."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]


def between(text, start, end_function):
    i = text.index(start)
    m = re.search(r'(?:static )?(?:PSX3D_COLD )?(?:static )?int ' + end_function + r'\([^;{}]*?\)\s*\{', text[i:])
    assert m, end_function
    j = i + text[i:].index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[j] == '{') - (text[j] == '}')
        j += 1
    return text[i:j]


ui = (root / 'app/ui/ui.c').read_text()
gba = (root / 'systems/gba/gba.c').read_text()
picker = between(ui, 'typedef struct { const char *title, *message; ui_button_t tab[UI_BAR_MAX]; int n, lit; } pick_n_t;', 'ui_confirm_pick_n')
question = between(gba, '#define FOREIGN_TITLE', 'save_damaged_ask')


def function(name):
    m = re.search(r'(?:PSX3D_COLD )?(?:static )?(?:void|u32) ' + name + r'\([^;{}]*?\)\s*\{', ui)
    assert m, name
    j = ui.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (ui[j] == '{') - (ui[j] == '}')
        j += 1
    return ui[m.start():j].replace('PSX3D_COLD ', '')


gate = '\n'.join(function(n) for n in ('motion_now', 'swap_sound', 'ui_swap_keys', 'ui_swap_confirm_keys', 'ui_swap_enter'))
code = r'''
#define main legacy_check_main
#include "check.c"
#undef main
#include "gba_cart.h"
#include <ftw.h>
typedef uint32_t u32;typedef uint8_t u8;
#define PSX3D_COLD
#include "ui_style.h"
#define UI_BAR_MAX 6
#define UI_SWAP_MENU 0u
#define SYSCLOCK_ARM11 1000000ULL
enum {KEY_A=1,KEY_B=2,KEY_L=4,KEY_R=8,KEY_LEFT=16,KEY_RIGHT=32,KEY_CPAD_LEFT=64,KEY_CPAD_RIGHT=128,KEY_TOUCH=256};
enum {UI_ICON_RIGHT=1,UI_ICON_BACK,UI_ICON_CHECK,UI_ICON_CROSS,UI_ICON_EXIT,UI_ICON_RESCAN,UI_ICON_TOGGLE,UI_ICON_PLAY,
 UI_ICON_PLUS,UI_ICON_RESET,UI_ICON_LOAD,UI_ICON_START};
#include "ui_actions.h"
typedef struct {u32 key;const char *label;u8 icon;bool on;} ui_button_t;
/* the script: one letter a frame. A/B a press; a: A pressed and still held from the screen before; L/R left/right;
 * S sleep and wake (the loop goes on); C HOME > Close; P power-off; . nothing */
static const char *script;static unsigned frame;static unsigned long long clock_us;
/* the real gate's surroundings: a 60 Hz clock, sounds counted (tests/ui_sound/check.py plays them for real) */
static unsigned long long svcGetSystemTick(void){return clock_us;}
enum {SFX_DIALOG,SFX_MOVE,SFX_CONFIRM,SFX_DENY,SFX_BACK,SFX_SELECT,SFX_N};
static int sounds[SFX_N],dims;
static void sfx_play(int e){sounds[e]++;}
static void sfx_now(int e){sounds[e]++;}
static void sfx_default(int e){(void)e;}
static void sfx_flush(void){}
static u32 ui_resume_pending,swap_keys,swap_buffer,swap_token,swap_buffer_token,swap_buffer_tick,swap_enter_tick;
static bool gpu_right;static int draws,drawn_lit,drawn_n;
static u8 drawn_icons[UI_BAR_MAX];
static bool aptMainLoop(void){char c=script[frame];if(c=='C' || c=='P'){gpu_right=false;return false;}return c!=0;}
static void input_poll(void){}
static void dev_tick_keys(void){}
static u32 input_keys_held(void){return script[frame]=='a'?KEY_A:0;}
static u32 input_keys_down(void){char c=script[frame++];return c=='A' || c=='a'?KEY_A:c=='B'?KEY_B:c=='L'?KEY_LEFT:c=='R'?KEY_RIGHT:0;}
static int ui_tabs_hit(int n,u32 k){(void)n;(void)k;return -1;}
static void ui_begin(void){}
static void ui_menu_backdrop(void){}
static void ui_menu_header(const char *t,const char *r){(void)t;(void)r;}
static void dialog_text(const char *m,int page,bool one){(void)m;(void)page;(void)one;}
static void ui_tabs_draw(const ui_button_t *b,int n,int lit){drawn_n=n;if(!draws)drawn_lit=lit;   /* the choice when it first shows */
 for(int i=0;i<n;i++)drawn_icons[i]=b[i].icon;}
static void ui_menu_top(void *p){(void)p;}
static void video_present_vsync(bool g,void (*t)(void *),void (*d)(void *),void *p)
{(void)g;(void)t;assert(gpu_right);d(p);draws++;clock_us+=1000000/60;dims+=ui_resume_pending!=0;}
static bool dev_remote_game(void){return false;}
static bool ui_confirm_choice(const char *a,const char *b,const char *c,const char *d,bool e){(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
static void ui_format_when(char *o,size_t n,time_t t){(void)t;snprintf(o,n,"then");}
''' + gate + '\n' + picker + '\n' + question + r'''
#define BIG (128u*1024u)
static unsigned char full_ram[BIG],original[32768],good[BIG];
static bool full_battery(void **p,size_t *n){*p=full_ram;*n=BIG;return true;}
static bool native(size_t n){return gba_cart_save_fits(GBA_SAVE_UNKNOWN,GBA_FLASH_64K,n);}
static void put(const char *p,const void *b,size_t n){FILE *f=fopen(p,"wb");assert(f && fwrite(b,1,n,f)==n && !fclose(f));}
static bool exact(const char *p,const void *want,size_t n)
{
 FILE *f=fopen(p,"rb");if(!f)return false;unsigned char *b=malloc(n+1);assert(b);
 size_t got=fread(b,1,n+1,f);fclose(f);bool ok=got==n && !memcmp(b,want,n);free(b);return ok;
}
static char *listing;static size_t listing_len;
static int list_add(const char *path,const struct stat *st,int type,struct FTW *ftw)
{
 (void)ftw;if(type!=FTW_F)return 0;
 FILE *f=fopen(path,"rb");assert(f);unsigned char *b=malloc((size_t)st->st_size+1);assert(b);
 size_t n=fread(b,1,(size_t)st->st_size+1,f);fclose(f);
 char line[600];int k=snprintf(line,sizeof(line),"%s %zu %08lx\n",path,n,(unsigned long)crc32(0,b,(uInt)n));free(b);
 listing=realloc(listing,listing_len+(size_t)k+1);assert(listing);memcpy(listing+listing_len,line,(size_t)k+1);listing_len+=(size_t)k;
 return 0;
}
static char *tree(void){listing=NULL;listing_len=0;assert(!nftw(".",list_add,16,FTW_PHYS));return listing?listing:strdup("");}
static char top[512];
static void setup(const char *name,bool backup)
{
 assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("history");
 if(backup){history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,BIG));history_fake_now=0;}
 put("battery.sav",original,sizeof(original));
}
#define LEAD 20   /* 333 ms at 60 Hz: past the gate's UI_MOTION_CONFIRM_S */
static char scripted[256];
static bool start_at(const char *keys,int lead){char e[256];snprintf(scripted,sizeof(scripted),"%.*s%s",lead,"....................",keys);
 script=scripted;frame=0;clock_us+=10000000;memset(sounds,0,sizeof(sounds));dims=0;gpu_right=true;draws=0;drawn_n=drawn_lit=-1;memset(full_ram,0xa5,BIG);bool ok=bb_save_begin(&config,e,sizeof(e));assert(ok || !e[0]);return ok;}
static bool start(const char *keys){return start_at(keys,LEAD);}
static bool untouched_ram(void){for(size_t i=0;i<BIG;i++)if(full_ram[i]!=0xa5)return false;return true;}
static int fails;
#define EXPECT(c,what) do{if(!(c)){fprintf(stderr,"FAIL %s: %s\n",name,what);fails++;}}while(0)
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]) && getcwd(top,sizeof(top)));
 static bb_save_ops sized;static bb_system_t sys;
 sized=ops;sized.battery=full_battery;sized.battery_native=native;sized.battery_fill=0xff;sized.rtc_size=NULL;
 sys=test_system;sys.save=&sized;config.system=&sys;bb_save_on_damage(save_damaged_ask);
 for(size_t i=0;i<sizeof(original);i++)original[i]=(unsigned char)(i*11+5);
 memset(good,0x77,BIG);
 /* nothing changes: B, Close, power-off, sleep, A from the screen before (Restore or Back is chosen first) */
 static const struct {const char *name,*keys;bool backup;} back[]={
  {"b","..B",false},{"close","..C",false},{"power","P",false},{"sleep-b","SSSSB",false},{"sleep-close","S.S.C",false},
  {"sleep-power","S.SP",false},{"double-a","aA",false},{"double-a-later","a.A",false},{"move-back-b","LLB",false},
  {"move-close","LL.C",false},{"right-a","RRRA",false},
  {"b-backup","..B",true},{"close-backup","..C",true},{"sleep-b-backup","SSSB",true},{"move-close-backup","LLC",true},
  {"right-b-backup","RB",true},
 };
 for(unsigned i=0;i<sizeof(back)/sizeof(*back);i++){
  const char *name=back[i].name;setup(name,back[i].backup);char *before=tree();
  int lead=strchr(back[i].keys,'a')?0:LEAD;   /* A held from the screen before: the question's first frames */
  bool ok=start_at(back[i].keys,lead);char *after=tree();int drawn=draws;
  EXPECT(!ok,"started");EXPECT(!strcmp(before,after),"a file changed");EXPECT(untouched_ram(),"RAM written");
  if(drawn)EXPECT(drawn_n==(back[i].backup?4:3) && drawn_lit==2,"tabs or the safe answer chosen first");
  if(strchr(back[i].keys,'C') || strchr(back[i].keys,'P'))EXPECT(drawn==lead+(int)strcspn(back[i].keys,"CP"),"drew after the GPU was gone");
  EXPECT(!dims,"a menu question dimmed");
  EXPECT(sounds[SFX_DIALOG]==1 && sounds[SFX_CONFIRM]==(lead && strchr(back[i].keys,'A')) && sounds[SFX_DENY]==(lead && strchr(back[i].keys,'B')),"question sounds");
  free(before);free(after);
 }
 /* the real gate: A twice right after the question opens (the press that opened it, then another) answers nothing */
 static const char *const at_once[]={"A","aA",".A","A.A","........A"};
 for(unsigned i=0;i<sizeof(at_once)/sizeof(*at_once);i++){
  const char *name=at_once[i];setup(name,true);char *before=tree();
  bool ok=start_at(at_once[i],0);char *after=tree();
  EXPECT(!ok && !strcmp(before,after) && untouched_ram(),"answered inside UI_MOTION_CONFIRM_S");
  free(before);free(after);
 }
 /* the tabs: Use it, Start empty, (Restore backup), Back, each its own icon */
 {const char *name="icons";setup(name,true);start("B");
  EXPECT(drawn_icons[0]==UI_ICON_CHECK && drawn_icons[1]==UI_ICON_PLUS && drawn_icons[2]==UI_ICON_RESET && drawn_icons[3]==UI_ICON_BACK,"icons");}
 /* A on the first choice with a backup: Restore */
 {const char *name="restore";setup(name,true);
  EXPECT(start(".A") && sounds[SFX_CONFIRM]==1 && !sounds[SFX_MOVE] && full_ram[0]==0x77 && full_ram[BIG-1]==0x77 && exact("battery.sav.bad",original,sizeof(original)),"Restore");
  EXPECT(bb_save_end(false),"end");}
 /* Start empty */
 {const char *name="empty";setup(name,false);
  EXPECT(start(".LA") && sounds[SFX_MOVE]==1 && sounds[SFX_CONFIRM]==1 && untouched_ram() && exact("battery.sav.bad",original,sizeof(original)),"Start empty");EXPECT(bb_save_end(false),"end");}
 /* Use it: moved to, then A */
 for(int b=0;b<2;b++){
  const char *name=b?"use-backup":"use";setup(name,b);
  bool ok=start(b?".LLLLA":".LLA");
  EXPECT(ok && !memcmp(full_ram,original,sizeof(original)) && full_ram[sizeof(original)]==0xff && full_ram[BIG-1]==0xff,"Use it");
  hist_entry_t list[8];int n=history_list("test-test-unique",false,list,8),originals=0;
  for(int i=0;i<n;i++)originals+=exact(list[i].path,original,sizeof(original));
  EXPECT(originals==1 && n==1+b,"the original byte for byte in Backups");
  if(ok)EXPECT(bb_save_end(false),"end");
 }
 if(fails)return 1;
 puts("foreign save question (real shared gate, menu mask, 60 Hz): B, Close, power-off, sleep, A from the screen before or inside the confirm delay: no byte changed, no dim; Restore/Back chosen first; Use it only when chosen; dialog/move/confirm sounds: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    (t / 'question.c').write_text(code)
    exe = t / 'question'
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-misleading-indentation',
                    '-Wno-unused-parameter', '-DSAVE_WRITER_TEST', '-fsanitize=address,undefined', '-pthread',
                    '-I' + str(root / 'tests/save_service'), '-I' + str(root / 'app/ui'), '-I' + str(root / 'include/breadbox'),
                    '-I' + str(root / 'platform/3ds'), '-I' + str(root / 'systems/gba'), '-I' + str(root / 'tests/gba_saves'),
                    str(t / 'question.c')] + [str(root / 'app/ui' / f) for f in ('bb_save.c', 'bb_final.c', 'save_writer.c', 'safefile.c', 'history.c')]
                   + ['-lz', '-o', str(exe)], check=True)
    sd = t / 'sd'
    sd.mkdir()
    sys.exit(subprocess.run([str(exe), str(sd)]).returncode)
