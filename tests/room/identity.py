#!/usr/bin/env python3
"""Execute the shelf's production identity lookup and launch after a catalog reorder."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'app/ui/shelf.c').read_text()
lookup=s[s.index('static int box_item('):s.index('static int sort_mode;')]
launch=s[s.index('static const bb_system_t *launched_sys;'):s.index('static const ui_button_t bar_all')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {char key[256],path[128];int discs;} bb_catalog_item;
typedef struct {bool (*launch)(int,char*,size_t);} bb_shell_ops;
typedef struct {const bb_shell_ops *shell;} bb_system_t;
typedef struct {const bb_system_t *system;int row;char key[64];} shelf_box;
static int order[]={0,1},selected=-1;
static int bb_system_library_count(const bb_system_t*s){(void)s;return 2;}
static bool item_of(const bb_system_t*s,int row,bb_catalog_item*i){(void)s;snprintf(i->key,sizeof(i->key),"game-%d",order[row]);snprintf(i->path,sizeof(i->path),"/game-%d",order[row]);i->discs=1;return true;}
static void key_of(const bb_system_t*s,bb_catalog_item*i){(void)s;(void)i;}
static bool adapter_launch(int row,char*p,size_t cap){selected=row;return snprintf(p,cap,"/game-%d",order[row])<(int)cap;}
static const bb_shell_ops ops={adapter_launch};static const bb_system_t system={&ops};
static shelf_box box={&system,0,"game-0"};static int view;
static const shelf_box *shelf_pulled_box(const int*v){(void)v;return &box;}
'''
main=r'''
int main(void){char path[128];assert(launch(path,sizeof(path)) && selected==0);
 order[0]=1;order[1]=0;assert(launch(path,sizeof(path)) && selected==1 && !strcmp(path,"/game-0"));
 assert(launched_row==1);order[1]=2;selected=-1;assert(!launch(path,sizeof(path)) && selected==-1);
 puts("Production held-box identity: reordered catalog retargets Play; removed game never launches another title PASS");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+lookup+launch+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
