#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "app_config.h"
#include "app_paths.h"
#include "cold_code.h"
#include "whatsnew.h"
#include "whatsnew_text.h"

static bool pending;

static PSX3D_COLD bool earlier_data(void)
{
 static const char *const dirs[]={APP_SD_ROOT "/saves",APP_SD_ROOT "/states",APP_SD_ROOT "/games",
  APP_SD_ROOT "/config",APP_SD_ROOT "/updates"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++) {
  DIR *d=opendir(dirs[i]);if(!d)continue;
  struct dirent *entry;bool found=false;
  while((entry=readdir(d)))if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..") &&
   strcmp(entry->d_name,"touch-side-migrated-v1")){found=true;break;}
  closedir(d);if(found)return true;
 }
 return false;
}

PSX3D_COLD void bb_whatsnew_boot(bool had_config)
{
 pending=bb_whatsnew_changed(had_config || earlier_data(),config_seen_version,BB_CHANGELOG_VERSION,BB_CHANGELOG_TEXT);
 if(!strcmp(config_seen_version,BB_CHANGELOG_VERSION))return;
 char before[BB_SEEN_VERSION_CAP];memcpy(before,config_seen_version,sizeof(before));
 snprintf(config_seen_version,sizeof(config_seen_version),"%s",BB_CHANGELOG_VERSION);
 config_save();
 if(!config_last_save_ok()){memcpy(config_seen_version,before,sizeof(before));pending=false;}
}

PSX3D_COLD bool bb_whatsnew_take(void){bool due=pending;pending=false;return due;}
