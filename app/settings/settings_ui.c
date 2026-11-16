/* Settings: one list per page and a page stack, no tabs (owner 2026-11-12: "have it all in the settings list ...
 * the systems need to be in its own sub menu because we will add more"). Adapters own the values; the shell owns
 * the pages, the shared card and the app's own rows.
 *
 *   Settings     Customize >  Systems >  Collection >  Updates >  About >
 *   Customize    Room style, Hands > (roomhands), FF keys, Rewind keys (New), Touch buttons (New: their side)
 *   Systems      one row per registered system with settings (bb_systems[])
 *   <system>     its rows under red section headings (L / R jump between them), then Advanced > and Help
 *   Advanced     the system's Speed / Compatibility / Troubleshooting rows (bb_settings_row.advanced), Help
 *   Collection   Games folder, Import again from PSX3D
 *   About        Credits, How to install, Welcome (when the welcome tour is built in)
 * A game's own Settings (shelf or pause) opens straight on its system page, titled with the game. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "theme.h"
#include "ui_format.h"
#include "ui_fit.h"
#include "app_paths.h"
#include "app_config.h"
#include "settings_ui.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "libui.h"
#include "hand_styles.h"
#include "app_main.h"
#include "bb_settings.h"
#include "bb_update.h"
#include "bb_registry.h"
#include "bb_feature_ui.h"
#include "breadbox_import.h"
#include "breadbox_credits.h"
#include "control_model.h"
#include "room.h"
#include "devtools.h"
#include "ui_motion.h"
#include "app_log.h"
#include "dev_switches.h"
#define SETTINGS_ROWS 192
#define SETTINGS_DEPTH 4
/* Row ids the shell owns (adapters use 0..9999). A system's row on the Systems page: APP_SYSTEM + registry index. */
enum { APP_CUSTOMIZE=10000,APP_SYSTEMS,APP_COLLECTION,APP_UPDATES,APP_ABOUT,
 APP_APPEARANCE,APP_HANDS,APP_SKIN,APP_GLOVES,APP_HOTKEY,APP_HOTKEY_LAST=APP_HOTKEY+INPUT_HOTKEY_N-1,APP_SIDE,
 APP_FOLDER,APP_IMPORT,APP_CREDITS,APP_INSTALL,APP_WELCOME,APP_ADVANCED,ACTION_HELP,APP_SYSTEM=11000 };
typedef enum { PAGE_TOP,PAGE_CUSTOMIZE,PAGE_HANDS,PAGE_SYSTEMS,PAGE_SYSTEM,PAGE_ADVANCED,PAGE_COLLECTION,PAGE_ABOUT,PAGE_COUNT } settings_page;
static const char *const page_titles[PAGE_COUNT]={"Settings","Customize","Hands","Systems",NULL,"Advanced","Collection","About"};
typedef struct {settings_page page;const bb_system_t *system;int selected;} settings_level;
typedef struct {
 bb_settings_row row;
 char label[128],value[160],help[1024],section[48],notice[192];
} settings_cached_row;
typedef struct {
 settings_level levels[SETTINGS_DEPTH];int depth;   /* levels[depth-1] is on the card */
 int remembered[PAGE_COUNT+BB_MAX_SYSTEMS];        /* the row each page was left on: a second visit is one press */
 settings_cached_row *rows;int count,help_focus;
 flist_t list;   /* the shown page's motion (one: a page reopens on its remembered row, not its scroll) */
 char folder_value[PATH_LEN];   /* one full path, in the existing static state; no per-row heap growth */
 bb_settings_context context;const bb_settings_ops *ops;const bb_system_t *ops_system;
 bool theme_picker;int value_page;
 void (*top)(void *);void *top_ud;
 const ui_destination *scene;ui_destination backdrop;float scene_time,scene_visibility,page_time;
 char scope[192],source[96],title[128];   /* title: the game's clean name (ui_clean_title), as the library and pause card say it */
 flist_t *appearance_list;int appearance_selected,appearance_previous;
} settings_state;
static bool active,debug_capture,folders_requested,hands_preview;
static settings_state state;
/* The welcome tour (welcome.h) defines it; About shows its row only when it is built in. True: the player pressed X
 * on the games card, so Settings closes for the games-folder picker, as its Games folder row does. */
extern bool bb_welcome_open(void) __attribute__((weak));
static settings_level *level(settings_state *s){return &s->levels[s->depth-1];}
const char *bb_settings_source_name(bb_setting_source source)
{
 switch(source){case BB_SOURCE_SYSTEM:return "System";case BB_SOURCE_RECOMMENDED:return "Recommended";
 case BB_SOURCE_CHANGED:return "Changed";case BB_SOURCE_ALL:return "All systems";default:return "";}
}
static void append(settings_state *s,bb_settings_row row)
{
 if(s->count>=SETTINGS_ROWS)return;
 settings_cached_row *r=&s->rows[s->count++];r->row=row;
 /* whole UTF-8 characters only: a long multibyte folder name is never cut inside a character */
 ui_copy_text(r->label,sizeof(r->label),row.label?row.label:"");r->row.label=r->label;
 char *value=row.id==APP_FOLDER?s->folder_value:r->value;
 size_t value_cap=row.id==APP_FOLDER?sizeof(s->folder_value):sizeof(r->value);
 ui_copy_text(value,value_cap,row.value?row.value:"");r->row.value=value;
 ui_copy_text(r->help,sizeof(r->help),row.help?row.help:"");r->row.help=r->help;
 ui_copy_text(r->section,sizeof(r->section),row.section?row.section:"");r->row.section=r->section;
 ui_copy_text(r->notice,sizeof(r->notice),row.notice?row.notice:"");r->row.notice=r->notice;
}
static void heading(settings_state *s,const char *name)
{append(s,(bb_settings_row){.id=-s->count-1,.label=name,.section=name,.kind=BB_ROW_HEADING});}
static void page_row(settings_state *s,int id,const char *label,const char *help)
{append(s,(bb_settings_row){.id=id,.label=label,.value=">",.help=help,.kind=BB_ROW_ACTION});}
/* the first row from `from` on with help to read (a setting, not a page or the Help row itself), -1 none */
static int help_row(const settings_state *s,int from)
{
 for(int i=from;i<s->count;i++) {
  const bb_settings_row *r=&s->rows[i].row;
  if(r->kind!=BB_ROW_HEADING && r->id!=ACTION_HELP && r->value && strcmp(r->value,">") && r->help && r->help[0])return i;
 }
 return -1;
}
static const char *hotkey_notice(int slot,char *text,size_t cap)
{
 const char *exception=bb_settings_hotkey_exception(slot);if(!exception)return NULL;
 snprintf(text,cap,"All systems · Game exception: %s",exception);return text;
}
static void customize_rows(settings_state *s)
{
 append(s,(bb_settings_row){.id=APP_APPEARANCE,.label="Room style",.value=theme()->name,.help="How the room is lit: Day or Night. Left and right change it at once; A opens the styles with the room showing each.",.kind=BB_ROW_VALUE});
 if(room_show_hands)page_row(s,APP_HANDS,"Hands","Choose a skin tone and gloves for the hands in the room.");
 static const char *const keys[INPUT_HOTKEY_N]={"FF keys","Rewind keys"};
 for(int h=0;h<INPUT_HOTKEY_N;h++) {
  if(h && !g_new3ds)continue;   /* rewinding needs the New 3DS's memory */
  char combo[32],notice[160];
  append(s,(bb_settings_row){.id=APP_HOTKEY+h,.label=keys[h],.value=ui_keys_text(input_hotkey_combo_name(bb_settings_hotkey_get(h)),combo,sizeof(combo)),
   .help=h?"Applies to all systems. Hold these buttons together to rewind. Existing game exceptions are kept.":"Applies to all systems. Press these buttons together to fast-forward. Existing game exceptions are kept.",
   .notice=hotkey_notice(h,notice,sizeof(notice)),.kind=BB_ROW_VALUE,.source=BB_SOURCE_ALL,.can_clear=true});
 }
 if(g_new3ds)append(s,(bb_settings_row){.id=APP_SIDE,.label="Touch buttons",.value=control_side()?"Left side":"Right side",
  .help="Applies to all systems. Choose the side of the touch buttons in a game; the menu button and logo move to the other side.",
  .kind=BB_ROW_VALUE,.source=BB_SOURCE_ALL,.can_clear=true});
}
/* A system's rows: the adapter's (one page or its Advanced page), under a heading wherever the section changes. */
static void system_rows(settings_state *s,bool advanced)
{
 const char *previous="";char section[48]="";bool more=false;
 for(unsigned i=0;i<s->ops->count();i++) {
  bb_settings_row row;if(!s->ops->read(i,&row))continue;
  assert(row.id>=0 && row.id<APP_CUSTOMIZE);
  if(row.advanced!=advanced){more|=row.advanced;continue;}
  if(row.section && strcmp(previous,row.section)) {
   snprintf(section,sizeof(section),"%s",row.section);previous=section;heading(s,section);
  }
  append(s,row);
 }
 if(more || help_row(s,0)>=0)heading(s,"More");
 if(more)page_row(s,APP_ADVANCED,"Advanced","Speed, compatibility and troubleshooting options. Tested games keep their tested values.");
}
/* A registered system without a model adapter: its descriptor rows (a new system appears by itself). */
static void descriptor_rows(settings_state *s,const bb_system_t *sys)
{
 for(unsigned i=0;sys && i<sys->nsettings;i++) {
  const bb_setting_desc *d=&sys->settings[i];int v=sys->setting_value?sys->setting_value(i):0;
  if(!(d->models&(g_new3ds?BB_MODEL_NEW:BB_MODEL_OLD)) || (d->flags&BB_SET_GAME_ONLY))continue;
  const char *value=d->kind==BB_SET_TOGGLE?(v?"On":"Off"):d->values && v>=0 && (unsigned)v<d->nvalues?d->values[v]:"";
  append(s,(bb_settings_row){.id=(int)i,.label=d->label,.value=value,.help=d->help,.kind=d->kind==BB_SET_ACTION?BB_ROW_ACTION:BB_ROW_VALUE,.source=d->kind==BB_SET_ACTION?BB_SOURCE_NONE:BB_SOURCE_SYSTEM,.can_clear=d->kind!=BB_SET_ACTION && sys->setting_clear!=NULL});
 }
}
static bool has_settings(const bb_system_t *sys){return sys->settings_model || sys->nsettings;}
/* A system's row on the Systems page: its name, or its short name where the name would be cut. */
static const char *system_label(const bb_system_t *sys)
{
 ui_card_row_t r={.label=sys->name,.value=">"};
 return ui_card_row_cut(&r) && sys->short_name?sys->short_name:sys->name;
}
static void build_rows(settings_state *s)
{
 s->count=0;settings_level *l=level(s);
 switch(l->page) {
 case PAGE_TOP:
  page_row(s,APP_CUSTOMIZE,"Customize","The room's style and the shortcuts every system shares.");
  page_row(s,APP_SYSTEMS,"Systems","Picture, controls and saves for each game system.");
  page_row(s,APP_COLLECTION,"Collection","Where your games are, and importing from PSX3D.");
  page_row(s,APP_UPDATES,"Updates","Downloads keep going through Settings. Return to the room or reopen BreadBox to install an accepted update. Below 10% battery, plug in the charger. Interrupted updates retry the next time you open BreadBox.");
  page_row(s,APP_ABOUT,"About","Credits, licenses and how to install BreadBox.");
  break;
 case PAGE_CUSTOMIZE:customize_rows(s);break;
 case PAGE_HANDS:
  append(s,(bb_settings_row){.id=APP_SKIN,.label="Skin tone",.value=hand_skin_name[g_cfg.hand_skin],.help="The skin tone of the hands that take your games off the shelf.",.kind=BB_ROW_VALUE});
  append(s,(bb_settings_row){.id=APP_GLOVES,.label="Gloves",.value=hand_glove_name[g_cfg.hand_glove],.help="Choose gloves for your hands in the room.",.kind=BB_ROW_VALUE});
  break;
 case PAGE_SYSTEMS:
  for(unsigned i=0;i<bb_system_count;i++)if(has_settings(bb_systems[i]))page_row(s,APP_SYSTEM+(int)i,system_label(bb_systems[i]),"This system's picture, controls and saves.");
  break;
 case PAGE_SYSTEM:case PAGE_ADVANCED:
  if(s->ops)system_rows(s,l->page==PAGE_ADVANCED);else descriptor_rows(s,l->system);
  if(help_row(s,0)>=0){if(!s->ops)heading(s,"More");page_row(s,ACTION_HELP,"Help","Read the full explanation of the setting you last chose.");}
  break;
 case PAGE_COLLECTION: {
  char folder[PATH_LEN];snprintf(folder,sizeof(folder),"%s",g_cfg.games_dir);
  size_t len=strlen(folder);while(len>1 && folder[len-1]=='/')folder[--len]=0;
  append(s,(bb_settings_row){.id=APP_FOLDER,.label="Games folder",.value=*folder?folder:"None",.help="Choose one folder for all systems, including subfolders. Hold Y at the bookcase to rescan the games.",.kind=BB_ROW_ACTION});
  page_row(s,APP_IMPORT,"Import again from PSX3D","Copy memory cards, postcards, pictures and settings safely. Conflicts keep both copies. PSX3D is never changed.");
 } break;
 case PAGE_ABOUT:
  page_row(s,APP_CREDITS,"Credits","Open the full version, authors, emulator cores, artwork and license notices.");
  page_row(s,APP_INSTALL,"How to install","Open the shared installation help.");
  if(bb_welcome_open)page_row(s,APP_WELCOME,"Welcome","Take the short tour of BreadBox again.");
  break;
 default:break;
 }
 if(l->selected>=s->count)l->selected=s->count?s->count-1:0;
 while(l->selected<s->count-1 && s->rows[l->selected].row.kind==BB_ROW_HEADING)l->selected++;
 if(s->help_focus>=s->count || s->help_focus<0)s->help_focus=l->selected;
}
static int memory_slot(const settings_level *l)
{
 if(l->page!=PAGE_SYSTEM || !l->system)return l->page;
 for(unsigned i=0;i<bb_system_count;i++)if(bb_systems[i]==l->system)return PAGE_COUNT+(int)i;
 return l->page;
}
/* Opens or closes the system model as the stack enters or leaves a system's pages. */
static bool system_open(settings_state *s,const bb_system_t *sys)
{
 if(s->ops_system==sys)return true;
 if(s->ops && s->ops->close)s->ops->close();
 s->ops=NULL;s->ops_system=sys;
 if(!sys)return true;
 if(sys->settings_model) {
  if(!sys->settings_model->open(&s->context)){s->ops_system=NULL;return false;}
  s->ops=sys->settings_model;
 } else if(sys->settings_open)sys->settings_open(s->context.game?s->context.key:NULL);
 return true;
}
static bool push(settings_state *s,settings_page page,const bb_system_t *sys)
{
 if(s->depth>=SETTINGS_DEPTH)return false;
 if(page==PAGE_ADVANCED)sys=level(s)->system;
 if((page==PAGE_SYSTEM || page==PAGE_ADVANCED) && !system_open(s,sys))return false;
 if(s->depth)s->remembered[memory_slot(level(s))]=level(s)->selected+1;
 settings_level *l=&s->levels[s->depth++];
 *l=(settings_level){.page=page,.system=sys};s->list=(flist_t){0};
 int slot=memory_slot(l);l->selected=s->remembered[slot]?s->remembered[slot]-1:0;
 hands_preview=page==PAGE_HANDS;
 s->value_page=0;s->help_focus=-1;s->page_time=0;
 build_rows(s);return true;
}
/* B: one level up. False at the root (Settings closes). */
static bool pop(settings_state *s)
{
 if(s->depth<=1)return false;
 s->remembered[memory_slot(level(s))]=level(s)->selected+1;
 s->depth--;s->list=(flist_t){0};
 if(level(s)->page!=PAGE_SYSTEM && level(s)->page!=PAGE_ADVANCED)system_open(s,NULL);
 hands_preview=level(s)->page==PAGE_HANDS;s->value_page=0;s->help_focus=-1;
 build_rows(s);return true;
}
static const char *page_title(const settings_state *s)
{
 const settings_level *l=&s->levels[s->depth-1];
 if(l->page==PAGE_SYSTEM)return s->context.game && s->depth==1?s->title:l->system?l->system->name:"";
 return page_titles[l->page];
}
static void full_help(settings_state *s)
{
 int i=s->help_focus;
 if(i<0 || i>=s->count || !s->rows[i].row.help || !s->rows[i].row.help[0])i=help_row(s,0);   /* never an empty page */
 if(i<0)return;
 bb_settings_row *r=&s->rows[i].row;char text[1400];
 if(r->source==BB_SOURCE_NONE)snprintf(text,sizeof(text),"%s",r->help);   /* no source line, no blank lines above the text */
 else snprintf(text,sizeof(text),"%s%s%s\n\n%s",bb_settings_source_name(r->source),r->system_changed?" default, changed":"",r->source==BB_SOURCE_ALL?" · Applies to all systems":"",r->help);
 ui_help_pages(r->label,text,s->context.running);
}
/* Y on the page that owns the scope: Customize (and Hands) = room preferences and the shared shortcuts;
   a system's pages = that system, or this game. Nowhere else. */
static bool reset_scope(const settings_state *s)
{settings_page p=s->levels[s->depth-1].page;return p==PAGE_CUSTOMIZE || p==PAGE_HANDS || ((p==PAGE_SYSTEM || p==PAGE_ADVANCED) && s->ops);}
static void reset(settings_state *s)
{
 settings_page p=level(s)->page;bool app=p==PAGE_CUSTOMIZE || p==PAGE_HANDS;
 const char *name=app?"Reset Customize?":s->context.game?"Reset this game?":"Reset system defaults?";
 const char *scope=app?"The Day room, the default hands, fast-forward and rewind keys and bottom-screen buttons come back. Games and saves are kept."
  :s->context.game?"This game's own settings and controls are removed. Saves and postcards are kept."
  :"This system's settings and controls go back to the defaults. Saves and postcards are kept.";
 char text[768];
 if(app)snprintf(text,sizeof(text),"%s",scope);
 else snprintf(text,sizeof(text),"%s\n\n%s",s->context.game?s->title:level(s)->system->name,scope);
 if(!ui_confirm_action(name,text,"A  Reset",s->context.running))return;
 if(app) {
  config_room_defaults();
  for(int i=0;i<INPUT_HOTKEY_N;i++)bb_settings_hotkey_default(i);
  control_side_set(0);input_touch_reset();config_save();
 } else s->ops->reset(BB_RESET_SCOPE);
}
/* The room's style (Day, Night): the room behind takes the highlighted one at once; B keeps the old one. */
static void appearance(settings_state *s)
{
 int before=g_cfg.theme,selected=before;flist_t list={0};ui_swap_enter();
 while(aptMainLoop()) {
  if(dev_tick(false) || dev_ui_pending())break;
  input_poll();u32 k=ui_list_input(&list,theme_count(),&selected,input_keys_down(),false);
  k=ui_swap_keys(k,UI_SWAP_MENU);
  g_cfg.theme=selected;theme_menu_refresh();
  if(k&KEY_B)break;
  if(k&KEY_A){before=selected;config_user_changed("theme");break;}
  flist_update(&list,selected,theme_count(),UI_LIST_ROWS);
  s->theme_picker=true;
  /* Existing picker is the shared appearance component. */
  extern void settings_appearance_present(settings_state *,flist_t *,int,int);
  settings_appearance_present(s,&list,selected,before);
 }
 s->theme_picker=false;g_cfg.theme=before;theme_menu_refresh();if(aptMainLoop())config_save();ui_swap_enter();
}
static int settings_bar(settings_state *s,ui_button_t *bar)
{
 int n=0;bar[n++]=(ui_button_t){KEY_B,"Back",UI_ICON_BACK,false};
 if(!s->count){bar[n++]=(ui_button_t){KEY_A,"Open",UI_ICON_CHANGE,false};return n;}
 bb_settings_row *focused=&s->rows[level(s)->selected].row;
 if(ui_value_card_pages(focused->label,focused->value)>1)bar[n++]=(ui_button_t){KEY_SELECT,"Next",UI_ICON_RIGHT,false};
 if(reset_scope(s))bar[n++]=(ui_button_t){KEY_Y,"Reset",UI_ICON_TRASH,false};   /* the whole scope: not the X arrow that restores one value */
 if(focused->can_clear)bar[n++]=(ui_button_t){KEY_X,s->context.game?"Inherit":"Default",UI_ICON_RESET,false};
 bar[n++]=(ui_button_t){KEY_A,focused->kind==BB_ROW_VALUE?"Change":"Open",UI_ICON_CHANGE,false};return n;
}
static void settings_value_input(settings_state *s,u32 keys)
{
 if(!(keys&KEY_SELECT) || !s->count)return;
 bb_settings_row *row=&s->rows[level(s)->selected].row;
 int pages=ui_value_card_pages(row->label,row->value);
 s->value_page=(s->value_page+1)%pages;
 if(pages>1)sfx_play(SFX_MOVE);
}
/* L / R: the first row of the previous / next section (L inside a section: its own first row first);
   false at either end (no wrap). */
static bool heading_jump(settings_state *s,int dir)
{
 int *sel=&level(s)->selected,i=*sel;
 if(dir<0)while(i>0 && s->rows[i-1].row.kind!=BB_ROW_HEADING)i--;   /* this section's first row */
 if(dir<0 && i!=*sel){*sel=i;return true;}
 for(i+=dir;i>=0 && i<s->count;i+=dir)if(s->rows[i].row.kind==BB_ROW_HEADING)break;
 if(i<0 || i>=s->count)return false;
 if(dir<0)while(i>0 && s->rows[i-1].row.kind!=BB_ROW_HEADING)i--;
 while(i<s->count && s->rows[i].row.kind==BB_ROW_HEADING)i++;
 if(i>=s->count || i==*sel)return false;
 *sel=i;return true;
}
static void draw_bottom(void *);static void draw_top(void *);
/* Returns true when the row's own value was changed in place (its sound is the value's, decided by the caller).
   press: A (Room style opens its picker on A; left / right change it in place). */
static bool activate(settings_state *s,bb_settings_row *r,int direction,bool press)
{
 if(r->id>=APP_SYSTEM){if(!push(s,PAGE_SYSTEM,bb_systems[r->id-APP_SYSTEM]))ui_confirm_action("Settings unavailable","This system's settings could not be opened safely.","A  Back",s->context.running);return false;}
 if(r->id>=APP_HOTKEY && r->id<=APP_HOTKEY_LAST){bb_settings_hotkey_cycle(r->id-APP_HOTKEY,direction);config_save();return true;}
 switch(r->id) {
 case APP_CUSTOMIZE:push(s,PAGE_CUSTOMIZE,NULL);break;
 case APP_SYSTEMS:push(s,PAGE_SYSTEMS,NULL);break;
 case APP_COLLECTION:push(s,PAGE_COLLECTION,NULL);break;
 case APP_ABOUT:push(s,PAGE_ABOUT,NULL);break;
 case APP_ADVANCED:push(s,PAGE_ADVANCED,NULL);break;
 case APP_HANDS:push(s,PAGE_HANDS,NULL);break;
 case APP_UPDATES:bb_update_settings();break;
 case APP_APPEARANCE:
  if(press){appearance(s);break;}
  g_cfg.theme=(g_cfg.theme+theme_count()+direction)%theme_count();theme_menu_refresh();config_user_changed("theme");config_save();return true;
 case APP_SIDE:control_side_set(!control_side());input_touch_reset();config_user_changed("touch_click_left");config_save();return true;
 case APP_SKIN:g_cfg.hand_skin=(g_cfg.hand_skin+HAND_SKINS+direction)%HAND_SKINS;config_save();return true;
 case APP_GLOVES:g_cfg.hand_glove=(g_cfg.hand_glove+HAND_GLOVES+direction)%HAND_GLOVES;config_save();return true;
 case APP_FOLDER:folders_requested=true;break;
 case APP_IMPORT:breadbox_import_start(false);break;
 case APP_CREDITS:bb_credits_run();if(!aptMainLoop())return false;break;
 case APP_INSTALL:bb_install_help_run();if(!aptMainLoop())return false;break;
 case APP_WELCOME:if(bb_welcome_open && bb_welcome_open())folders_requested=true;break;
 case ACTION_HELP:full_help(s);break;
 default:
  if(s->ops)s->ops->change(r->id,direction);
  else {const bb_system_t *sys=level(s)->system;if(sys && sys->setting_change)sys->setting_change(r->id,direction);}
  return true;
 }
 return false;
}
static void clear_row(settings_state *s,bb_settings_row *row)
{
 if(row->id>=APP_HOTKEY && row->id<=APP_HOTKEY_LAST) {
  if(ui_confirm_action("Restore shared value?","Applies to all systems. Restore this value to its built-in default. Game exceptions and saved data are kept.","A  Restore",s->context.running)){bb_settings_hotkey_default(row->id-APP_HOTKEY);config_save();}
 } else if(row->id==APP_SIDE){control_side_set(0);input_touch_reset();config_save();sfx_play(SFX_VALUE);}
 else if(row->source==BB_SOURCE_ALL) {
  if(ui_confirm_action("Restore shared value?","Applies to all systems. Restore this value to its built-in default. Game exceptions and saved data are kept.","A  Restore",s->context.running) && s->ops)s->ops->clear(row->id);
 } else if(s->ops){s->ops->clear(row->id);sfx_play(SFX_VALUE);}
 else {const bb_system_t *sys=level(s)->system;if(sys && sys->setting_clear){sys->setting_clear(row->id);sfx_play(SFX_VALUE);}}
}
static void settings_backdrop(void *ud){settings_state *s=ud;if(s->top)s->top(s->top_ud);else libui_draw_backdrop();}
bool bb_settings_child_tag_visible(void){return !active || !state.context.game || state.context.running;}
bool bb_settings_child_top(void)
{
 if(!active)return false;
 ui_destination_draw_top(state.scene?state.scene:&state.backdrop,NULL,NULL);return true;
}
/* One frame's keys on the current page, before the swap gate: list moves, heading skips and jumps, title taps. */
static u32 settings_keys(settings_state *s)
{
 settings_level *l=level(s);int before=l->selected;
 ui_button_t bar[UI_BAR_MAX];int nbar=settings_bar(s,bar);
 u32 k=ui_card_input(&s->list,s->count,&l->selected,ui_bar_input(bar,nbar,input_keys_down()));
 if(before!=l->selected && s->count && s->rows[l->selected].row.kind==BB_ROW_HEADING) {   /* headings are passed over */
  int dir=l->selected<before?-1:1,next=l->selected+dir;
  l->selected=next>=0 && next<s->count?next:before;
 }
 if(l->selected<0 || l->selected>=s->count)l->selected=0;
 if(before!=l->selected)s->value_page=0;
 if((k&(KEY_L|KEY_R)) && heading_jump(s,(k&KEY_L)?-1:1)){s->value_page=0;sfx_play(SFX_JUMP);}
 bb_settings_row *row=s->count?&s->rows[l->selected].row:NULL;
 if(row && row->kind!=BB_ROW_HEADING && row->id!=ACTION_HELP)s->help_focus=l->selected;
 if((k&KEY_TOUCH) && ui_card_title_hit())k|=KEY_B;   /* tapping the title goes back */
 return k;
}
/* What the gated keys do on the current page. False: Settings closes. */
static bool settings_act(settings_state *s,u32 k,const ui_destination *scene,bool *resume)
{
 settings_level *l=level(s);bb_settings_row *row=s->count?&s->rows[l->selected].row:NULL;
 if(scene && scene->settled && !scene->settled(scene->userdata)){if(!(k&KEY_B))sfx_cancel();k&=KEY_B;}   /* still arriving: only B acts */
 settings_value_input(s,k);
 if(k&KEY_START){full_help(s);return aptMainLoop();}
 if(k&KEY_B){if(!pop(s))return false;ui_swap_enter();return true;}
 if(!row)return true;
 if((k&KEY_Y) && reset_scope(s)){reset(s);build_rows(s);}
 else if((k&KEY_X) && row->can_clear){clear_row(s,row);build_rows(s);}
 else if((k&(KEY_A|KEY_LEFT|KEY_RIGHT)) && row->kind!=BB_ROW_HEADING && (row->kind==BB_ROW_VALUE || (k&KEY_A))) {
  char was[64];snprintf(was,sizeof(was),"%s",row->value?row->value:"");
  int depth=s->depth;s->value_page=0;
  bool in_place=activate(s,row,k&KEY_LEFT?-1:1,(k&KEY_A)!=0);
  if(!aptMainLoop())return false;
  if(s->depth!=depth){ui_swap_enter();return true;}   /* a page opened: its rows are built */
  build_rows(s);
  if(in_place) {   /* one sound for the change; a value that did not move (a fixed or unavailable row) is silent */
   const char *now=s->rows[level(s)->selected].row.value;
   if(!now || !strcmp(now,was))sfx_cancel();
   else sfx_play(!strcmp(was,"On") || !strcmp(was,"Off") ? SFX_TOGGLE : SFX_VALUE);
  }
 } else if(k&KEY_A)sfx_cancel();   /* a heading: nothing to open */
 if(s->ops && s->ops->resume_requested && s->ops->resume_requested()){*resume=s->context.running;return false;}
 return !folders_requested;
}
static bool run_model(const bb_system_t *system,const bb_settings_context *context,void (*top)(void *),void *ud,const ui_destination *scene)
{
 // A nested run would overwrite the state referenced by the installed backdrop.
 if(active)return false;
 settings_state *s=&state;memset(s,0,sizeof(*s));
 s->rows=calloc(SETTINGS_ROWS,sizeof(*s->rows));
 if(!s->rows){ui_confirm_action("Settings unavailable","There is not enough free memory to open Settings.","A  Back",context->running);return false;}
 s->context=*context;s->scene_visibility=scene?0:1;s->top=top;s->top_ud=ud;s->scene=scene;
 ui_clean_title(context->title?context->title:"",s->title,sizeof(s->title));
 s->backdrop=(ui_destination){.userdata=s,.draw_top=settings_backdrop};
 active=true;debug_capture=false;ui_swap_enter();
 const ui_destination *old_scene=ui_dialog_scene_set(scene?scene:&s->backdrop);
 if(scene && scene->enter)scene->enter(scene->userdata);
 u64 tick=svcGetSystemTick();
 /* A game's own Settings open on its system page; app Settings on the top list (or down to a given system). */
 bool opened=context->game?push(s,PAGE_SYSTEM,system):push(s,PAGE_TOP,NULL) && (!system || (push(s,PAGE_SYSTEMS,NULL) && push(s,PAGE_SYSTEM,system)));
 if(!opened){
  ui_confirm_action("Settings unavailable","This game's settings could not be opened safely. Return to your collection and try again.","A  Back",context->running);
  system_open(s,NULL);ui_dialog_scene_set(old_scene);free(s->rows);s->rows=NULL;active=false;return false;
 }
 bool resume=false;
 while(aptMainLoop()) {
  if(dev_tick(context->running)||dev_ui_pending())break;
  u64 now=svcGetSystemTick();float dt=(now-tick)/(float)SYSCLOCK_ARM11;tick=now;if(dt>.05f)dt=.05f;
  if(scene && scene->tick)scene->tick(scene->userdata,dt);
  s->scene_time+=dt;s->page_time+=dt;
  /* the card arrives once, with Settings (in the room: as the camera settles); pages change without a fade */
  s->scene_visibility=ui_motion_ease((scene?s->scene_time-(UI_MOTION_CAMERA_S-UI_MOTION_UI_S):s->scene_time)/UI_MOTION_UI_S);
  ui_pause_motion(s->scene_visibility);
  input_poll();
  u32 k=ui_swap_keys(settings_keys(s),UI_SWAP_MENU);
  if(!settings_act(s,k,scene,&resume))break;
  settings_level *l=level(s);
  ui_card_update(&s->list,l->selected,s->count);
  if(l->selected>0 && s->rows[l->selected-1].row.kind==BB_ROW_HEADING && s->list.scroll_target>l->selected-1)
   s->list.scroll_target=l->selected-1;   /* a section's first row keeps its heading in view */
  if(context->running)ui_pause_frame();else music_update();
  ui_begin();video_present_vsync(context->running,draw_top,draw_bottom,s);
  if(sw_framelog && s->count>0) {   /* dev: what this presented frame showed */
   const bb_settings_row *f=&s->rows[l->selected].row;
   extern u32 ui_mark_ticks;   /* the highlighter's CPU time this frame (ui_mark.c) */
   log_printf("frame: settings t=%ums page=%d sel=%d hl=%.3f lift=%.3f vis=%.3f rows=%d mark=%uus value=%s\n",(unsigned)(svcGetSystemTick()/(SYSCLOCK_ARM11/1000)),l->page,l->selected,s->list.hl,
    l->selected<FLIST_MAX?s->list.lift[l->selected]:0.0f,s->scene_visibility,s->count,(unsigned)(ui_mark_ticks/(SYSCLOCK_ARM11/1000000)),f->value?f->value:"");
  }
 }
 if(scene && scene->leave && aptMainLoop()) {
  scene->leave(scene->userdata);float leaving=0,start_visibility=s->scene_visibility;tick=svcGetSystemTick();
  while(aptMainLoop() && leaving<UI_MOTION_CAMERA_S && (!scene->settled || !scene->settled(scene->userdata))) {
   u64 now=svcGetSystemTick();float dt=(now-tick)/(float)SYSCLOCK_ARM11;tick=now;if(dt>.05f)dt=.05f;leaving+=dt;
   if(scene->tick)scene->tick(scene->userdata,dt);
   s->scene_visibility=start_visibility*(1-ui_motion_ease(leaving/UI_MOTION_UI_S));ui_pause_motion(s->scene_visibility);
   music_update();ui_begin();video_present_vsync(false,draw_top,draw_bottom,s);
  }
 }
 ui_pause_motion(1);ui_dialog_scene_set(old_scene);
 system_open(s,NULL);
 active=false;hands_preview=false;debug_capture=false;free(s->rows);s->rows=NULL;if(aptMainLoop())config_save();return resume;
}
void bb_settings_run(const bb_system_t *system,const bb_settings_context *context,void (*top)(void *),void *ud)
{run_model(system,context,top,ud,NULL);}
void bb_settings_room_run(const ui_destination *scene)
{bb_settings_context c={0};run_model(NULL,&c,NULL,NULL,scene);}
void bb_settings_game(const bb_system_t *sys,const char *path,const char *key,const char *title,void (*top)(void *),void *ud)
{bb_settings_context c={.path=path,.key=key,.title=title,.game=true};bb_settings_run(sys,&c,top,ud);}
bool settings_ui_run(bool game,void (*top)(void *))
{bb_settings_context c={.game=game,.running=game,.title=game?bb_session_title():NULL};return run_model(game?bb_session_system():NULL,&c,top,NULL,NULL);}
bool settings_ui_take_folders(void){bool r=folders_requested;folders_requested=false;return r;}
bool settings_ui_hands_row(void){return hands_preview && room_show_hands;}
bool settings_ui_active(void){return active;}
bool settings_ui_debug_capture(void){return active && debug_capture;}
bool settings_ui_debug_key(const char *key){if(!active || (strcmp(key,"debug-on") && strcmp(key,"debug-off")))return false;debug_capture=!strcmp(key,"debug-on");build_rows(&state);return true;}
bool settings_ui_cheats(void){const bb_system_t *s=bb_session_system();return s?bb_cheats_ui(s->cheats,true):false;}
#include "components/settings_ui.inc"
