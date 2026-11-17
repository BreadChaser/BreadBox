/* Production Settings shell: the page tree, navigation, Y scopes, the shared card's highlighter and the GBA settings
   flicker regression, against a fake two-system registry and the real ui.c / ui_mark.c. */
#define APP_BREADBOX
#define HOST_CUSTOM_SESSION
#define SETTINGS_NAV_TEST
#define main harness_main
#include "../app/host/check.c"
#undef main
#include "bb_registry.h"
#include "../../app/ui/theme_picker.c"
#include "../../app/settings/settings_ui.c"
bool cheats_widescreen(bool on){(void)on;return false;}
void sfx_play(int n){ui_trace_sound(n);}void sfx_now(int n){ui_trace_sound(n);}void sfx_default(int n){ui_trace_sound(n);}void sfx_cancel(void){}void sfx_flush(void){}
void sfx_card_closes(void){}bool sfx_close_said(void){return false;}bool sw_framelog;
static bool opened,paging_inputs;static int resets,cleared,changed;
static char paging_label[128],paging_value[160];
static bool open_test(const bb_settings_context *c){opened=c->game;return true;}
/* rows 0..3: Picture / Controls; rows 4..5: the Advanced page's Speed section */
static unsigned count_test(void){return 6;}
static bool read_test(unsigned i,bb_settings_row *r){
 static const char *labels[]={"Inherited","Tested","Equal but overridden","Shared shortcut","Frameskip","CPU speed"};
 if(i>=6)return false;
 *r=(bb_settings_row){.id=(int)i,.section=i<2?"Picture":i<4?"Controls":"Speed",.label=labels[i],.value="Off",.help="Explanation",.source=(bb_setting_source)(i%4+1),.can_clear=true,.advanced=i>=4};
 if(paging_inputs && !i){r->label=paging_label;r->value=paging_value;r->kind=BB_ROW_VALUE;}return true;
}
static void change_test(int id,int d){changed=id+d;if(paging_inputs)strcpy(paging_value,"Changed");}
static void clear_test(int id){cleared=id;}
static void reset_test(bb_settings_reset scope){resets=scope+1;}
static void close_test(void){}
static bool resume_requested=true;
static bool request_resume(void){return resume_requested;}
static const bb_cheat_ops test_cheats={0};
static const bb_cheat_ops *seen_cheats;
bool bb_cheats_ui(const bb_cheat_ops *ops,bool game){assert(game);seen_cheats=ops;return true;}
void music_update(void){}
void breadbox_import_start(bool b){(void)b;}
void bb_credits_run(void){}
void bb_install_help_run(void){}
static int welcome_opened;static bool welcome_x;   /* X on the tour's games card */
bool bb_welcome_open(void){welcome_opened++;return welcome_x;}
const char *bb_update_preference(void){return "Automatic";}
static int updates_opened;
void bb_update_settings(void){updates_opened++;}
static const bb_settings_ops test_ops={open_test,count_test,read_test,change_test,clear_test,reset_test,close_test,request_resume};
static const bb_system_t one={.id="one",.name="First system",.settings_model=&test_ops,.cheats=&test_cheats};
static const bb_system_t two={.id="two",.name="Second system",.settings_model=&test_ops,.cheats=&test_cheats};
static const bb_system_t bare={.id="bare",.name="No settings"};
const bb_system_t *const bb_systems[]={&one,&two,&bare};const unsigned bb_system_count=3;
bool room_show_hands;
bool bb_session_active(void){return false;}
const bb_system_t *bb_session_system(void){return &one;}
u32 dev_ui_keys_take(void){return 0;}
static unsigned parent_frames;
static void parent_top(void *ud){assert(ud==&parent_frames);parent_frames++;}
static void dispatcher_alias(void *ud){ui_menu_top(ud);}
static unsigned room_enters,room_leaves,room_ticks;
static void room_enter_test(void *ud){assert(ud==&parent_frames);room_enters++;}
static void room_leave_test(void *ud){assert(ud==&parent_frames);room_leaves++;}
static void room_tick_test(void *ud,float dt){assert(ud==&parent_frames && dt>=0);room_ticks++;}
static bool room_settled_test(void *ud){assert(ud==&parent_frames);return true;}
static void close_top(void *ud){parent_top(ud);host_menu_key=KEY_B;}
static void nested_top(void *ud){
 assert(ud==&parent_frames && active);settings_cached_row *rows=state.rows;
 assert(!settings_ui_run(false,ui_menu_top));assert(active && state.rows==rows);
 parent_frames++;host_menu_key=KEY_B;
}
#ifdef SHARE_TOP_TEST
// Compile the exact shared Share top/tag functions alongside real Settings/ui.c.
enum {SHARE_RELEASE,SHARE_LOCAL,SHARE_INSTALL,SHARE_CREDITS};
static const ui_destination *share_scene;static bool share_qr,serving,share_failed;
static char url[128],cia_note[160];static unsigned long served_count;
static int share_item(void){return SHARE_RELEASE;}
#define APP_NAME "BreadBox"
#define PSX3D_VERSION "1.0.0"
#include SHARE_TOP_TEST
static void share_over_settings(void *ud){
 (void)ud;const ui_destination *saved=ui_dialog_scene_set(NULL);ui_dialog_scene_set(saved);
 ui_destination share={.draw_top=ui_menu_top};share_scene=&share;
 share_test_top(NULL);share_scene=NULL;
 assert(ui_dialog_scene_set(NULL)==saved);ui_dialog_scene_set(saved);
 parent_frames++;host_menu_key=KEY_B;
}
#endif
/* The rendered card (ui_card_draw): every row, value, title and notice baseline is on a drawn ruled line; the
   selection is the highlighter's stroke (ui_mark.h) around the selected row's text, no band. */
static int ruled_rows;
static void ruled_row(int i,void *ud,ui_card_row_t *out){
 (void)ud;static const char *const l[]={"Screen","Smoothing","FPS counter","Controls","Button mappings","Help","Credits"};
 *out=(ui_card_row_t){.label=l[i%7],.value=i%2?"On":NULL,.icon=UI_ICON_TOGGLE,.arrows=i%2,.source=BB_SOURCE_NONE,.heading=i==3};ruled_rows++;
}
static void paper_line_checks(void)
{
 flist_t f;memset(&f,0,sizeof(f));f.hl=1;f.lift[1]=1;
 ui_card_t card={.eyebrow="SETTINGS",.eyebrow_right="2 / 5",.title="Picture",.notice="Saved for this game"};
 pause_progress=1;host_mark_n=0;host_text_begin();ui_card_draw(&f,7,1,ruled_row,NULL,&card);host_text_on=false;
 float lines[64];int nl=0;
 for(int r=0;r<host_rect_n;r++){
  host_rect_t *q=&host_rects[r];
  if(q->h==1 && ((q->w==UI_CARD_W && q->z==UI_CARD_Z_RULE) || (q->w==UI_CARD_W-2 && q->z==UI_CARD_Z_NOTICE_LINE)) && nl<64)lines[nl++]=q->y;   /* the grid, and the notice's own line */
 }
 assert(nl>=UI_CARD_ROWS+2);
 int on=0;
 for(int t=0;t<host_text_n;t++){
  float base=host_texts[t].y+host_texts[t].sy*HOST_SHEET_BASELINE;   /* the host font's own letters (both faces share its sheet) */
  bool hit=false;for(int k=0;k<nl;k++)hit|=fabsf(base-lines[k])<0.01f;
  if(!hit)fprintf(stderr,"off the lines: '%s' baseline %.2f\n",host_texts[t].s,base);
  assert(hit);on++;
 }
 assert(on>=UI_CARD_ROWS+2);   /* rows, values, the title, the eyebrow, the notice */
 for(int r=0;r<host_rect_n;r++)assert(host_rects[r].z!=UI_CARD_Z_SELECT || host_rects[r].h!=UI_CARD_ROW-1);   /* no band */
 /* one stroke, on the selected row: from left of its label to its value's arrow tip, around the text's x-height */
 assert(host_mark_n==1);ui_mark_geo *g=&host_marks[0];
 float line=UI_CARD_ROWS_Y+2*UI_CARD_ROW-1,right=UI_CARD_X+UI_CARD_W-UI_CARD_RIGHT;
 assert(g->variant==1 && g->n==3);
 assert(fabsf(g->x0-(UI_CARD_TEXT_X+UI_CARD_NUDGE-UI_MARK_BEFORE))<.01f && fabsf(g->x1-(right+UI_MARK_AFTER))<.01f);
 assert(g->top>=line-UI_PAPER_LINE+1 && g->bottom<=line+UI_PAPER_LINE-UI_PAPER_CAP);
 assert(g->x0>UI_CARD_MARGIN_X && g->x1<=UI_CARD_X+UI_CARD_W);
 /* a heading is never marked; nothing at all before the stroke starts drawing */
 host_mark_n=0;f.lift[3]=1;ui_card_draw(&f,7,3,ruled_row,NULL,&card);assert(host_mark_n==0);
 host_mark_n=0;f.lift[2]=0;ui_card_draw(&f,7,2,ruled_row,NULL,&card);assert(host_mark_n==1 && host_marks[0].n==0);
 /* a row without a value: the stroke ends past its label */
 host_mark_n=0;f.lift[2]=1;ui_card_draw(&f,7,2,ruled_row,NULL,&card);assert(host_mark_n==1 && host_marks[0].x1<UI_CARD_TEXT_X+UI_CARD_NUDGE+ui_text_width(NULL,UI_CARD_ROW_SIZE,"FPS counter")+UI_MARK_AFTER+.01f);
 /* Dark cards use the palette's visible selection band and a narrow focus edge. */
 for(int look=BB_LOOK_PSX3D;look<BB_LOOK_COUNT;look++) {
  theme_select_look(look);host_mark_n=0;host_text_begin();
  ui_card_draw(&f,7,2,ruled_row,NULL,&card);host_text_on=false;
  int bands=0,edges=0;
  for(int i=0;i<host_rect_n;i++) {
   const host_rect_t *r=&host_rects[i];
   if(r->z==UI_CARD_Z_SELECT && r->h==UI_CARD_ROW-1) {
    assert(fabsf(r->y-(UI_CARD_ROWS_Y+2*UI_CARD_ROW))<.01f);
    bands+=r->w>UI_LOOK_FOCUS_EDGE;edges+=r->w==UI_LOOK_FOCUS_EDGE;
   }
  }
  assert(!host_mark_n && bands==1 && edges==1);
 }
 theme_select_look(BB_LOOK_ROOM);
 /* the source marks: nothing for a System value, a mark for each of the others */
 for(int src=BB_SOURCE_NONE;src<=BB_SOURCE_ALL;src++){
  capture_bounds=true;ink_left=ink_top=1e9f;ink_right=ink_bottom=-1e9f;
  ui_source_mark((bb_setting_source)src,30,30,0xff000000,0xffffffff);capture_bounds=false;
  assert((ink_right>=ink_left)==(src==BB_SOURCE_CHANGED || src==BB_SOURCE_RECOMMENDED || src==BB_SOURCE_ALL));
 }
 printf("PASS: ruled card: %d texts on %d drawn lines; the selection is one highlighter stroke inside its row, none on a heading\n",on,nl);
}
/* GBA settings flicker: while a row's lift animates (about 1 s),
   every frame must draw each row's own value. Before the fix
   the label was refitted each frame and its insert into ui_fit_ex's 32-entry cache could overwrite the slot the
   value had just been read from: the value then showed another text for a frame. */
static const char *const flicker_labels[]={"Screen size","Smoothing","Color correction","Frame blend","FPS counter"};
static const char *const flicker_values[]={"Fit","Sharp","Off","On","Off"};
static void flicker_row(int i,void *ud,ui_card_row_t *out){(void)ud;*out=(ui_card_row_t){.label=flicker_labels[i],.value=flicker_values[i],.arrows=true,.source=BB_SOURCE_SYSTEM};}
/* One run: the card opens on `sel` (or moves there from `from`) and its lifts settle as ui_card_update moves them
   (1 - e^-18 dt per frame), `extra` foreign cache inserts per frame (the top screen, the bar). Returns the frames
   on which some row drew a text other than its own value, or a label changed size. */
static int flicker_run(int sel,int from,float hz,int extra)
{
 flist_t f;memset(&f,0,sizeof(f));ui_card_t card={.title="Kirby"};pause_progress=1;
 if(from>=0)f.lift[from]=1;
 float step=1-expf(-UI_MOTION_SETTLE_RATE/hz),size0[5]={0};int bad=0;char junk[32];static int serial;
 for(int frame=0;frame<(int)(1.5f*hz);frame++) {
  for(int i=0;i<5;i++)f.lift[i]+=((i==sel?1.0f:0.0f)-f.lift[i])*step;
  for(int k=0;k<extra;k++){snprintf(junk,sizeof(junk),"junk %d",serial++);ui_fit_ex(NULL,junk,.7f,.7f,100,NULL);}
  host_text_begin();ui_card_draw(&f,5,sel,flicker_row,NULL,&card);host_text_on=false;
  bool wrong=false;
  for(int i=0;i<5;i++) {
   int l=host_text_find(flicker_labels[i]),twice=0;
   for(int t=0;t<host_text_n;t++)twice+=!strcmp(host_texts[t].s,flicker_labels[i]);
   if(host_text_find(flicker_values[i])<0 || twice!=1 || l<0)wrong=true;
   else if(!frame)size0[i]=host_texts[l].sy;else if(host_texts[l].sy!=size0[i])wrong=true;
  }
  bad+=wrong;
 }
 return bad;
}
static void flicker_checks(void)
{
 int runs=0,bad=0;
 for(int hz=30;hz<=60;hz+=30)for(int sel=0;sel<5;sel++)for(int from=-1;from<5;from++)for(int extra=0;extra<4;extra++)
  if(from!=sel){bad+=flicker_run(sel,from,(float)hz,extra);runs++;}
 if(bad)fprintf(stderr,"flicker: %d frames drew a wrong value or label\n",bad);
 assert(!bad);
 printf("PASS: GBA settings flicker regression: %d runs (open on a row or move to it, 30 and 60 Hz, 0-3 other cache users per frame), every frame drew each row's own value at a steady size\n",runs);
}
/* valid UTF-8, and no "..." / U+2026 ellipsis */
static bool whole_utf8(const char *s)
{
 if(strstr(s,"...")||strstr(s,"\xe2\x80\xa6"))return false;
 for(const unsigned char *p=(const unsigned char *)s;*p;) {
  int n=*p<0x80?1:(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:(*p&0xf8)==0xf0?4:0;if(!n)return false;
  for(int i=1;i<n;i++)if((p[i]&0xc0)!=0x80)return false;p+=n;
 }
 return true;
}
static void squeeze(char *out,size_t cap,const char *in){size_t n=0;for(;*in&&n+1<cap;in++)if(*in!=' ')out[n++]=*in;out[n]=0;}
/* A page on its own (no run loop): the stack holds exactly that page. */
static void page_only(settings_state *v,settings_page page,const bb_system_t *sys)
{v->depth=1;v->levels[0]=(settings_level){.page=page,.system=sys};build_rows(v);}
/* Full-value inspection: the focused row the card cuts shows its whole text on the
   top screen through the real Settings top (settings_tag) and ui_value_card, at the floor; a row that fits shows none. */
static void maximum_value_checks(void)
{
 /* PATH_LEN includes NUL. Every component is valid UTF-8 and shorter than 255 bytes. */
 char path[PATH_LEN];strcpy(path,"sdmc:/");
 for(int i=6;i<PATH_LEN-1;i++)path[i]='W';
 path[100]='/';path[200]='/';path[300]='/';path[400]='/';
 memcpy(path+20,"\xc3\xa9\xe5\x86\x92\xf0\x9f\x8d\x9e",9);path[PATH_LEN-1]=0;
 assert(strlen(path)==PATH_LEN-1 && whole_utf8(path));
 strcpy(g_cfg.games_dir,path);
 /* Keep ordinary row storage at its pre-paging budget; only one folder can be full length. */
 assert(sizeof(settings_cached_row)-sizeof(bb_settings_row)<=1600);
 static settings_cached_row cache[SETTINGS_ROWS];static settings_state v;memset(&v,0,sizeof(v));v.rows=cache;
 page_only(&v,PAGE_COLLECTION,NULL);assert(v.rows[0].row.id==APP_FOLDER);
 assert(v.rows[0].row.value==v.folder_value && !strcmp(v.rows[0].row.value,path));
 int pages=ui_value_card_pages(v.rows[0].row.label,v.rows[0].row.value);assert(pages>1);
 ui_button_t bar[UI_BAR_MAX];int nb=settings_bar(&v,bar);bool next=false;
 for(int i=0;i<nb;i++)if(bar[i].key==KEY_SELECT){assert(bar[i].icon==UI_ICON_RIGHT);next=true;}
 assert(next);
 char joined[1024]="",want[1024],got[1024];
 for(int page=0;page<pages;page++) {
  assert(v.value_page==page);host_text_begin();settings_tag(&v);host_text_on=false;
  bool cue=false,number=false;char page_number[32];snprintf(page_number,sizeof(page_number),"%d/%d",page+1,pages);
  for(int t=0;t<host_text_n;t++) {
   host_text_t *line=&host_texts[t];
   assert(whole_utf8(line->s) && line->sy*30>=UI_TEXT_FLOOR*30-0.01f);
   assert(line->y>=0 && line->y+line->sy*30<=240);
   assert(ui_text_width(NULL,line->sy,line->s)<=UI_VALUE_CARD_W-UI_VALUE_CARD_PAD*2+0.01f);
   if(!strcmp(line->s,"SEL")){cue=true;continue;}
   if(!strcmp(line->s,page_number)){number=true;continue;}
   strncat(joined,line->s,sizeof(joined)-strlen(joined)-1);
  }
  assert(cue && number);
  settings_value_input(&v,KEY_SELECT);
 }
 assert(v.value_page==0);snprintf(want,sizeof(want),"%s%s",v.rows[0].row.label,path);
 squeeze(got,sizeof(got),joined);char compact[1024];squeeze(compact,sizeof(compact),want);assert(!strcmp(compact,got));
 /* A shorter value drops the paging action; a new page starts on page one. */
 strcpy(g_cfg.games_dir,"sdmc:/games");build_rows(&v);assert(ui_value_card_pages(v.rows[0].row.label,v.rows[0].row.value)==1);
 nb=settings_bar(&v,bar);for(int i=0;i<nb;i++)assert(bar[i].key!=KEY_SELECT);
 v.value_page=pages-1;assert(push(&v,PAGE_ABOUT,NULL) && v.value_page==0);
 puts("PASS: maximum-length valid UTF-8 path: complete text reachable with SELECT, every page marked, floor/bounds kept");
}
/* Observe the real run_model after actual input frames, not calls to helpers. Settings > Systems > First system:
   row 0 is the Picture heading, row 1 the long paging row. */
static int paging_frame;
static void paging_input_top(void *unused)
{
 (void)unused;assert(active);settings_level *l=level(&state);
 switch(paging_frame++) {
 case 0:assert(l->page==PAGE_TOP);host_menu_key=KEY_DOWN;break;                       /* Systems */
 case 1:assert(l->selected==1);host_menu_key=KEY_A;break;
 case 2:assert(l->page==PAGE_SYSTEMS && l->selected==0);host_menu_key=KEY_A;break;      /* First system */
 case 3:assert(l->page==PAGE_SYSTEM && l->system==&one && l->selected==1 && state.value_page==0);
  assert(ui_value_card_pages(state.rows[1].row.label,state.rows[1].row.value)>1);host_menu_key=KEY_SELECT;break;
 case 4:assert(state.value_page==1);host_menu_key=KEY_DOWN;break;
 case 5:assert(l->selected==2 && state.value_page==0);host_menu_key=KEY_UP;break;
 case 6:assert(l->selected==1 && state.value_page==0);host_menu_key=KEY_UP;break;        /* the heading above: stays */
 case 7:assert(l->selected==1);host_menu_key=KEY_SELECT;break;
 case 8:assert(state.value_page==1);host_menu_key=KEY_RIGHT;break;
 case 9:assert(changed==1 && state.value_page==0 && !strcmp(state.rows[1].row.value,"Changed"));host_menu_key=KEY_R;break;
 case 10:assert(!strcmp(state.rows[l->selected].row.label,"Shared shortcut") || !strcmp(state.rows[l->selected].row.label,"Equal but overridden"));
  assert(state.rows[l->selected-1].row.kind==BB_ROW_HEADING);host_menu_key=KEY_B;break;   /* R: Controls' first row */
 case 11:assert(l->page==PAGE_SYSTEMS && l->selected==0);host_menu_key=KEY_B;break;
 case 12:assert(l->page==PAGE_TOP && l->selected==1);host_menu_key=KEY_B;break;          /* B at the top: Settings closes */
 default:assert(!"unexpected Settings frame");
 }
}
static void paging_reset_input_checks(void)
{
 memset(paging_label,'W',sizeof(paging_label)-1);paging_label[sizeof(paging_label)-1]=0;
 memset(paging_value,'W',sizeof(paging_value)-1);paging_value[sizeof(paging_value)-1]=0;
 paging_inputs=true;paging_frame=0;changed=0;resume_requested=false;
 host_render_top=true;host_menu_key=0;host_menu_frames=100;confirm_step=0;
 bb_settings_context context={0};assert(!run_model(NULL,&context,paging_input_top,NULL,NULL));
 assert(paging_frame==13 && !active);host_menu_frames=-1;host_menu_key=0;paging_inputs=false;resume_requested=true;
 puts("PASS: real Settings frames: top > Systems > system (A, A), SELECT paging, DOWN/UP (no stop on a heading), RIGHT change, R heading jump, B back each level and out");
}
static void value_inspection_checks(void)
{
 static const char *const labels[]={"Games folder","FF keys","Folder"};
 static const char *const values[]={"sdmc:/roms/Spiele und Sammlungen/BreadBox_Sammlung_2026/PlayStation-Ordner-mit-einem-langen-Namen",
  "SELECT + ZL + ZR + L + R + Y + X",
  "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0\xe3\x83\x95\xe3\x82\xa9\xe3\x83\xab\xe3\x83\x80 \xc3\x9c" "bersicht der Sammlung f\xc3\xbcr BreadBox und PlayStation \xe2\x80\x94 Gro\xc3\x9f" "e Ordner"};
 static settings_cached_row rows[1];
 static settings_state v;memset(&v,0,sizeof(v));v.rows=rows;v.count=1;v.depth=1;v.levels[0].page=PAGE_COLLECTION;
 for(int i=0;i<3;i++) {
  rows[0].row=(bb_settings_row){.id=0,.label=labels[i],.value=values[i],.kind=BB_ROW_VALUE};
  ui_card_row_t r;card_row(0,&v,&r);assert(ui_card_row_cut(&r));   /* the bottom card shows it cut */
  host_text_begin();settings_tag(&v);host_text_on=false;
  assert(host_text_n>=2);
  char joined[1024]="",want[1024],got[1024];
  for(int t=0;t<host_text_n;t++) {
   assert(!host_texts[t].font && host_texts[t].sy*30>=UI_TEXT_FLOOR*30-0.01f && whole_utf8(host_texts[t].s));
   assert(ui_text_width(NULL,host_texts[t].sy,host_texts[t].s)<=UI_VALUE_CARD_W-UI_VALUE_CARD_PAD*2+0.01f);
   strncat(joined,host_texts[t].s,sizeof(joined)-strlen(joined)-1);
  }
  char full[512];snprintf(full,sizeof(full),"%s%s",labels[i],values[i]);
  squeeze(want,sizeof(want),full);squeeze(got,sizeof(got),joined);assert(!strcmp(want,got));
 }
 rows[0].row=(bb_settings_row){.id=0,.label="Smoothing",.value="On",.kind=BB_ROW_VALUE};
 ui_card_row_t r;card_row(0,&v,&r);assert(!ui_card_row_cut(&r));
 host_text_begin();settings_tag(&v);host_text_on=false;assert(host_text_n==0);   /* no chatter for a whole row */
 puts("PASS: focused cut rows (long folder, key combo, multibyte) shown whole on the top screen at the floor");
}
/* The room bar: only the chosen place's tab carries the A glyph. */
static void room_cue_checks(void)
{
 for(int lit=0;lit<4;lit++) {
  ui_button_t b[4]={{0,"Shelf",UI_ICON_TOGGLE,false},{0,"Settings",UI_ICON_TOGGLE,false},{0,"Share",UI_ICON_TOGGLE,false},{0,"Quit",UI_ICON_EXIT,false}};
  b[lit].key=KEY_A;
  host_text_begin();ui_tabs_icons(b,4,lit);host_text_on=false;
  int a=-1,count=0;for(int t=0;t<host_text_n;t++)if(!strcmp(host_texts[t].s,"\xee\x80\x80")){a=t;count++;}
  assert(count==1 && host_texts[a].x>=lit*80 && host_texts[a].x<(lit+1)*80);
  for(int t=0;t<host_text_n;t++)assert(strcmp(host_texts[t].s,"Shelf")&&strcmp(host_texts[t].s,"Quit"));
 }
 puts("PASS: room bar: the A glyph only on the chosen place, no captions");
}
static int count_id(const settings_state *s,int id){int n=0;for(int i=0;i<s->count;i++)n+=s->rows[i].row.id==id;return n;}
static int find_label(const settings_state *s,const char *label){for(int i=0;i<s->count;i++)if(!strcmp(s->rows[i].row.label,label))return i;return -1;}
/* Every row's label and value fit the card at real metrics (the system font's stand-in), as the focused row draws it. */
static void fit_checks(settings_state *s,bool scrolls)
{
 for(int i=0;i<s->count;i++){ui_card_row_t r;card_row(i,s,&r);if(r.heading)continue;
  if(ui_card_row_cut(&r))fprintf(stderr,"cut: page %d '%s' '%s'\n",level(s)->page,r.label,r.value?r.value:"");
  assert(!ui_card_row_cut(&r));}
 if(!scrolls)assert(s->count<=UI_CARD_ROWS);
}
/* Every row of a page for tests/settings_b/fit.py (the Azahar-calibrated width model): label, value, arrows. */
static void fit_dump(settings_state *s,int newer)
{
 for(int i=0;i<s->count;i++){ui_card_row_t r;card_row(i,s,&r);if(r.heading)continue;
  printf("FITROW\t%d\t%d\t%s\t%s\t%d\n",newer,level(s)->page,r.label,r.value?r.value:"",r.arrows);}
}
/* The tree, page by page (Old and New): what each page holds, nothing doubled, Y only where a scope is, fits. */
static void tree_checks(void)
{
 static settings_cached_row cache[SETTINGS_ROWS];
 for(int newer=0;newer<2;newer++) {
  g_new3ds=newer;room_show_hands=false;
  static settings_state s;memset(&s,0,sizeof(s));s.rows=cache;
  assert(push(&s,PAGE_TOP,NULL) && s.count==5);fit_dump(&s,newer);
  static const char *const top[]={"Customize","Systems","Collection","Updates","About"};
  for(int i=0;i<5;i++)assert(!strcmp(s.rows[i].row.label,top[i]) && !strcmp(s.rows[i].row.value,">"));
  fit_checks(&s,false);assert(!reset_scope(&s));
  /* Customize: Room style; the shared shortcuts once (Rewind keys and the button side on the New only) */
  assert(push(&s,PAGE_CUSTOMIZE,NULL));fit_checks(&s,false);assert(reset_scope(&s));
  assert(count_id(&s,APP_LOOK)==1 && count_id(&s,APP_APPEARANCE)==1 && count_id(&s,APP_HOTKEY)==1 && count_id(&s,APP_HOTKEY+1)==newer && count_id(&s,APP_SIDE)==newer && count_id(&s,APP_HANDS)==0);
  for(int i=0;i<s.count;i++)printf("CUSTOMIZE\t%d\t%s\n",newer,s.rows[i].row.label);
  fit_dump(&s,newer);
  room_show_hands=true;build_rows(&s);assert(count_id(&s,APP_HANDS)==1);assert(push(&s,PAGE_HANDS,NULL) && s.count==2 && settings_ui_hands_row());fit_dump(&s,newer);
  assert(pop(&s) && !settings_ui_hands_row());room_show_hands=false;assert(pop(&s));
  /* Systems: each registered system with settings, from the registry (a new one appears by itself) */
  assert(push(&s,PAGE_SYSTEMS,NULL) && s.count==2 && count_id(&s,APP_SYSTEM)==1 && count_id(&s,APP_SYSTEM+1)==1 && count_id(&s,APP_SYSTEM+2)==0);
  fit_checks(&s,false);assert(!reset_scope(&s));
  /* a system: headings, its main rows, More: Advanced + Help; the Advanced rows only on the Advanced page */
  assert(push(&s,PAGE_SYSTEM,&one) && s.ops==&test_ops);assert(reset_scope(&s));
  static const char *const want[]={"Picture","Inherited","Tested","Controls","Equal but overridden","Shared shortcut","More","Advanced","Help"};
  assert(s.count==9);for(int i=0;i<9;i++)assert(!strcmp(s.rows[i].row.label,want[i]));
  assert(s.rows[0].row.kind==BB_ROW_HEADING && s.rows[6].row.kind==BB_ROW_HEADING && level(&s)->selected==1);
  assert(find_label(&s,"Frameskip")<0 && !strcmp(page_title(&s),"First system"));
  assert(push(&s,PAGE_ADVANCED,NULL) && level(&s)->system==&one && s.ops==&test_ops && reset_scope(&s));
  assert(find_label(&s,"Frameskip")>=0 && find_label(&s,"CPU speed")>=0 && find_label(&s,"Inherited")<0 && find_label(&s,"Help")>=0 && count_id(&s,APP_ADVANCED)==0);
  assert(!strcmp(page_title(&s),"Advanced"));
  assert(pop(&s) && s.ops==&test_ops && pop(&s) && !s.ops);   /* the model closes when its pages are left */
  assert(pop(&s));
  assert(push(&s,PAGE_COLLECTION,NULL) && s.count==2 && count_id(&s,APP_FOLDER)==1 && count_id(&s,APP_IMPORT)==1);fit_dump(&s,newer);fit_checks(&s,false);assert(!reset_scope(&s));assert(pop(&s));
  assert(push(&s,PAGE_ABOUT,NULL) && count_id(&s,APP_CREDITS)==1 && count_id(&s,APP_INSTALL)==1 && count_id(&s,APP_WELCOME)==1);fit_dump(&s,newer);fit_checks(&s,false);
  bb_settings_row *w=&s.rows[find_label(&s,"Welcome")].row;welcome_opened=0;activate(&s,w,1,true);assert(welcome_opened==1 && !settings_ui_take_folders());
  /* X on the tour's games card: Settings closes for the games-folder picker, as the Games folder row does */
  welcome_x=true;activate(&s,w,1,true);assert(welcome_opened==2 && settings_ui_take_folders() && !settings_ui_take_folders());welcome_x=false;
  assert(pop(&s) && !pop(&s));   /* B at the top leaves */
  /* no tab bar, Sections, Source marks or reset rows anywhere */
  for(int p=0;p<PAGE_COUNT;p++)assert(!page_titles[p] || (strcmp(page_titles[p],"Sections") && strcmp(page_titles[p],"Source marks")));
 }
 /* the oscilloscope's path fits the glass at the context floor, for the real systems and sections (the host font
    is wider than the 3DS's): the full names, or the short ones where the full do not fit */
 static const bb_system_t ps={.name="PlayStation",.short_name="PS1"},gb={.name="Game Boy Advance",.short_name="GBA"};
 static const char *const sections[]={"Picture","Controls","Saves","Cartridge","Cheats","Speed","Compatibility","Troubleshooting"};
 int shortened=0;
 for(int sys=0;sys<2;sys++)for(unsigned i=0;i<sizeof(sections)/sizeof(*sections);i++){
  static settings_state ss;memset(&ss,0,sizeof(ss));ss.rows=cache;ss.depth=1;ss.levels[0]=(settings_level){.page=PAGE_SYSTEM,.system=sys?&gb:&ps};
  ss.count=1;ss.rows[0].row=(bb_settings_row){.label="Row",.section=sections[i]};
  const char *path=settings_path_fit(&ss,UI_SETTINGS_SCOPE_MAX_W);
  if(ui_text_width(NULL,UI_FONT_CONTEXT,path)>UI_SETTINGS_SCOPE_MAX_W)fprintf(stderr,"scope path too wide: %s\n",path);
  assert(ui_text_width(NULL,UI_FONT_CONTEXT,path)<=UI_SETTINGS_SCOPE_MAX_W && strstr(path,sections[i]));
  shortened+=strncmp(path,sys?"Game":"Play",4)!=0;
 }
 printf("scope paths: %d of 16 use the short system name on the host font\n",shortened);
 g_new3ds=false;
 puts("PASS: settings tree (Old, New): top list of 5 (no scroll), Customize with the shared shortcuts once, Systems from the registry, a system's sectioned list + Advanced + Help, Collection, About + Welcome; Y only on a scope; every label fits");
}
/* B goes up one level and a page reopens on the row it was left on; L / R jump between headings without wrapping. */
static void navigation_checks(void)
{
 static settings_cached_row cache[SETTINGS_ROWS];static settings_state s;memset(&s,0,sizeof(s));s.rows=cache;
 assert(push(&s,PAGE_TOP,NULL));level(&s)->selected=1;
 assert(push(&s,PAGE_SYSTEMS,NULL));level(&s)->selected=1;
 assert(push(&s,PAGE_SYSTEM,&two) && level(&s)->selected==1);
 assert(heading_jump(&s,1) && level(&s)->selected==4);     /* Controls' first row */
 assert(heading_jump(&s,1) && level(&s)->selected==7);     /* More: Advanced */
 assert(!heading_jump(&s,1) && level(&s)->selected==7);    /* the last heading: no wrap */
 assert(heading_jump(&s,-1) && level(&s)->selected==4);
 level(&s)->selected=5;assert(heading_jump(&s,-1) && level(&s)->selected==4);   /* L inside a section: its first row */
 assert(heading_jump(&s,-1) && level(&s)->selected==1 && !heading_jump(&s,-1));
 level(&s)->selected=5;
 assert(pop(&s) && level(&s)->page==PAGE_SYSTEMS && level(&s)->selected==1);
 assert(push(&s,PAGE_SYSTEM,&two) && level(&s)->selected==5);   /* remembered per system */
 assert(pop(&s) && push(&s,PAGE_SYSTEM,&one) && level(&s)->selected==1);   /* the other system: its own memory */
 assert(pop(&s) && pop(&s) && level(&s)->page==PAGE_TOP && level(&s)->selected==1 && !pop(&s));
 system_open(&s,NULL);
 /* a game's own Settings: the system page is the root, titled with the game; B leaves */
 memset(&s,0,sizeof(s));s.rows=cache;s.context=(bb_settings_context){.game=true,.key="k",.title="Crash Bandicoot (USA).cue"};
 ui_clean_title(s.context.title,s.title,sizeof(s.title));   /* as run_model does: the library's name */
 assert(push(&s,PAGE_SYSTEM,&one) && opened && s.depth==1 && !strcmp(page_title(&s),"Crash Bandicoot") && !pop(&s));
 assert(push(&s,PAGE_ADVANCED,NULL) && !strcmp(page_title(&s),"Advanced") && pop(&s) && s.depth==1);
 s.ops->clear(2);assert(cleared==2);s.ops->reset(BB_RESET_CONTROLS);assert(resets==BB_RESET_CONTROLS+1);
 system_open(&s,NULL);
 puts("PASS: navigation: B up one level, rows remembered per page and per system, L/R heading jumps stop at the ends, a game's list is the root");
}
/* The 10 most-used settings: presses from "Settings just opened in the room" (Collection's row is the third). */
static void press_counts(void)
{
 /* top: Customize 0, Systems 1, Collection 2, Updates 3, About 4. PlayStation is the first system, GBA the second;
    on PS1's list Picture's Screen is row 1, Smoothing 2, FPS counter 3; Controls heading 5, Button mappings 6;
    on GBA's Screen size 1, Color correction 3. Customize: Room style 0, FF keys 1. */
 static const struct {const char *what;int presses;const char *how;} table[]={
  {"Room style Day->Night",3,"A, Down, Right"},{"PS1 Screen",4,"Down, A, A, Right"},{"PS1 Smoothing",5,"Down, A, A, Down, Right"},
  {"PS1 FPS counter",6,"Down, A, A, Down, Down, Right"},{"GBA Screen size",5,"Down, A, Down, A, Right"},
  {"GBA Color correction",7,"Down, A, Down, A, Down, Down, Right"},{"PS1 Button mappings (open)",5,"Down, A, A, R, A"},
  {"Games folder (open)",4,"Down, Down, A, A"},{"Updates page (open)",4,"Down x3, A"},{"FF keys",4,"A, Down, Down, Right"}};
 int total=0;for(unsigned i=0;i<sizeof(table)/sizeof(*table);i++){printf("PRESSES\t%s\t%d\t%s\n",table[i].what,table[i].presses,table[i].how);total+=table[i].presses;}
 printf("PRESSES\ttotal\t%d\n",total);
}
static void look_switch_checks(void)
{
 static settings_state s;static settings_cached_row cache[SETTINGS_ROWS];
 s.rows=cache;g_cfg.theme=theme_from_saved_id("night");
 assert(push(&s,PAGE_TOP,NULL) && push(&s,PAGE_CUSTOMIZE,NULL));
 level(&s)->selected=find_label(&s,"Look");int depth=s.depth,selected=level(&s)->selected;
 for(int look=BB_LOOK_PSX3D;look<BB_LOOK_COUNT;look++) {
  bb_settings_row row=s.rows[selected].row;
  assert(activate(&s,&row,1,false));build_rows(&s);
  assert(theme_look()==look && s.depth==depth && level(&s)->page==PAGE_CUSTOMIZE && level(&s)->selected==selected);
  assert(count_id(&s,APP_LOOK)==1 && count_id(&s,APP_APPEARANCE)==1);
 }
 bb_settings_row row=s.rows[selected].row;assert(activate(&s,&row,1,false));build_rows(&s);
 assert(theme_look()==BB_LOOK_ROOM && !strcmp(theme()->id,"night") && count_id(&s,APP_APPEARANCE)==1);
 g_cfg.theme=theme_default();theme_menu_refresh();
 puts("PASS: look switching preserves the current Settings page and selection; Room remembers its lighting");
}
#ifndef BB_SETTINGS_EQ
int main(void){
 look_switch_checks();
 assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 assert(!FT_Set_Pixel_Sizes(title_face,0,30));assert(!FT_Set_Pixel_Sizes(body_face,0,30));
 assert(!strcmp(bb_settings_source_name(BB_SOURCE_RECOMMENDED),"Recommended"));
 assert(!strcmp(bb_settings_source_name(BB_SOURCE_ALL),"All systems"));
 /* Measure actual Unicode glyphs in the SDK substitute, including the middle
    dot used by scope paths. Byte-wise measurement must fail these controls. */
 const char *utf8[]={"A", "\xc2\xb7", "\xc3\xa9", "\xe2\x86\x92", "\xf0\x9f\x8d\x9e"};
 const u32 points[]={65,183,233,8594,127838};
 for(unsigned i=0;i<sizeof(points)/sizeof(*points);i++) {
  C2D_Text text; C2D_TextParse(&text,NULL,utf8[i]);float measured;
  C2D_TextGetDimensions(&text,.5f,.5f,&measured,NULL);
  float expected=.5f*C2D_FontGetCharWidthInfo(NULL,C2D_FontGlyphIndexFromCodePoint(NULL,points[i]))->charWidth;
  assert(fabsf(measured-expected)<.001f);
 }
 tree_checks();navigation_checks();
 state.depth=1;state.levels[0].page=PAGE_TOP;state.rows=calloc(SETTINGS_ROWS,sizeof(*state.rows));active=true;
 assert(settings_ui_debug_key("debug-on")&&settings_ui_debug_capture());
 assert(!settings_ui_debug_key("down"));assert(settings_ui_debug_key("debug-off")&&!settings_ui_debug_capture());active=false;free(state.rows);state.rows=NULL;
 host_menu_frames=4;confirm_step=0;assert(settings_ui_run(true,NULL));host_menu_frames=-1;
 assert(settings_ui_cheats()&&seen_cheats==&test_cheats);
 bb_settings_context stopped={0};host_menu_frames=4;assert(!run_model(&one,&stopped,NULL,NULL,NULL));host_menu_frames=-1;
 // Library SELECT and remote Settings pass the menu dispatcher itself.
 // Exercise the real Settings top and ui.c dispatcher, not only its card.
 host_render_top=true;host_menu_frames=4;confirm_step=0;
 assert(!settings_ui_run(false,ui_menu_top));host_menu_frames=-1;
 assert(host_scene_count>0);assert(ui_dialog_scene_set(NULL)==NULL);
 ui_destination parent={.userdata=&parent_frames,.draw_top=parent_top};
 assert(ui_dialog_scene_set(&parent)==NULL);
 host_menu_frames=4;confirm_step=0;assert(!settings_ui_run(false,ui_menu_top));host_menu_frames=-1;
 assert(parent_frames==0);assert(ui_dialog_scene_set(NULL)==&parent);
 // An indirect alias also cycles through the dispatcher: no pointer special case.
 unsigned painted=host_scene_count;host_menu_frames=4;confirm_step=0;
 bb_settings_run(NULL,&stopped,dispatcher_alias,NULL);host_menu_frames=-1;
 assert(host_scene_count>painted);
 // Room entry/exit/tick and its concrete non-NULL backdrop.
 parent_frames=0;host_menu_key=0;host_menu_frames=20;confirm_step=0;
 ui_destination room={&parent_frames,room_enter_test,room_leave_test,room_tick_test,room_settled_test,close_top};
 bb_settings_room_run(&room);host_menu_frames=-1;host_menu_key=0;
 assert(parent_frames && room_enters==1 && room_leaves==1 && room_ticks);
 assert(ui_dialog_scene_set(NULL)==NULL);
 // X Options: the real game-scoped wrapper, concrete top, and nested-run refusal.
 resume_requested=false;parent_frames=0;host_menu_frames=20;confirm_step=0;
 bb_settings_game(&one,"test.gba","test","Test",nested_top,&parent_frames);
 host_menu_frames=-1;host_menu_key=0;assert(parent_frames && !active && !state.rows);
#ifdef SHARE_TOP_TEST
 parent_frames=0;host_menu_frames=20;confirm_step=0;
 bb_settings_run(NULL,&stopped,share_over_settings,NULL);
 host_menu_frames=-1;host_menu_key=0;assert(parent_frames && !active);
#endif
 puts("Settings backdrops PASS: indirect dispatcher cycle, nested refusal, room lifecycle, X Options concrete top, production Share top over Settings");
 maximum_value_checks();paging_reset_input_checks();value_inspection_checks();room_cue_checks();paper_line_checks();flicker_checks();press_counts();
 host_render_top=false;
 FT_Done_Face(title_face);FT_Done_Face(body_face);FT_Done_FreeType(ft);
 puts("Settings production pages, navigation, game scope, sources, highlighter and flicker regression PASS");return 0;
}

#endif
