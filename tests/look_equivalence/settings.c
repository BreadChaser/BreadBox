/* Compare the production Settings controller, input, cards and config writer. */
#define BB_UI_TRACE_HOST
#define BB_SETTINGS_EQ
#define main settings_contract_main
#include "../settings_b/model.c"
#undef main
#include "../../app/ui/ui_trace.c"
bool ps1_3d_available(void){return g_new3ds;}
static unsigned step;
static uint32_t file_hash(void)
{
 FILE *f=fopen(PSX3D_CONFIG,"rb");if(!f)return 0;
 uint32_t h=2166136261u;char line[1024];
 while(fgets(line,sizeof(line),f))if(strncmp(line,"theme=",6))for(const char *p=line;*p;p++)h=(h^(unsigned char)*p)*16777619u;
 fclose(f);return h;
}
static void snapshot(const char *event)
{
 draw_bottom(&state);
 /* The active look value is intentionally different; every other field is compared. */
 if(level(&state)->page==PAGE_CUSTOMIZE) {
  ui_trace_begin("Customize",state.count,level(&state)->selected,(int)state.list.scroll_target);
  for(int i=0;i<state.count;i++)ui_trace_row(i,state.rows[i].row.label,i?state.rows[i].row.value:"Theme",state.rows[i].row.kind==BB_ROW_HEADING);
  ui_button_t b[UI_BAR_MAX];int n=settings_bar(&state,b);for(int i=0;i<n;i++)ui_trace_button(b[i].key,b[i].label,0,0,0,0);
 }
 printf("%u %s %s effects=%d/%d/%d/%d/%d/%d file=%u\n",step++,event,ui_trace_json(),changed,cleared,resets,updates_opened,welcome_opened,folders_requested,file_hash());
}
static bool key(u32 k,const char *name)
{
 fake_down=k;fake_held=0;host_menu_key=0;input_poll();bool resume=false;
 bool running=settings_act(&state,settings_keys(&state),NULL,&resume);
 if(running){ui_card_update(&state.list,level(&state)->selected,state.count);flist_snap(&state.list,level(&state)->selected,state.count,UI_CARD_ROWS);snapshot(name);}
 return running;
}
static void target(int id)
{
 int row=-1;for(int i=0;i<state.count;i++)if(state.rows[i].row.id==id)row=i;
 assert(row>=0);int guard=0;
 while(level(&state)->selected!=row){assert(++guard<200);key(level(&state)->selected<row?KEY_DOWN:KEY_UP,"focus");}
}
static void touch_focus(int id)
{
 target(id);int i=level(&state)->selected;
 fake_touch=(touchPosition){UI_CARD_X+20,UI_CARD_ROWS_Y+(i-(int)state.list.scroll)*UI_CARD_ROW+UI_CARD_ROW/2};
 key(KEY_TOUCH,"touch");
}
int main(int argc,char **argv)
{
 assert(argc==2);assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);
 host_menu_frames=100000;confirm_step=0;config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);g_new3ds=true;room_show_hands=true;
 resume_requested=false;state=(settings_state){0};state.rows=calloc(SETTINGS_ROWS,sizeof(*state.rows));assert(state.rows);active=true;push(&state,PAGE_TOP,NULL);snapshot("open");
 target(APP_CUSTOMIZE);key(KEY_A,"open-customize");target(APP_THEME);key(KEY_RIGHT,"theme");key(KEY_LEFT,"theme-back");
 target(APP_HANDS);key(KEY_A,"open-hands");key(KEY_RIGHT,"skin");key(KEY_DOWN,"gloves-focus");key(KEY_RIGHT,"gloves");key(KEY_B,"back-customize");
 target(APP_HOTKEY);key(KEY_RIGHT,"hotkey");target(APP_SIDE);key(KEY_RIGHT,"touch-side");key(KEY_LEFT,"touch-side-back");key(KEY_B,"back-settings");
 target(APP_SYSTEMS);touch_focus(APP_SYSTEMS);
 for(int system=0;system<2;system++) {
  target(APP_SYSTEM+system);key(KEY_A,"open-system");
  for(int id=0;id<4;id++){target(id);key(KEY_RIGHT,"value");key(KEY_LEFT,"value-back");if(id!=3)key(KEY_X,"default");}
  key(KEY_R,"next-section");key(KEY_L,"previous-section");target(APP_ADVANCED);key(KEY_A,"advanced");target(4);key(KEY_RIGHT,"advanced-value");key(KEY_DOWN,"advanced-next");key(KEY_B,"back-system");key(KEY_B,"back-systems");
 }
 key(KEY_B,"back-settings");target(APP_COLLECTION);key(KEY_A,"collection");target(APP_IMPORT);key(KEY_A,"import");key(KEY_B,"back-settings");
 target(APP_UPDATES);key(KEY_A,"updates");target(APP_ABOUT);key(KEY_A,"about");target(APP_CREDITS);key(KEY_A,"credits");target(APP_INSTALL);key(KEY_A,"install-help");target(APP_WELCOME);key(KEY_A,"welcome");key(KEY_B,"back-settings");
 assert(!key(KEY_B,"close"));system_open(&state,NULL);free(state.rows);state.rows=NULL;active=false;
 printf("END %u file=%u\n",step,file_hash());return 0;
}
