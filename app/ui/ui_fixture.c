/* Explicit development-build visual fixtures, never enabled in release builds. */
#ifdef BB_UI_FIXTURES
#include <string.h>
#include <stdlib.h>
#include "ui_fixture.h"
#include "devtools.h"
#include "cold_code.h"
#include "ui.h"
#include <stdio.h>
#include "app_config.h"
#include "whatsnew.h"
static int notes,notice;
PSX3D_COLD bool ui_fixture_command(const char *command,const char *args)
{
 if(strcmp(command,"ui-fixture"))return false;
 if(!strncmp(args,"seen=",5))snprintf(config_seen_version,BB_SEEN_VERSION_CAP,"%s",args+5);
 if(!strcmp(args,"upgrade") && !config_in_game()) {
  snprintf(config_seen_version,BB_SEEN_VERSION_CAP,"0.9.1");config_save();bb_whatsnew_boot(true);
 }
 if(!strcmp(args,"offer")) {
  dev_reply("{\"ok\":true}");
  ui_confirm_choice("Update available","BreadBox can update now. Your saved games will be kept.","A  Update","B  Later",false);return true;
 }
 if(!strcmp(args,"metrics")) {
  char reply[2048];int at=snprintf(reply,sizeof(reply),"{\"advance\":[");
  for(int i=32;i<127;i++){char text[2]={(char)i,0};at+=snprintf(reply+at,sizeof(reply)-at,"%s%.3f",i==32?"":",",ui_text_width(NULL,1,text));}
  snprintf(reply+at,sizeof(reply)-at,"]}");dev_reply(reply);return true;
 }
 if(!strncmp(args,"notes=",6)){notes=atoi(args+6);if(notes!=3 && notes!=12)notes=0;}
 else if(!strncmp(args,"notice=",7))notice=atoi(args+7);
 dev_reply("{\"ok\":true,\"fixture\":\"BB_UI_FIXTURES=1\"}");return true;
}
PSX3D_COLD int ui_fixture_notes(void){return notes;}
PSX3D_COLD bool ui_fixture_problem(void){return notice==3 || notice>=6;}
PSX3D_COLD const char *ui_fixture_notice(void)
{
 static const char *const lines[]={0,"Update found","Ready. Reopen to install","Battery low. Plug in","Opening your collection","Updating in 3s. Any key: later","Free 207 MB on SD card","Off while remote is on","Needs the installed app"};
 return notice>0 && notice<(int)(sizeof(lines)/sizeof(*lines))?lines[notice]:0;
}
#endif
