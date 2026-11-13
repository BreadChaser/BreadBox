/* Finalization regressions execute the production service and real file writer. */
#define main service_legacy_main
#include "check.c"
#undef main
int main(int argc,char **argv)
{
 assert(argc==3);assert(!chdir(argv[1]));history_set_root("history");
 begin();memset(ram,0x31,128);position=1;assert(bb_save_flush());
 if(!strcmp(argv[2],"sync")){
  memset(ram,0x42,128);rtc[0]=4242;block_tmp(true);
  assert(!bb_save_end(false));assert(bb_save_final_failed());
  memset(ram,0,128);rtc[0]=0;char e[256];assert(!bb_save_begin(&config,e,sizeof(e)));
  assert(bb_save_end(false));block_tmp(false);assert(bb_save_final_retry());disk(0x42);
  begin();assert(rtc[0]==4242);assert(bb_save_end(false));
 }else if(!strcmp(argv[2],"uncaptured")){
  lose_ram=true;assert(!bb_save_end(false));assert(bb_save_final_failed());
  assert(bb_save_final_uncaptured());bb_save_final_acknowledge();assert(!bb_save_final_failed());
  lose_ram=false;begin();disk(0x31);assert(bb_save_end(false));
 }else if(!strcmp(argv[2],"end-continue")){
  assert(bb_save_state(10,true));position=2;memset(ram,0x42,128);block_tmp(true);
  (void)bb_save_end(true);bb_save_settle();assert(bb_save_final_failed());
  FILE *f=fopen("game.state10","rb");assert(f && !fseek(f,28,SEEK_SET) && fgetc(f)==1);fclose(f);
  /* The failed battery still belongs to the ended session, independent of its core. */
  memset(ram,0,128);position=0;block_tmp(false);assert(bb_save_final_retry());disk(0x42);
  begin();position=3;memset(ram,0x53,128);assert(bb_save_end(true));disk(0x53);
  f=fopen("game.state10","rb");assert(f && !fseek(f,28,SEEK_SET) && fgetc(f)==3);fclose(f);
 }else if(!strcmp(argv[2],"partial")){
  unsigned slots[]={0,BB_SLOT_CONTINUE,BB_SLOT_QUICK,BB_SLOT_UNDO};
  for(unsigned i=0;i<sizeof(slots)/sizeof(*slots);i++){
   if(i)begin();position=1;assert(bb_save_state(slots[i],true));position=2;partial=true;relocate=i%2;
   assert(!bb_save_state(slots[i],false));assert(!bb_save_runnable() && !bb_save_queue());
   assert(!bb_save_state(1,true));assert(!bb_save_state(slots[i],false));
   assert(!bb_save_end(true));disk(0x31);partial=false;
  }
 }else{
  assert(bb_save_state(10,true));position=2;
  assert(!mkdir("game.state10.tmp",0700));FILE *f=fopen("game.state10.tmp/x","w");assert(f);fclose(f);
  assert(bb_save_state_end(10,true));assert(bb_save_end(false));bb_save_settle();
  assert(bb_save_final_failed());memset(ram,0,128);position=0;
  char e[256];assert(!bb_save_begin(&config,e,sizeof(e)));
  assert(!unlink("game.state10.tmp/x") && !rmdir("game.state10.tmp"));
  assert(bb_save_final_retry());
  /* A newer payload CRC must not classify the previous valid point as damaged. */
  FILE *old=fopen("game.state10.bak","rb");assert(old && !fseek(old,28,SEEK_SET) && fgetc(old)==1);fclose(old);
  assert(access("game.state10.bad",F_OK));
  begin();assert(bb_save_state(10,false) && position==2);assert(bb_save_end(false));
 }
 puts("final ownership regression PASS");return 0;
}
