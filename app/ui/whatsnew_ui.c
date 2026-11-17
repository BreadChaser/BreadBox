#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cold_code.h"
#include "app_config.h"
#include "app_log.h"
#include "app_input.h"
#include "devtools.h"
#include "platform_video.h"
#include "uisound.h"
#include "ui.h"
#include "ui_pause.h"
#include "ui_card_scroll.h"
#include "ui_fixture.h"
#include "bb_settings.h"
#include "bb_update.h"
#include "whatsnew.h"
#include "whatsnew_text.h"

static void (*room_present)(void (*)(void *));
typedef struct {ui_fit_line *lines;int count;flist_t *list;ui_card_scroll_t scroll;char title[96];} notes_card;
static notes_card *current;
static notes_card notes;
static const ui_button_t bar[]={ {KEY_A,"Done",UI_ICON_CHECK,false} };

/* Copy one highlight to each ruled line; generation already enforces count and width. */
static PSX3D_COLD int prepare(char *text,ui_fit_line *out)
{
 int count=0;char *next;
 for(char *s=text;s && *s;s=next) {
  next=strchr(s,'\n');if(next)*next++=0;
  if(strlen(s)>=UI_FIT_LINE_MAX || ui_text_width(NULL,UI_CARD_ROW_SIZE,s)>
     UI_CARD_X+UI_CARD_W-UI_CARD_RIGHT-UI_CARD_TEXT_X-UI_CARD_NUDGE)return 0;
  if(out)strcpy(out[count],s);
  count++;
 }
 return count;
}

static PSX3D_COLD void row(int i,void *ud,ui_card_row_t *r)
{notes_card *c=ud;r->label=c->lines[i];}

#include "components/whatsnew.inc"

static PSX3D_COLD void bottom(void *ud)
{
 (void)ud;notes_card *c=current;
 ui_card_t card={.title=c->title};
 notes_paper(c->list,c->count,-1,&card,bar,1);
}

static PSX3D_COLD void top(void *ud)
{(void)ud;if(!bb_settings_child_top())ui_menu_top(NULL);}

PSX3D_COLD void bb_whatsnew_prepare(void)
{
 if(notes.lines || !BB_CHANGELOG_TEXT[0])return;
 char *text=strdup(BB_CHANGELOG_TEXT);if(!text)return;
 notes_card c={0};c.count=prepare(text,NULL);
 c.lines=calloc((size_t)c.count,sizeof(*c.lines));
 if(!c.lines || !c.count){free(c.lines);free(text);return;}
 strcpy(text,BB_CHANGELOG_TEXT);prepare(text,c.lines);free(text);
 snprintf(c.title,sizeof(c.title),"What's new in %s",BB_CHANGELOG_VERSION);
 notes=c;
}

PSX3D_COLD void bb_whatsnew_exit(void)
{free(notes.lines);notes=(notes_card){0};}

PSX3D_COLD const char *bb_whatsnew_label(void){return "What's new";}

PSX3D_COLD void bb_whatsnew_open(void)
{
 if(config_in_game() || !notes.lines)return;
 (void)bb_whatsnew_take();
 flist_t list={0};notes_card c=notes;c.list=&list;
#ifdef BB_UI_FIXTURES
 ui_fit_line fixture[12];int n=ui_fixture_notes();
 if(n){for(int i=0;i<n;i++)snprintf(fixture[i],sizeof(fixture[i]),"Highlight %d",i+1);c.lines=fixture;c.count=n;}
#endif
 current=&c;log_printf("whatsnew: opened %s (%d lines)\n",BB_CHANGELOG_VERSION,c.count);
 ui_swap_enter();
 while(aptMainLoop()) {
  if(dev_tick(false) || dev_ui_pending())break;
  input_poll();u32 k=ui_swap_keys(ui_bar_input(bar,1,input_keys_down()),UI_SWAP_MENU);
  if(k&(KEY_A|KEY_B)){sfx_play(SFX_SELECT);break;}
  ui_card_scroll_input(&c.scroll,c.list,c.count,k,input_contact());
  music_update();if(gspHasGpuRight()){if(room_present)room_present(bottom);else {ui_begin();video_present_vsync(false,top,bottom,&c);}}
 }
 current=NULL;log_printf("whatsnew: closed\n");
}

PSX3D_COLD void bb_whatsnew_room(int screen,bool ready,void (*present)(void (*)(void *)))
{
 room_present=present;
 if(screen==DEV_UI_WHATSNEW || (ready && bb_update_startup_ready() && bb_whatsnew_take()))bb_whatsnew_open();
}
