#include <string.h>
#include <sys/types.h>
#include "app_policy.h"
#include "theme.h"
#include "ui_format.h"
#include "app_log.h"
#include "dev_switches.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "playlog.h"
#include "ui_actions.h"
#include "bb_pause.h"
#include "last_picture.h"
#include "bb_feature_ui.h"
#include "bb_save.h"
#include "devtools.h"
#include "postcards.h"
#include "ui_motion.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
static bb_pause_config config;
static bool paused;
static unsigned menu_row,state_slot;
static char notice[192];
static u64 notice_until;
#define menu_title config.title
static void message(const char *s){bb_pause_notice(s);sfx_play(SFX_ERROR);}   /* the card says why a press could not be done */
static bool save_state(unsigned slot){return bb_save_state(slot,true);}
static bool load_state(unsigned slot){return bb_save_state(slot,false);}
/* a board card (0..11, then Continue) -> the save service's file index (bb_slots.h) */
static unsigned file_of(int card){return card==PC_CONTINUE?BB_SLOT_CONTINUE:(unsigned)bb_slot_file(card);}

const ui_button_t bb_pause_bar[3] = {
    {KEY_B,"Resume",UI_ICON_PLAY,false}, {KEY_X,"Save",UI_ICON_SAVE,false}, {KEY_A,"Select",UI_ICON_OPEN,false}
};
#define pause_bar bb_pause_bar
#define PAUSE_BAR 3
static flist_t menu_list;
static bool undo_ready;
/* The card's rows, in the order shown: the shell's, then the system's own (Fast forward, Rewind, Sunlight...),
 * then Reset and Quit. Rebuilt whenever one of them can change. */
enum {K_RESUME=BB_PAUSE_RESUME,K_UNDO=BB_PAUSE_UNDO,K_SAVE=BB_PAUSE_SAVE,K_LOAD=BB_PAUSE_LOAD,K_SETTINGS=BB_PAUSE_SETTINGS,
 K_CONTROLS=BB_PAUSE_CONTROLS,K_CHEATS=BB_PAUSE_CHEATS,K_SYSTEM=BB_PAUSE_SYSTEM,K_RESET=BB_PAUSE_RESET,K_QUIT=BB_PAUSE_QUIT};
typedef struct {unsigned char kind,index;} pause_item;
#define PAUSE_ITEMS 24
static pause_item items[PAUSE_ITEMS];
static int nitems;
/* "Quit game": it goes back to the game's shelf, not the room (deep UI test F08) */
static const char *const kind_name[]={"Resume","Undo","Save postcard","Load postcard","Settings","Controls","Cheats","","Reset game","Quit game"};
static const unsigned char kind_icon[]={UI_ICON_PLAY,UI_ICON_BACK,UI_ICON_SAVE,UI_ICON_LOAD,UI_ICON_SLIDERS,UI_ICON_PADS,UI_ICON_STAR,UI_ICON_NONE,UI_ICON_RESET,UI_ICON_EXIT};
_Static_assert(sizeof(kind_name)/sizeof(*kind_name)==BB_PAUSE_KINDS && sizeof(kind_icon)==BB_PAUSE_KINDS,"one name and icon per shared row");
const char *bb_pause_row_name(int kind){return kind>=0 && kind<BB_PAUSE_KINDS?kind_name[kind]:"";}
unsigned char bb_pause_row_icon(int kind){return kind>=0 && kind<BB_PAUSE_KINDS?kind_icon[kind]:UI_ICON_NONE;}
static bool system_row_shown(unsigned i)
{
 const bb_pause_row *r=&config.system->pause_rows[i];
 return !r->visible || r->visible();
}
static void rows_build(void)
{
 nitems=0;
 for(int k=K_RESUME;k<=K_QUIT;k++) {
  if(k==K_UNDO && !undo_ready)continue;
  if(k==K_CONTROLS && !config.system->controls)continue;
  if(k==K_CHEATS && !config.system->cheats)continue;
  if(k==K_SYSTEM) {
   for(unsigned i=0;i<config.system->npause_rows && nitems<PAUSE_ITEMS;i++)
    if(system_row_shown(i))items[nitems++]=(pause_item){K_SYSTEM,(unsigned char)i};
   continue;
  }
  if(nitems<PAUSE_ITEMS)items[nitems++]=(pause_item){(unsigned char)k,0};
 }
}
static const bb_pause_row *system_row(int row){return &config.system->pause_rows[items[row].index];}
static int pause_count(void){return nitems;}
/* Slots used, read when the card opens and after each save (12 stat calls; nothing per frame). */
static bb_pause_slots slots_now={-1,0,-1,0};
#define used_count slots_now.used
#define free_slot slots_now.free_card
static void slots_scan(void)
{
 used_count=0;free_slot=-1;
 uint32_t present=bb_save_slots_present();   /* not a stat per slot: ~1 s on the Old (bb_save.c) */
 for(int i=0;i<PC_SLOTS;i++) {
  if(present>>file_of(i)&1)used_count++;
  else if(free_slot<0)free_slot=i;
 }
}
#define SAVED_SECONDS 3
void bb_pause_slots_saved(bb_pause_slots *s,int card){sfx_play(SFX_SAVE);s->saved_card=card;s->saved_until=svcGetSystemTick()+SAVED_SECONDS*SYSCLOCK_ARM11;}
void bb_pause_row_value(int kind,const bb_pause_slots *sl,ui_card_row_t *out)
{
 static char save_text[24];
 switch(kind) {
 case BB_PAUSE_SAVE:
  if(sl->saved_card>=0 && svcGetSystemTick()<sl->saved_until){snprintf(save_text,sizeof(save_text),"Saved");out->tone=UI_TONE_GREEN;}
  else if(sl->free_card>=0){snprintf(save_text,sizeof(save_text),"\xee\x80\x82 slot %d",bb_slot_shown(sl->free_card));   /* the X button's glyph, not the letter */out->tone=UI_TONE_BLUE;}
  else snprintf(save_text,sizeof(save_text),"All %d used",PC_SLOTS);
  out->value=save_text;break;
 case BB_PAUSE_LOAD:out->value=sl->used?">":"";out->dim=!sl->used;break;   /* dim when there is none: no count and no words */
 case BB_PAUSE_SETTINGS:case BB_PAUSE_CONTROLS:case BB_PAUSE_CHEATS:case BB_PAUSE_RESET:out->value=">";break;
 default:break;
 }
}
/* "Select + R" for hotkey i (the input's own "SELECT+R"), "" when it is off. */
const char *bb_pause_hotkey(int i)
{
 static char out[32];
 const char *name=input_hotkey_combo_name(input_hotkey_get(i));
 if(!strcmp(name,"off"))return "";
 return ui_keys_text(name,out,sizeof(out));
}
static const char *pause_label(int row,void *ud)
{
 (void)ud;
 return items[row].kind==K_SYSTEM?system_row(row)->label:kind_name[items[row].kind];
}
static void card_row(int row,void *ud,ui_card_row_t *out)
{
 (void)ud;
 pause_item it=items[row];
 out->label=pause_label(row,NULL);out->icon=kind_icon[it.kind];out->tone=UI_TONE_RED;
 bb_pause_row_value(it.kind,&slots_now,out);
 switch(it.kind) {
 case K_UNDO:out->value="just now";break;
 case K_SYSTEM: {
  const bb_pause_row *r=system_row(row);
  out->value=r->value?r->value():NULL;out->icon=r->icon;out->tone=r->tone;out->arrows=r->arrows;
  out->dim=r->enabled && !r->enabled();
  break;
 }
 default:break;
 }
}
static void draw_paused_top(void *unused); /* rendered by the shared pause component */
static void game_in_glass(void *unused);
static void draw_pause(void *unused); /* rendered by the shared pause component */
typedef struct { const char *title,*text; ui_button_t bar[2]; } bb_confirm_t;
static void draw_confirm(void *ud); /* rendered by the shared pause component */
bool bb_pause_confirm(const char *title,const char *text,const char *action) {
    bb_confirm_t c={title,text,{{KEY_B,"Cancel",UI_ICON_CROSS,false},{KEY_A,action,(u8)ui_action_icon(action,UI_ICON_CHECK),false}}};   /* the answer's own icon */
    sfx_play(SFX_DIALOG);ui_swap_enter();   /* the press that opened it never answers it */
    while(aptMainLoop()) {
        dev_tick_keys();input_poll();
        u32 k=ui_header_back(ui_bar_input(c.bar,2,input_keys_down()));
        k=ui_swap_confirm_keys(k,UI_SWAP_MENU);
        if(k&KEY_B){sfx_play(SFX_DENY);return false;}
        if(k&KEY_A){sfx_play(SFX_CONFIRM);return true;}
        ui_pause_frame();ui_begin();video_present_vsync(false,draw_paused_top,draw_confirm,&c);
    }
    return false;
}
/* The Continue question, the same for every system (deep UI test Old F3): A plays on, X starts over after
 * one more question; B goes back to the shelf with nothing played (it used to start over). */
const ui_button_t bb_continue_bar[4]={{KEY_B,"Back",UI_ICON_BACK,false},{KEY_X,"Start over",UI_ICON_START,false},{KEY_A,"Continue",UI_ICON_PLAY,false},{KEY_Y,"Older",UI_ICON_LOAD,false}};
int bb_continue_input(u32 keys,bool older)
{
 /* B and Y: the gate's back / select. A and a confirmed start over hand over to the game: their sound now. */
 if(keys&KEY_B)return BB_CONTINUE_BACK;
 if(keys&KEY_A){sfx_now(SFX_SELECT);return BB_CONTINUE_YES;}
 if(older && (keys&KEY_Y))return BB_CONTINUE_OLDER;
 if(keys&KEY_X) {
  if(ui_confirm_action("Start over?","Start the game from the beginning?","A  Start over",false)){sfx_flush();return BB_CONTINUE_OVER;}
  ui_swap_enter();
 }
 return BB_CONTINUE_ASK;
}
static bool went_back;
void bb_continue_went_back(void){went_back=true;}
bool bb_continue_went_back_take(void){bool b=went_back;went_back=false;return b;}
static struct {pc_card card;u32 saved;} continue_view;
static void draw_continue_top(void *unused); /* rendered by the shared pause component */
static void draw_continue(void *unused); /* rendered by the shared pause component */
int bb_pause_prompt(void) {
    /* the Continue point's own picture and time (the game has not drawn anything yet) */
    bb_slot_info in={0};
    uint16_t *px=linearAlloc(BB_THUMB_MAX_W*BB_THUMB_MAX_H*2);
    memset(&continue_view,0,sizeof(continue_view));
    bb_save_slot_info(BB_SLOT_CONTINUE,&in,px);
    if(px && in.w)pc_picture(&continue_view.card,px,in.w,in.h);
    if(px)linearFree(px);
    continue_view.saved=in.saved;
    int answer=BB_CONTINUE_ASK;bool over_failed=false;
    bb_save_continue_hold(true);   /* until it is answered, HOME, sleep, Close and the end keep the old one */
    ui_swap_enter();
    while(aptMainLoop() && answer==BB_CONTINUE_ASK) {
        dev_tick_keys();input_poll();
        u32 k=ui_bar_input(bb_continue_bar,BB_CONTINUE_BUTTONS(false),input_keys_down());
        k=ui_swap_keys(k,KEY_A);
        answer=bb_continue_input(k,false);
        /* Start over: the old one to the backups first; if it can't be moved nothing starts over (it stays, held) */
        if(answer==BB_CONTINUE_OVER && !bb_save_continue_aside()){answer=BB_CONTINUE_ASK;over_failed=true;}
        if(answer!=BB_CONTINUE_ASK)break;
        ui_pause_frame();ui_begin();video_present_vsync(false,draw_continue_top,draw_continue,&over_failed);
    }
    pc_card_clear(&continue_view.card);
    if(answer==BB_CONTINUE_ASK)answer=BB_CONTINUE_BACK;   /* closed while asking: nothing was chosen */
    if(answer==BB_CONTINUE_BACK)bb_continue_went_back();   /* the hold stays to the end: nothing is made */
    else if(answer==BB_CONTINUE_YES)bb_save_continue_hold(false);
    return answer;
}
/* Save state / Load state: the postcard board (postcards.h). Slot files are
 * read once when it opens; nothing touches the SD card per frame. */
static pc_view slots;
static void slot_read(pc_view *v,int i,uint16_t *px)
{
 bb_slot_info in;pc_card *c=&v->card[i];
 pc_card_clear(c);
 bb_save_slot_info(file_of(i),&in,px);
 c->used=in.used;c->saved=in.saved;c->played=in.played;
 if(in.w && px)pc_picture(c,px,in.w,in.h);
}
static int slot_bar(pc_view *v)
{
 const pc_card *c=&v->card[v->sel];int n=0;
 bool confirm=v->confirm_replace || v->confirm_delete;
 v->bar[n++]=(ui_button_t){KEY_B,confirm?"Cancel":"Back",confirm?UI_ICON_CROSS:UI_ICON_BACK,false};
 if(c->used && v->sel<PC_SLOTS)v->bar[n++]=(ui_button_t){KEY_X,"Toss",UI_ICON_TRASH,false};
 if(v->saving && v->sel<PC_SLOTS)v->bar[n++]=(ui_button_t){KEY_A,"Save",UI_ICON_SAVE,false};
 else if(!v->saving && c->used)v->bar[n++]=(ui_button_t){KEY_A,"Load",UI_ICON_LOAD,false};
 return v->nbar=n;
}
// Back returns to the same pause row.
static int slots_run(bool saving) {
    pc_view *v=&slots;
    unsigned linear_before=(unsigned)(linearSpaceFree()/1024);
    pc_open(v,saving,true,config.title,config.system->short_name);
    uint16_t *px=linearAlloc(BB_THUMB_MAX_W*BB_THUMB_MAX_H*2);   /* not the game's heap */
    for(int i=0;i<PC_CARDS;i++)slot_read(v,i,px);
    {int used=0;for(int i=0;i<PC_CARDS;i++)used+=v->card[i].used;
     log_printf("pause: %s board, %d of %d cards used, linear free %uK -> %uK (the board took %uK)\n",saving?"save":"load",used,PC_CARDS,
      linear_before,(unsigned)(linearSpaceFree()/1024),linear_before-(unsigned)(linearSpaceFree()/1024));}
    unsigned w,h;
    if(saving && px && bb_save_picture(px,&w,&h))pc_picture(&v->now,px,w,h);
    v->sel=state_slot<PC_SLOTS?(int)state_slot:0;
    if(!saving && !v->card[v->sel].used) {   // loading: start on the newest save
        int best=-1;
        for(int i=0;i<PC_CARDS;i++)if(v->card[i].used && (best<0 || v->card[i].saved>v->card[best].saved))best=i;
        if(best>=0)v->sel=best;
    }
    int result=-1,tossed=-1;
    u64 last_tick=svcGetSystemTick();
    while(true) {
        if(!aptMainLoop() || dev_tick(true)){result=-2;break;}
        input_poll();
        slot_bar(v);
        u32 k=ui_bar_input(v->bar,v->nbar,input_keys_down());
        bool hit=!pc_animating(v) && pc_input(v,&k);
        if(hit)k|=KEY_A;
        // asked again every frame until the board closes: a one-shot request was buffered during the reveal
        // after a slow load and expired (the board stayed open over the loaded game; final walk, fix-r1)
        if(result>=0 && !pc_animating(v))k|=KEY_START;
        k=ui_swap_keys(k,KEY_START);
        if(pc_animating(v))k=0;
        else if(k&KEY_START)break;   // the stamp has landed
        if(k&KEY_B) {
            if(!v->confirm_replace && !v->confirm_delete){state_slot=v->sel<PC_SLOTS?(unsigned)v->sel:state_slot;break;}
            v->confirm_replace=v->confirm_delete=false;k=0;
        }
        bool pick=(k&KEY_A)!=0;
        pc_card *c=&v->card[v->sel];
        if(pick && v->saving) {
            if(v->sel==PC_CONTINUE)pc_notice(v,"Continue is written for you when you leave the game.");
            else if(c->used && !v->confirm_replace){v->confirm_replace=true;v->confirm_delete=false;}
            else if(save_state(file_of(v->sel))) {
                state_slot=v->sel;slot_read(v,v->sel,px);
                v->confirm_replace=false;pc_animate(v,PC_ANIM_STAMP,v->sel);sfx_card_closes();result=v->sel;
            } else {v->confirm_replace=false;pc_notice(v,notice);}
        } else if(pick && c->used) {
            unsigned slot=file_of(v->sel);
            undo_ready=save_state(11);
            if(load_state(slot)){if(v->sel<PC_SLOTS)state_slot=v->sel;result=v->sel;sfx_play(SFX_LOAD);sfx_card_closes();continue;}
            pc_notice(v,notice);
        }
        if((k&KEY_X) && c->used && v->sel<PC_SLOTS) {
            if(!v->confirm_delete){v->confirm_delete=true;v->confirm_replace=false;sfx_play(SFX_MOVE);}
            else if(bb_save_delete(file_of(v->sel))){v->confirm_delete=false;tossed=v->sel;pc_animate(v,PC_ANIM_TOSS,v->sel);}
            else {v->confirm_delete=false;pc_notice(v,notice);}
        }
        if(!gspHasGpuRight()){result=-2;break;}
        u64 tick=svcGetSystemTick();
        pc_tick(v,(float)(tick-last_tick)/SYSCLOCK_ARM11);
        last_tick=tick;
        if(tossed>=0 && !pc_animating(v)){pc_card_clear(&v->card[tossed]);tossed=-1;}
        ui_pause_frame();ui_begin();video_present_vsync(false,postcards_top,postcards_bottom,v);
    }
    if(px)linearFree(px);
    pc_close(v);
    return result;
}

static int setting_row;
static flist_t settings_list;
static const ui_button_t settings_bar[]={{KEY_B,"Back",UI_ICON_BACK,false},{KEY_LEFT,"Change",UI_ICON_LEFT,false},{KEY_RIGHT,"Change",UI_ICON_RIGHT,false},{KEY_X,"Use default",UI_ICON_RESET,false}};
static const char *setting_label(int row,void *unused){(void)unused;return config.system->settings[row].label;}
static const char *setting_value(int row,void *unused)
{
 (void)unused;const bb_setting_desc *d=&config.system->settings[row];int v=bb_setting(config.system,row);
 if(d->values && v>=0 && v<d->nvalues)return d->values[v];
 if(d->kind==BB_SET_TOGGLE)return v?"On":"Off";
 static char text[24];snprintf(text,sizeof(text),"%d",v);return text;
}
static void draw_settings(void *unused);
static bool settings_run(void)
{
 if(config.system->settings_model) {
  bb_settings_context c={.game=true,.running=true,.title=config.title};
  bb_settings_run(config.system,&c,draw_paused_top,NULL);
  return !aptMainLoop();
 }
 if(config.settings)return config.settings();
 setting_row=0;settings_list.init=false;
 while(aptMainLoop()){
  if(dev_tick(true))return true;
  input_poll();u32 k=ui_list_input(&settings_list,config.system->nsettings,&setting_row,ui_bar_input(settings_bar,4,input_keys_down()),false);
  k=ui_swap_keys(k,UI_SWAP_MENU);
  if(k&KEY_B)return false;
  if((k&KEY_X) && config.system->setting_clear){config.system->setting_clear(setting_row);sfx_play(SFX_VALUE);}
  if(config.system->nsettings && (k&(KEY_A|KEY_LEFT|KEY_RIGHT)) && config.system->setting_change)
   {config.system->setting_change(setting_row,k&KEY_LEFT?-1:1);sfx_play(SFX_VALUE);}
  if(!gspHasGpuRight())return true;
  flist_update(&settings_list,setting_row,config.system->nsettings,UI_LIST_ROWS);
  ui_pause_frame();ui_begin();video_present_vsync(false,draw_paused_top,draw_settings,NULL);
 }
 return true;
}
/* X: the next free slot in one tap. All twelve used: the Save board, to pick the one to replace.
 * Returns 0 stay in the menu, 1 resume the game (a board save), 2 quit. */
static int save_now(void)
{
 /* The scan said this card is free: check its one file before writing (one stat, only on X), so a
  * scan that missed a file can never put a new postcard over an old one. Used: the board decides. */
 if(free_slot>=0 && !bb_save_absent(file_of(free_slot)))free_slot=-1;   /* there, or unsure (SD error) */
 if(free_slot<0){sfx_play(SFX_SELECT);int result=slots_run(true);return result==-2?2:result>=0?1:0;}
 if(save_state(file_of(free_slot))) {
  state_slot=(unsigned)free_slot;bb_pause_slots_saved(&slots_now,free_slot);
  notice_until=0;   // the value says "Saved" and the picture is stamped: no line of red handwriting as well
  slots_scan();
 } else sfx_play(SFX_ERROR);   // bb_save_state left its reason in the notice
 return 0;
}
/* The game's frame when the player quits from the card (the save service's picture, as a postcard takes it). */
static uint16_t *quit_px;
static unsigned quit_w,quit_h;
static void quit_picture_drop(void){if(quit_px)linearFree(quit_px);quit_px=NULL;}
static void quit_picture_take(void)
{
 quit_picture_drop();
 quit_px=linearAlloc(BB_THUMB_MAX_W*BB_THUMB_MAX_H*2);   /* not the game's heap */
 if(quit_px && !bb_save_picture(quit_px,&quit_w,&quit_h))quit_picture_drop();
}
bool bb_pause_last_picture(const char *game_path)
{
 if(!quit_px)return false;
 bb_last_picture_save(game_path,quit_px,(int)quit_w,(int)quit_h,(int)quit_w);
 quit_picture_drop();
 return true;
}
bool bb_pause_run(void)
{
    paused=true;
    u32 opening_keys=input_keys_held() & (KEY_X|KEY_TOUCH|KEY_A|KEY_B), pending_keys=0;

    config.pause(true);
    // The game's own save, then the Continue point of this moment: taken now (no SD access), written by the
    // later thread once the card's first frame is up, so a crash or flat battery after a pause loses nothing.
    // Writing it first held the card back ~3 s on the Old (deep UI test Old F2; the PS1 card opens in 0.6 s).
    bool continue_waiting=bb_save_flush() && bb_save_pause_take();
    bool quit=false,remote_exit=false,leaving=false;
    slots_now.saved_card=-1;slots_scan();rows_build();
    log_printf("pause: card open, %d rows, linear free %uK, heap free %uK\n",nitems,(unsigned)(linearSpaceFree()/1024),(unsigned)(bb_heap_available()/1024));
    if(menu_row>=(unsigned)nitems)menu_row=0;
    ui_card_update(&menu_list,menu_row,pause_count());
    bb_pause_scene_set(menu_title,config.system->picture_box,config.draw_game?game_in_glass:NULL);
    if(!ui_pause_open()){quit=true;goto done;}
    while(true) {
        if(!aptMainLoop()) {quit=true;break;}
        if(dev_tick(true))remote_exit=true;
        input_poll();
        bool remote_resume=dev_ui_pending()==DEV_UI_RESUME;
        if(remote_resume)dev_ui_take();
        // Latch every edge while the card opens, including X/A/B/touch. Only the
        // initiating hold is suppressed; dispatch touch now while its position is current.
        opening_keys &= input_keys_held();
        pending_keys |= input_keys_down() & ~opening_keys;
        u32 k=ui_bar_input(pause_bar,PAUSE_BAR,pending_keys);
        if(remote_resume)k|=KEY_B;
        k=ui_swap_pause_keys(k);
        if(remote_exit && !quit) {k|=UI_SWAP_REMOTE;quit=true;}
        pending_keys=0;
        int row=menu_row;
        k=ui_card_input(&menu_list,pause_count(),&row,k);
        menu_row=row;
        u32 swaps=KEY_B|UI_SWAP_REMOTE;   /* back to the game, or out of it; every other row opens a menu at once */
        pause_item selected=items[menu_row];
        if(selected.kind==K_RESUME || selected.kind==K_QUIT || (selected.kind==K_UNDO && undo_ready))swaps|=KEY_A;
        k=ui_swap_keys(k,swaps);
        if(k&UI_SWAP_REMOTE){leaving=quit;goto done;}
        if(k&KEY_B) break;
        if(k&KEY_X) {int r=save_now();if(r==2){quit=true;goto done;}if(r==1)goto done;}
        pause_item it=items[menu_row];
        if(it.kind==K_SYSTEM && (k&(KEY_LEFT|KEY_RIGHT))) {
            const bb_pause_row *extra=system_row(menu_row);
            if(extra->arrows && extra->activate && extra->activate(k&KEY_LEFT?-1:1)) {sfx_play(SFX_VALUE);pending_keys|=KEY_B;continue;}
        }
        if(k&KEY_A) {
            switch(it.kind) {
            case K_RESUME: goto done;
            case K_UNDO: if(undo_ready && load_state(11)) {undo_ready=false;goto done;} else message("Nothing to undo yet."); break;
            case K_SAVE: case K_LOAD: {
                if(it.kind==K_LOAD && !used_count){message(UI_PC_NOTHING_SAVED);break;}
                int result=slots_run(it.kind==K_SAVE);
                if(result==-2){quit=true;goto done;}
                if(result>=0)goto done;
                slots_scan();
                break;
            }
            case K_SETTINGS: if(settings_run()){quit=true;goto done;} break;
            case K_RESET: if(bb_pause_confirm("Reset game?","Restart from the beginning? Undo in the menu can restore this moment.","Reset game")) {
                undo_ready=save_state(11);bb_save_pause_over();
                if(config.reset)config.reset();
                message("Game reset. Undo it in the menu.");goto done;
            } break;
            case K_QUIT: quit=leaving=true;goto done;   // the pause's Continue point is the last one (no second)
            case K_CONTROLS: if(bb_controls_ui(&bb_active_controls,true)){quit=true;goto done;} break;
            case K_CHEATS: if(bb_cheats_ui(config.system->cheats,true)){quit=true;goto done;} break;
            case K_SYSTEM: {
                const bb_pause_row *extra=system_row(menu_row);
                if(extra->activate && extra->activate(1)) {pending_keys|=KEY_B;continue;}
                sfx_cancel();   // a row that cannot act now: the press did nothing
                break;
            }
            }
        }
        if(!gspHasGpuRight()){quit=true;goto done;}
        rows_build();
        if(menu_row>=(unsigned)nitems)menu_row=nitems-1;
        ui_card_update(&menu_list,menu_row,pause_count());
        ui_pause_frame();
        ui_begin(); video_present_vsync(false,draw_paused_top,draw_pause,NULL);
        if(continue_waiting){continue_waiting=false;log_printf("pause: first frame up, Continue point to the SD card\n");bb_save_pause_put();}
        if(sw_framelog)log_printf("frame: pause t=%ums row=%u hl=%.3f lift=%.3f rows=%d\n",(unsigned)(svcGetSystemTick()/(SYSCLOCK_ARM11/1000)),
            menu_row,menu_list.hl,menu_row<FLIST_MAX?menu_list.lift[menu_row]:0.0f,nitems);   /* dev: what each presented card showed */
    }
done:
    if(continue_waiting)bb_save_pause_put();   /* left before the first frame */
    bb_pause_scene_set(NULL,NULL,NULL);
    log_printf("pause: card closed, linear free %uK, heap free %uK\n",(unsigned)(linearSpaceFree()/1024),(unsigned)(bb_heap_available()/1024));
    if(quit) {
        // the game's own picture for the room's TV and its Continue card (bb_pause_last_picture): the top screen
        // holds the dimmed pause scene now (deep UI test Old F1: a black TV after quitting a GBA game)
        if(leaving)quit_picture_take();
        ui_pause_close();
    }
    else if(ui_pause_close()) {
        config.pause(false);bb_save_pause_over();   // the game runs again: its end makes a new Continue point
        // the held game picture goes up now: the dim lifts from this frame, not after the next one has been emulated
        if(config.draw_game){ui_begin();video_present_vsync(false,config.draw_game,ui_game_blank,NULL);}
    } else quit=true;
    ui_pause_motion(1);

    paused=false;menu_row=0;   // the card opens on Resume next time
    return quit;
}

void bb_pause_begin(const bb_pause_config *c)
{
 quit_picture_drop();
 config=*c;menu_row=state_slot=0;paused=undo_ready=false;menu_list.init=false;notice_until=0;
}
void bb_pause_notice(const char *s){snprintf(notice,sizeof(notice),"%s",s);notice_until=svcGetSystemTick()+3ULL*SYSCLOCK_ARM11;}
bool bb_pause_quick(bool save)
{
 if(!save && !bb_save_exists(BB_SLOT_QUICK)){message("No quick save yet.");return false;}
 config.pause(true);
 bool ok;
 if(save)ok=save_state(BB_SLOT_QUICK);
 else {
  undo_ready=save_state(BB_SLOT_UNDO);
  ok=undo_ready && load_state(BB_SLOT_QUICK);
 }
 if(config.rebase)config.rebase();
 config.pause(false);
 if(ok)message(save?"Quick saved.":"Quick loaded. Undo is available in Pause.");
 else message("Could not save or load. Check the SD card.");
 return ok;
}
bool bb_pause_active(void){return paused;}
void bb_pause_reset(void){bb_save_pause_over();if(config.reset)config.reset();}
bool bb_pause_suspend(bool make_continue)
{config.pause(true);return bb_save_checkpoint(make_continue);}
void bb_pause_restore(void)
{if(config.rebase)config.rebase();if(!paused)config.pause(false);}
#include "components/bb_pause.inc"
