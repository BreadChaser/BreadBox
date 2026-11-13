#!/usr/bin/env python3
"""Run production shelf build/selection through partial and complete catalogs."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf.c').read_text()
start=s.index('static int row_of(')
end=s.index('static void pull(',start)
code=s[start:end]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX_ROWS 8
#define BB_KEY_LEN 32
#define MAX_BOXES 16
#define BB_MAX_SYSTEMS 2
typedef uint32_t u32;
typedef struct {const char *name;} bb_system_t;
typedef struct {int row;char key[16];} bb_catalog_item;
typedef struct {const bb_system_t *system;int row;u32 last,seconds;bool fav;char title[32],key[32];} shelf_box;
typedef struct {const bb_system_t *system;const char *name;shelf_box *box;int n;char plate[24];} shelf_row;
static shelf_row rows[MAX_ROWS];static shelf_box data[MAX_BOXES],*boxes=data;
static int nboxes;static unsigned generation;static bool favorite_rebuild_pending;
static struct {shelf_row *rows;int nrows,total,row,hsel,hcur,prow,sel[8],pidx,layout;float pull,pto;char last_title[32];} view;
static bb_system_t systems[]={{"First"},{"Second"}};static int counts[]={5,3};static int prepend;
static unsigned library_key(void){return counts[0]+counts[1];}
static unsigned bb_system_display_order(const bb_system_t **out,unsigned cap,bool visible){
 (void)visible;unsigned n=0;for(unsigned i=0;i<2 && n<cap;i++)if(counts[i])out[n++]=&systems[i];return n;
}
static int bb_system_library_count(const bb_system_t *s){return counts[s-systems];}
static bool item_of(const bb_system_t *s,int row,bb_catalog_item *out){(void)s;out->row=row;return true;}
static void key_of(const bb_system_t *s,bb_catalog_item *out){(void)s;(void)out;}
static void fill_box(shelf_box *b,const bb_system_t *s,int row,bb_catalog_item *item,int n){
 (void)item;(void)n;*b=(shelf_box){.system=s,.row=row};snprintf(b->title,sizeof(b->title),"Game %d",row-prepend);snprintf(b->key,sizeof(b->key),"key-%d",row-prepend);
}
static void lay_out(void){view.layout++;}
'''
main=r'''
int main(void){
 favorite_rebuild_pending=true;build();assert(!favorite_rebuild_pending);view.sel[1]=4;view.sel[2]=2;view.row=view.hsel=1;
 counts[0]=1;build();assert(view.sel[1]==0 && sel_recall(&systems[0])==4);
 build();assert(view.sel[1]==0 && sel_recall(&systems[0])==4);
 counts[0]=0;build();assert(view.nrows==2 && sel_recall(&systems[0])==4);
 counts[0]=5;build();assert(view.sel[1]==4 && view.sel[2]==2);
 counts[0]=3;build();assert(view.sel[1]==2 && sel_recall(&systems[0])==4);
 // A real navigation choice in the partial row supersedes the held selection.
 view.sel[1]=1;build();counts[0]=5;build();assert(view.sel[1]==1);
 // A-Z publication inserts games before the selected stable title.
 view.sel[1]=2;build();prepend=2;counts[0]=7;build();
 assert(view.sel[1]==4 && !strcmp(rows[1].box[4].key,"key-2"));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+code+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production shelf build preserves pre-clamp selection across partial and missing rows PASS')
