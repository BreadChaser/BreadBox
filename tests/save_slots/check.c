/* Save slot postcards, service side: the slot browser's one-time reads.
 * A save writes its picture beside the state; slot info returns it with the
 * date and play time; a picture that no longer matches its state is never
 * shown; Continue has a picture; tossing a slot moves it to the backups.
 * GPL-2.0-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bb_save.h"
bool breadbox_import_busy(void){return false;}
#include "safefile.h"
#include "history.h"
static unsigned char ram[128],position;
static bool have_picture=true;
static uint16_t shade;
void log_printf(const char *fmt,...){(void)fmt;}
static size_t size(void){return 16;}
static bool save(void *p,size_t n,size_t *len){memset(p,position,n);*len=n;return true;}
static bb_state_result load(const void *p,size_t n,bool keep){(void)n;(void)keep;position=*(const unsigned char *)p;return BB_STATE_OK;}
static bool battery(void **p,size_t *n){*p=ram;*n=sizeof(ram);return true;}
static bool picture(uint16_t *px,unsigned *w,unsigned *h)
{
 if(!have_picture)return false;
 for(unsigned i=0;i<240*160;i++)px[i]=(uint16_t)(shade+i);
 *w=240;*h=160;return true;
}
static uint32_t played(void){return 3725;}
static const bb_save_ops ops={.state_size=size,.state_save=save,.state_load=load,.battery=battery,.state_magic="BBGBAST1"};
static const bb_system_t test_system={.id="test",.save=&ops};
static bb_save_config config={.system=&test_system,.key="slots",.battery_path="battery.sav",.state_base="game",
 .identity=7,.content_bytes=64,.writer_core=1,.picture=picture,.played=played};
static uint16_t px[BB_THUMB_MAX_W*BB_THUMB_MAX_H];
static uint32_t picture_memory[BB_THUMB_BYTES/4];
static bool exists(const char *p){return !access(p,F_OK);}
static void card(unsigned slot,bool used,unsigned w,uint16_t first)
{
 bb_slot_info in;memset(px,0,sizeof(px));
 assert(bb_save_slot_info(slot,&in,px));
 assert(in.used==used);
 assert(in.w==w && in.h==(w?160u:0u));
 if(w){assert(px[0]==first && px[240*160-1]==(uint16_t)(first+240*160-1));assert(in.played==3725 && in.saved>0);}
}
int main(int argc,char **argv)
{
 assert(argc==2);assert(!chdir(argv[1]));history_set_root("history");
 config.picture_buffer=picture_memory;
 char error[128];assert(bb_save_begin(&config,error,sizeof(error)));
 /* empty board: nothing used, no pictures, no files created by looking */
 for(unsigned i=0;i<=10;i++)card(i,false,0,0);
 assert(!exists("game.state0.thm"));
 /* a save leaves its postcard picture beside the state */
 shade=100;position=1;assert(bb_save_state(3,true));
 assert(exists("game.state3.thm"));card(3,true,240,100);
 unsigned w,h;assert(bb_save_picture(px,&w,&h) && w==240 && h==160);
 /* saving over it replaces the picture with the new moment */
 shade=200;position=2;assert(bb_save_state(3,true));card(3,true,240,200);
 /* no picture now: the old one must not date or illustrate the new state */
 have_picture=false;position=3;assert(bb_save_state(3,true));
 assert(!exists("game.state3.thm"));card(3,true,0,0);have_picture=true;
 /* a picture that does not belong to its state is never shown */
 shade=300;position=4;assert(bb_save_state(4,true));card(4,true,240,300);
 FILE *f=fopen("game.state4.thm","r+b");assert(f);assert(!fseek(f,8,SEEK_SET));fputc(0x5a,f);fclose(f);
 card(4,true,0,0);
 /* Continue (slot 10) is a postcard too */
 shade=400;position=5;assert(bb_save_checkpoint(true));card(10,true,240,400);
 /* the info read does not need a pixel buffer */
 bb_slot_info in;assert(bb_save_slot_info(10,&in,NULL) && in.used && in.w==240);
 /* tossing: the state goes to the backups, the slot and its sidecars are gone */
 assert(bb_save_delete(4));card(4,false,0,0);
 assert(!exists("game.state4") && !exists("game.state4.bak") && !exists("game.state4.thm") && !exists("game.state4.time"));
 hist_entry_t list[8];int n=history_list("test-slots",false,list,8);
 bool found=false;for(int i=0;i<n;i++)found|=list[i].kind==HIST_DELETE && !strcmp(list[i].ext,".state4");
 assert(found);
 /* Continue and Undo are never tossed from the board */
 assert(!bb_save_delete(10) && !bb_save_delete(11));card(10,true,240,400);
 /* a slot whose current file is missing but whose backup survives still shows as used */
 shade=500;position=6;assert(bb_save_state(5,true));position=7;assert(bb_save_state(5,true));
 assert(!rename("game.state5","game.state5.gone"));
 assert(bb_save_slot_info(5,&in,px) && in.used && in.w==0);
 assert(bb_save_end(false));
 puts("Save slot postcards: pictures beside states, stale pictures hidden, Continue card, toss to backups PASS");
 return 0;
}
