/* Twelve save slots (shown 1..12): the mapping to files, and the save service behind it.
 * Slots 1..10 are the files they always were; 11 and 12 are new files that collide with no
 * Continue / Undo / quick state. GPL-2.0-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bb_save.h"
bool breadbox_import_busy(void){return false;}
#include "safefile.h"
#include "history.h"
static unsigned char ram[128],position;
void log_printf(const char *fmt,...){(void)fmt;}
static size_t size(void){return 16;}
static bool save(void *p,size_t n,size_t *len){memset(p,position,n);*len=n;return true;}
static bb_state_result load(const void *p,size_t n,bool keep){(void)n;(void)keep;position=*(const unsigned char *)p;return BB_STATE_OK;}
static bool battery(void **p,size_t *n){*p=ram;*n=sizeof(ram);return true;}
static bool picture(uint16_t *px,unsigned *w,unsigned *h){for(unsigned i=0;i<240*160;i++)px[i]=(uint16_t)i;*w=240;*h=160;return true;}
static const bb_save_ops ops={.state_size=size,.state_save=save,.state_load=load,.battery=battery,.state_magic="BBGBAST1"};
static const bb_system_t test_system={.id="test",.save=&ops};
static bb_save_config config={.system=&test_system,.key="map",.battery_path="battery.sav",.state_base="game",
 .identity=7,.content_bytes=64,.writer_core=1,.picture=picture};
static uint32_t picture_memory[BB_THUMB_BYTES/4];
static bool exists(const char *p){return !access(p,F_OK);}
int main(int argc,char **argv)
{
 /* the mapping: a bijection between the twelve cards and twelve file indices */
 assert(BB_SLOTS==12 && bb_slot_shown(0)==1 && bb_slot_shown(11)==12);
 bool seen[BB_SLOT_LAST+1]={0};
 for(int card=0;card<BB_SLOTS;card++){
  int file=bb_slot_file(card);
  assert(file>=0 && file<=BB_SLOT_LAST && !seen[file]);seen[file]=true;
  assert(bb_slot_card(file)==card);                  /* and back */
  if(card<10)assert(file==card);                     /* slots 1..10: the files they always were, no migration */
  else assert(file!=BB_SLOT_CONTINUE && file!=BB_SLOT_UNDO && file!=BB_SLOT_QUICK);
  assert(bb_ps1_slot_file(card)==card && bb_ps1_slot_card(card)==card);   /* PS1: .st0 .. .st11 */
 }
 assert(bb_slot_file(10)==13 && bb_slot_file(11)==14);
 assert(bb_slot_card(BB_SLOT_CONTINUE)<0 && bb_slot_card(BB_SLOT_UNDO)<0 && bb_slot_card(BB_SLOT_QUICK)<0);
 assert(bb_slot_file(-1)<0 && bb_slot_file(BB_SLOTS)<0 && bb_slot_card(15)<0 && bb_slot_card(-1)<0);
 assert(bb_ps1_slot_file(12)<0 && bb_ps1_slot_card(12)<0);
 /* the save service: the new files are written, picture and all, and tossed; Continue stays out of reach */
 assert(argc==2);assert(!chdir(argv[1]));history_set_root("history");
 config.picture_buffer=picture_memory;
 char error[128];assert(bb_save_begin(&config,error,sizeof(error)));
 position=1;assert(bb_save_state(3,true));                              /* slot 4: .state3, as before */
 position=2;assert(bb_save_state(bb_slot_file(10),true));                /* slot 11 */
 position=3;assert(bb_save_state(bb_slot_file(11),true));                /* slot 12 */
 position=4;assert(bb_save_checkpoint(true));                             /* Continue: .state10 */
 bb_save_settle();
 assert(exists("game.state3") && exists("game.state13") && exists("game.state14") && exists("game.state10"));
 assert(exists("game.state13.thm") && exists("game.state14.thm") && exists("game.state10.thm"));
 assert(!exists("game.state11") && !exists("game.state12"));              /* Undo and quick keep their own files */
 bb_slot_info in;
 assert(bb_save_slot_info(13,&in,NULL) && in.used && in.w==240);
 assert(bb_save_slot_info(14,&in,NULL) && in.used);
 assert(bb_save_slot_info(10,&in,NULL) && in.used);                       /* Continue is its own card */
 position=0;assert(bb_save_state(13,false) && position==2);              /* a load reads slot 11 back */
 assert(bb_save_delete(13) && !exists("game.state13") && !exists("game.state13.thm"));
 assert(bb_save_slot_info(13,&in,NULL) && !in.used);
 assert(!bb_save_delete(10) && !bb_save_delete(11) && !bb_save_delete(12) && exists("game.state10"));
 assert(!bb_save_state(15,true) && !bb_save_state(99,true));              /* nothing past the last file index */
 assert(bb_save_end(false));
 puts("Twelve slots: 1..10 keep their files, 11 and 12 are .state13 / .state14, Continue untouched, PS1 .st0-.st11 PASS");
 return 0;
}
