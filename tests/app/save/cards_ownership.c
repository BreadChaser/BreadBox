/* Production PS1 cards: retained failures, headers and allocation refusal. */
#define main legacy_cards_main
#include "cards.c"
#undef main
#include "bb_final.h"
#include "bb_save.h"
bool breadbox_import_busy(void){return false;}
static bool allocation_fail;   /* open_fail and the fopen wrapper: cards.c */
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){return allocation_fail?NULL:__real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){return allocation_fail?NULL:__real_calloc(n,z);}
static void verify(const char *path,int seed,size_t prefix)
{
 FILE *f=fopen(path,"rb");assert(f);unsigned char expected[CARD],actual[CARD];pattern(expected,seed);
 for(size_t i=0;i<prefix;i++)assert(fgetc(f)==(int)(i%251));
 assert(fread(actual,1,CARD,f)==CARD && !memcmp(actual,expected,CARD) && fgetc(f)==EOF);fclose(f);
}
int main(void)
{
 char dir[1024];snprintf(dir,sizeof(dir),"%s/cards-final-XXXXXX",getenv("TMPDIR")?getenv("TMPDIR"):"/tmp");assert(mkdtemp(dir));assert(!chdir(dir));
 for(int kind=0;kind<3;kind++)for(int cut=1;cut<=4;cut++){
  char sub[64];snprintf(sub,sizeof(sub),"kind%d-fault%d",kind,cut);assert(!mkdir(sub,0700) && !chdir(sub));history_set_root("hist");
  size_t prefix=kind==1?64:kind==2?3904:0;
  blank(ram);blank(Mcd1Data);blank(Mcd2Data);
  if(prefix){
   unsigned char *image=malloc(CARD+prefix);assert(image);for(size_t i=0;i<prefix;i++)image[i]=i%251;blank(image+prefix);
   put("card1.mcd",image,CARD+prefix);put("card2.mcd",image,CARD+prefix);free(image);
  }
  cards_begin("game.srm");char one[]="card1.mcd",two[]="card2.mcd";__wrap_LoadMcd(1,one);__wrap_LoadMcd(2,two);
  char label[600];assert(cards_trouble(label,sizeof(label))==CARD_TROUBLE_NONE);
  pattern(ram,3);pattern(Mcd1Data,5);pattern(Mcd2Data,7);
  open_fail=cut==1;write_fail=cut==2;close_fail=cut==3;rename_fail_all=cut==4;
  assert(cards_finalize()==CARD_WRITE_FAILED);cards_end();
  memset(ram,0,CARD);memset(Mcd1Data,0,CARD);memset(Mcd2Data,0,CARD);
  assert(bb_final_failed() && !bb_final_retry());
  bb_save_config blocked={0};char error[256];assert(!bb_save_begin(&blocked,error,sizeof(error)) && strstr(error,"not on the SD card"));open_fail=false;write_fail=close_fail=rename_fail_all=0;
  assert(bb_final_retry());verify("game.srm",3,0);verify("card1.mcd",5,prefix);verify("card2.mcd",7,prefix);
  assert(!chdir(".."));
 }
 /* Restoring a header-bearing card pins the latest live card, not its load image. */
 assert(!mkdir("header-restore",0700) && !chdir("header-restore"));history_set_root("hist");
 unsigned char *image=malloc(CARD+64);assert(image);for(unsigned i=0;i<64;i++)image[i]=i%251;
 pattern(image+64,1);put("card2.mcd",image,CARD+64);pattern(image+64,2);put("card2-restore.mcd",image,CARD+64);free(image);
 char header_path[]="card2.mcd";__wrap_LoadMcd(2,header_path);pattern(Mcd2Data,3);
 hist_entry_t entry={0};strcpy(entry.ext,".mcd");strcpy(entry.path,"card2-restore.mcd");assert(cards_put_back(&entry));
 verify("card2.mcd",2,64);hist_entry_t pinned[8];int count=history_list("card2",true,pinned,8);bool found=false;
 for(int i=0;i<count;i++)if(pinned[i].kind==HIST_CARD_PIN){verify(pinned[i].path,3,64);found=true;}
 assert(found);cards_end();assert(!chdir(".."));
 /* No recovery buffer: refuse writes before gameplay rather than lose a final capture. */
 allocation_fail=true;cards_begin("oom.srm");char path[]="oom2.mcd";__wrap_LoadMcd(2,path);
 char label[600];assert(cards_trouble(label,sizeof(label))==CARD_TROUBLE_MEMORY);
 cards_accept();pattern(ram,9);pattern(Mcd2Data,11);assert(cards_save_now()==CARD_WRITE_CLEAN);
 assert(access("oom.srm",F_OK) && access("oom2.mcd",F_OK));allocation_fail=false;cards_end();
 /* Try again after the card file changed its header (VGS -> DexDrive) with no
  * memory for the bigger image: nothing is written past the old one, the card stays locked; with
  * memory again it loads (ASan). */
 assert(!mkdir("header-retry",0700) && !chdir("header-retry"));history_set_root("hist");
 put_card("card2.mcd",64,31);cards_begin("game.srm");char retry_path[]="card2.mcd";
 sd_err.name="card2.mcd";sd_err.error=EIO;sd_err.on=ON_OPEN;__wrap_LoadMcd(2,retry_path);sd_err.name=NULL;
 assert(cards_trouble(label,sizeof(label))==CARD_TROUBLE_MEMORY);
 put_card("card2.mcd",3904,33);
 allocation_fail=true;assert(!cards_retry());allocation_fail=false;
 assert(cards_trouble(label,sizeof(label))!=CARD_TROUBLE_NONE);
 unsigned char want[CARD];pattern(want,33);
 assert(cards_retry() && !memcmp(Mcd2Data,want,CARD));verify("card2.mcd",33,3904);
 cards_end();assert(!chdir(".."));
 puts("PS1 all card modes: open/write/close/rename failures, core destruction/retry, exact header bytes, allocation refusal, header change on retry without memory PASS");return 0;
}
