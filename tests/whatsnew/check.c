#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "app_paths.h"
#include "whatsnew.h"
#include "whatsnew_text.h"
#include "ui_text_pages.h"

static bool saved=true;
char config_seen_version[BB_SEEN_VERSION_CAP];
static int writes;
void config_save(void){writes++;}
bool config_last_save_ok(void){return saved;}

int main(void)
{
 assert(!bb_whatsnew_changed(false,"","0.9.3","A release."));
 assert(bb_whatsnew_changed(true,"","0.9.3","A release."));
 assert(bb_whatsnew_changed(false,"0.9.2","0.9.3","A release."));
 assert(!bb_whatsnew_changed(true,"0.9.3","0.9.3","A release."));
 assert(bb_whatsnew_changed(true,"0.9.30","0.9.3","A release."));
 assert(bb_whatsnew_changed(true,"0.9.3","0.9.30","A release."));
 assert(bb_whatsnew_changed(true,"0.9.3-beta","0.9.3","A release."));
 assert(!bb_whatsnew_changed(true,"0.9.2","0.9.3",NULL));
 assert(!bb_whatsnew_changed(true,"0.9.2","0.9.3",""));
 assert(!bb_whatsnew_changed(true,"0.9.2","0.9.3"," \n\t"));
 assert(!bb_whatsnew_changed(true,"0.9.2","","A release."));
 bb_whatsnew_boot(false);assert(!bb_whatsnew_take() && writes==1);
 assert(!strcmp(config_seen_version,BB_CHANGELOG_VERSION));
 bb_whatsnew_boot(true);assert(!bb_whatsnew_take() && writes==1);
 config_seen_version[0]=0;bb_whatsnew_boot(true);
 assert(bb_whatsnew_take() && !bb_whatsnew_take());
 bb_whatsnew_boot(true);assert(!bb_whatsnew_take());
 strcpy(config_seen_version,"earlier");saved=false;bb_whatsnew_boot(true);
 assert(!bb_whatsnew_take() && !strcmp(config_seen_version,"earlier"));
 saved=true;config_seen_version[0]=0;
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(APP_SD_ROOT,0700);mkdir(PSX3D_SAVE_DIR,0700);
 bb_whatsnew_boot(false);assert(!bb_whatsnew_take());
 FILE *f=fopen(PSX3D_SAVE_DIR "/game.sav","w");assert(f);fputs("save",f);fclose(f);
 config_seen_version[0]=0;bb_whatsnew_boot(false);assert(bb_whatsnew_take());
 assert(ui_text_page_count(6,6,0)==1 && ui_text_page_count(7,6,0)==2);
 assert(ui_text_page_count(9,9,1)==1 && ui_text_page_count(10,9,1)==2 && ui_text_page_count(17,9,1)==3);
 puts("What's new startup decisions and shared text helpers: PASS");
}
