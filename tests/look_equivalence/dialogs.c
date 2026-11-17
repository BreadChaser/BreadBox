/* Shared dialogs execute their real input loops against a finite HID stream. */
#define BB_UI_TRACE_HOST
#define BB_MENU_SCRIPT
#define main host_contract_main
#include "../app/host/check.c"
#undef main
#include "../../app/ui/ui_trace.c"
#include "../../app/ui/card_scroll.c"
bool ps1_3d_available(void){return g_new3ds;}
bool bb_session_active(void){return false;}
static unsigned sound,step;
void sfx_play(int n){sound=sound*16777619u+(unsigned)n+1;}
void sfx_now(int n){sfx_play(n);}void sfx_default(int n){sfx_play(n);}
void sfx_cancel(void){}void sfx_flush(void){}void sfx_card_closes(void){}
bool sfx_close_said(void){return false;}
static const char *script,*scenario;static unsigned frame;
static bool host_script_poll(void){return script[frame]!=0;}
static void host_script_input(void)
{
 char c=script[frame];assert(c);frame++;host_script_tick+=SYSCLOCK_ARM11/4;ui_resume_sample(ui_resume_pending);
 fake_down=c=='a'?KEY_A:c=='b'?KEY_B:c=='x'?KEY_X:c=='d'?KEY_DOWN:c=='u'?KEY_UP:c=='l'?KEY_LEFT:c=='r'?KEY_RIGHT:c=='t'?KEY_TOUCH:0;
 fake_held=fake_down;host_menu_key=0;
}
static void host_script_present(void)
{printf("%u %s %u %s sound=%u\n",step++,scenario,frame,ui_trace_json(),sound);}
static void setup(const char *name,const char *keys)
{scenario=name;script=keys;frame=0;fake_down=fake_held=0;confirm_step=0;ui_resume_pending=swap_keys=swap_buffer=0;}
static void result(int answer){printf("%u %s result=%d sound=%u\n",step++,scenario,answer,sound);}

#if !defined(BB_PAUSE_EQ) && !defined(BB_CARDS_EQ) && !defined(BB_WELCOME_EQ) && !defined(BB_SHARE_EQ) && !defined(BB_PS1_EQ) && !defined(BB_FEATURES_EQ) && !defined(BB_BROWSER_EQ)
int main(int argc,char **argv)
{
 assert(argc==2 && !FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
 static const char *const titles[]={"Quit BreadBox?","Reset game?","Delete postcard?","Replace postcard?","Update available","Could not start game","Collection updated","Install BreadBox","Button mappings"};
 for(unsigned i=0;i<sizeof(titles)/sizeof(*titles);i++)for(int answer=0;answer<2;answer++){
  setup(titles[i],answer?"..a":"..b");result(ui_confirm_choice(titles[i],"Your saved games will be kept.","A  Continue","B  Back",false));
 }
 setup("Notice A","..a");ui_confirm_ok("Notice","Collection updated",false);result(1);
 setup("Notice B","..b");ui_confirm_ok("Notice","Collection updated",false);result(0);
 setup("Quit safe default","..a");result(ui_confirm_pick("Quit?","Leave BreadBox?","Quit","Stay"));
 setup("Quit left confirm","..l.a");result(ui_confirm_pick("Quit?","Leave BreadBox?","Quit","Stay"));
 setup("Quit touch confirm","..t.a");fake_touch=(touchPosition){80,220};result(ui_confirm_pick("Quit?","Leave BreadBox?","Quit","Stay"));
 setup("Quit B","..b");result(ui_confirm_pick("Quit?","Leave BreadBox?","Quit","Stay"));
 char text[3000]={0};for(int i=0;i<30;i++)strcat(text,"One line of help.\n");
 setup("Help paging","..d..u..a..b");result(ui_confirm_choice("Help",text,"A  Done","B  Back",false));
 setup("Dialog touch","..t");fake_touch=(touchPosition){240,220};result(ui_confirm_choice("Update available","Saved games stay.","A  Update","B  Later",false));
 return 0;
}

#endif
