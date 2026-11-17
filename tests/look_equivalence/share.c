/* Production Share controller; network transport is the external boundary. */
#define BB_SHARE_EQ
#include "dialogs.c"
#include "qrcode.h"
#define RELEASE_URL "https://github.com/BreadChaser/BreadBox/releases/latest/download/BreadBox.cia"
static bool serving,server_stop,local_available;
static unsigned starts,stops,help,credits;
static char cia_note[160],url[96]="http://localhost/BreadBox.cia";
static bool share_start(void){starts++;return serving=local_available;}
static void share_stop(void){if(serving)stops++;serving=false;}
void bb_install_help_run(void){help++;ui_confirm_ok("Install BreadBox","Open FBI, then Remote Install.",false);}
void bb_credits_run(void){credits++;ui_confirm_ok("Credits","BreadBox is free software.",false);}
#include "share-controller.inc"
static void draw_top(void *unused){(void)unused;}
static void row(int i,void *ud,ui_card_row_t *r){r->label=share_label(i,ud);r->value=share_value(i,ud);}
static void draw_bottom(void *unused){(void)unused;if(share_qr){ui_trace_qr(share_label(share_sel,NULL));return;}ui_card_t c={.title="Share"};ui_card_draw(&share_list,share_n,share_sel,row,NULL,&c);static const ui_button_t b[]={{KEY_B,"Back",UI_ICON_BACK,false},{KEY_A,"Select",UI_ICON_OPEN,false}};ui_bar_draw(b,2);}
static void outcome(void){printf("%u %s focus=%d starts=%u stops=%u help=%u credits=%u serving=%d sound=%u\n",step++,scenario,share_sel,starts,stops,help,credits,serving,sound);}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
 setup("share-qr","....a....b....b....");share_about_run(false);outcome();
 setup("share-local-missing","....d....a....b....");share_about_run(false);outcome();
 local_available=true;setup("share-local","....d....a....b....a....x....b....");share_about_run(false);outcome();
 setup("share-help-credits","....d....d....a....a....d....a....b....b....");share_about_run(false);outcome();
 setup("share-touch","....t....a....b....");fake_touch=(touchPosition){100,UI_CARD_ROWS_Y+UI_CARD_ROW+UI_CARD_ROW/2};share_about_run(false);outcome();
 assert(starts>=2 && stops>=1 && help>=1 && credits>=1);return 0;
}
