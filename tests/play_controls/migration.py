"""Execute the real cold migration against temporary legacy profiles."""
from pathlib import Path
import os, subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/config.c').read_text();body=s[s.index('static bool legacy_left_file'):s.index('void config_load(void)')]
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as td:
 p=Path(td);source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#define APP_SD_ROOT "box"
#define PATH_LEN 512
#define PSX3D_CONFIG "box/config.ini"
static struct {int touch_click_left;} g_cfg;
static unsigned saves;
static void config_user_changed(const char *key){assert(!strcmp(key,"touch_click_left"));}
static void config_save(void){saves++;}
static FILE *safe_begin(const char *path){return fopen(path,"w");}
static void safe_finish(FILE *f,const char *p,bool ok){(void)p;assert(ok);fclose(f);}
'''+body+r'''
static void put(const char *p,const char *text){FILE *f=fopen(p,"w");assert(f);fputs(text,f);fclose(f);}
int main(void){
 mkdir("box",0700);mkdir("box/games",0700);mkdir("box/config",0700);
 put("box/games/old.ini","touch_click_left=1\n");migrate_touch_side();assert(g_cfg.touch_click_left && saves==1);
 g_cfg.touch_click_left=0;migrate_touch_side();assert(!g_cfg.touch_click_left && saves==1);
 remove("box/config/touch-side-migrated-v1");remove("box/games/old.ini");
 put("box/config/gba.ini","touch_side=1\n");migrate_touch_side();assert(g_cfg.touch_click_left && saves==2);
 puts("PASS: legacy PS1 game/GBA system side migrates once; subsequent right-side choice survives");
}
''';(p/'check.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)
