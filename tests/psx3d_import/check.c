/* Real loader validation + real history ring, synthetic states with the real
 * core headers. Fault each durable publication boundary, then retry twice. */
#define _GNU_SOURCE
#include "../../systems/ps1/statefile.c"
#include "breadbox_import.h"
#include "history.h"
#include "fs_path.h"
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
struct PcsxSaveFuncs SaveFuncs;
PcsxConfig Config;
void log_printf(const char *fmt,...) {if(getenv("IMPORT_TRACE")){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);}}
bool __real_retro_serialize(void *p,size_t n) {(void)p;(void)n;return false;}
bool __real_retro_unserialize(const void *p,size_t n) {(void)p;(void)n;return false;}
static long write_budget=-1;
static bool fail_state_writes;
static unsigned state_checks;
#ifdef IMPORT_SEQUENCE_CHECK
static void sequence_write(FILE *);
#endif
state_result_t __real_state_check(const void *,size_t);
state_result_t __wrap_state_check(const void *p,size_t n){state_checks++;return __real_state_check(p,n);}
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
size_t __wrap_fwrite(const void *p,size_t size,size_t n,FILE *f)
{
#ifdef IMPORT_SEQUENCE_CHECK
 if(size*n>=8192)sequence_write(f);
#endif
 if(fail_state_writes && size*n>=8192) {fail_state_writes=false;return 0;}
 if(write_budget<0)return __real_fwrite(p,size,n,f);
 size_t allowed=size?(size_t)write_budget/size:0;if(n>allowed){n=allowed;errno=ENOSPC;}
 size_t written=__real_fwrite(p,size,n,f);write_budget-=(long)(written*size);return written;
}
int __real_mkdir(const char *,mode_t);
int __wrap_mkdir(const char *p,mode_t m)
{if(!strcmp(p,"sdmc:")) {errno=EINVAL;return -1;}return __real_mkdir(p,m);}
static char src[512],dst[512];
static int open_descriptors(void)
{
 DIR *d=opendir("/proc/self/fd");assert(d);int n=0;while(readdir(d))n++;closedir(d);return n;
}
static void mk(const char *p) {assert(!mkdir(p,0777));}
static void write_file(const char *p,const void *data,size_t n)
{
 FILE *f=fopen(p,"wb");assert(f);assert(fwrite(data,1,n,f)==n);assert(!fclose(f));
}
static void at(char *p,const char *root,const char *rel) {assert(fs_path_join(p,1024,!strcmp(root,".")?"":root,rel));}
static void put(const char *root,const char *rel,const void *data,size_t n)
{char p[1024];at(p,root,rel);write_file(p,data,n);}
static void *state(unsigned saved)
{
 size_t n=0x440000;unsigned char *p=calloc(1,n);assert(p);
 memcpy(p,"STv4 PCSX",9);
 u32 spu=SPU_MIN+0x1000;memcpy(p+SPU_SIZE_AT,&spu,4);
 size_t at=SPU_SIZE_AT+4+spu+STATE_TAIL;
 for(int i=0;i<PADS;i++) {
  PadDataS pad={0};pad.saveSize=sizeof(pad);memcpy(p+at,&pad,sizeof(pad));at+=sizeof(pad);
 }
 state_trailer_t t={.version=1,.data_len=at,.tail=STATE_TAIL,.saved=saved,.flags=STATE_SEALED};
 memcpy(t.magic,"PSX3Dsv1",8);memcpy(p+n-sizeof(t),&t,sizeof(t));
 assert(state_seal(p,n));assert(state_check(p,n)==STATE_OK);return p;
}
static void thumbnail_file(const char *root,const char *name,unsigned saved)
{
 size_t n=12+256*192*2;uint32_t *p=calloc(1,n);assert(p);
 p[0]=0x48543350;p[1]=256|(192u<<16);p[2]=saved;p[3]=saved;
 put(root,name,p,n);free(p);
}
static void thumbnail(const char *root,unsigned saved)
{thumbnail_file(root,"states/Exact Game (USA).sta.thm",saved);}
static void card_file(const char *root,const char *name,unsigned variant)
{
 unsigned char *p=calloc(1,128*1024);assert(p);p[0]='M';p[1]='C';p[127]='M'^'C';p[4096]=variant;
 put(root,name,p,128*1024);free(p);
}
static void card(const char *root,unsigned variant)
{card_file(root,"saves/Exact Game (USA).srm",variant);}
static void setup(const char *root)
{
 char p[1024];mk(root);at(p,root,"states");mk(p);at(p,root,"saves");mk(p);at(p,root,"games");mk(p);
}
static unsigned crcfile(const char *root,const char *rel)
{
 char p[1024];at(p,root,rel);FILE *f=fopen(p,"rb");assert(f);unsigned crc=0;unsigned char buf[4096];size_t n;
 while((n=fread(buf,1,sizeof(buf),f)))crc=state_crc32(buf,n,crc);
 assert(!ferror(f));fclose(f);return crc;
}
static void verify(unsigned source_crc,unsigned card_crc)
{
 char p[1024];at(p,dst,"states/Exact Game (USA).sta");
 unsigned why,t,id;assert(state_file_info(p,&why,&t,&id) && t==200);
 assert(crcfile(src,"states/Exact Game (USA).sta")==source_crc);
 assert(crcfile(src,"saves/Exact Game (USA).srm")==card_crc);
 assert(crcfile(dst,"saves/Exact Game (USA).srm")!=card_crc); /* unknown age: BreadBox wins */
 assert(crcfile(dst,"states/Exact Game (USA).sta.thm")==crcfile(src,"states/Exact Game (USA).sta.thm"));
 hist_entry_t entries[20];assert(history_list("Exact Game (USA)",false,entries,20)==1);
 assert(state_file_info(entries[0].path,&why,&t,&id) && t==100);
 at(p,dst,"states/Exact Game (Europe).sta");assert(!access(p,F_OK));
 unsigned saved;assert(psx_import_continue_ready(src,dst,"Exact Game (USA)",&saved) && saved==200);
 assert(!psx_import_continue_ready(src,dst,"../escape",&saved));
 assert(crcfile(dst,"states/Temp.st1")==crcfile(src,"states/Temp.st1.tmp"));
 assert(crcfile(dst,"states/Temp.st1.thm")==crcfile(src,"states/Temp.st1.thm.tmp"));
 assert(crcfile(dst,"saves/pcsx-card2.mcd")==crcfile(src,"saves/pcsx-card2.mcd"));
}
static unsigned tree_crc(const char *root)
{
 DIR *d=opendir(root);assert(d);struct dirent *e;unsigned c=0;
 while((e=readdir(d)))if(e->d_name[0]!='.') {
  char p[1024];struct stat st;at(p,root,e->d_name);assert(!lstat(p,&st));
  if(S_ISDIR(st.st_mode))c^=tree_crc(p);
  else if(S_ISREG(st.st_mode))c^=crcfile(root,e->d_name)^state_crc32(e->d_name,strlen(e->d_name),0);
 }
 closedir(d);return c;
}
static bool pause_now(const char *s,void *ctx) {(void)s;(void)ctx;return false;}
static bool pause_history(const char *s,void *ctx) {return strcmp(s,(const char *)ctx)!=0;}
static bool stop_size(void *ctx){unsigned *left=ctx;if(!*left)return false;--*left;return true;}
int main(int argc,char **argv)
{
 assert(argc==2 || argc==3);assert(!chdir(argv[1]));
 setup("Estimate");
 put("Estimate","saves/check.srm","12",2);
 put("Estimate","saves/check.srm.bak","123",3);
 put("Estimate","saves/check.srm.tmp","1234",4);
 put("Estimate","states/damaged.sta","12345",5);
 put("Estimate","games/ignored.txt","ignored",7);
 assert(!symlink("check.srm","Estimate/saves/link.srm"));
 psx_import_estimate estimate=psx_import_size("Estimate");
 assert(estimate.files==4 && estimate.bytes==14 && !estimate.incomplete);
 unsigned left=3;
 estimate=psx_import_size_checked("Estimate",stop_size,&left);
 assert(estimate.incomplete && estimate.files<4);
 char size[40];
 psx_import_format_size(size,sizeof(size),64*1024);assert(!strcmp(size,"64.0 KiB"));
 psx_import_format_size(size,sizeof(size),1024*1024-1);assert(!strcmp(size,"1024.0 KiB"));
 psx_import_format_size(size,sizeof(size),1024*1024);assert(!strcmp(size,"1.0 MiB"));
 psx_import_format_size(size,sizeof(size),199*1024*1024/100);assert(!strcmp(size,"2.0 MiB"));
 psx_import_format_size(size,sizeof(size),1ULL<<30);assert(!strcmp(size,"1.0 GiB"));
 psx_import_format_size(size,sizeof(size),(1ULL<<30)-1);assert(!strcmp(size,"1024.0 MiB"));
 psx_import_format_size(size,sizeof(size),UINT64_MAX);assert(!strcmp(size,"17179869184.0 GiB"));
 psx_import_format_size(size,sizeof(size),0);assert(!strcmp(size,"0.0 KiB"));

 assert(!access("Estimate/saves/check.srm",F_OK));
 strcpy(src,"PSX3D");setup(src);
 void *new=state(200),*old=state(100);put(src,"states/Exact Game (USA).sta",new,0x440000);
 thumbnail(src,200);card(src,2);card_file(src,"saves/pcsx-card2.mcd",3);put(src,"games/Exact Game (USA).ini","stretch=1\n",10);
 /* Backup-only and corrupt current with a sound backup: loader order applies. */
 put(src,"states/Exact Game (Europe).sta.bak",new,0x440000);
 put(src,"states/Temp.st1.tmp",new,0x440000);thumbnail_file(src,"states/Temp.st1.thm.tmp",200);
 put(src,"states/Recovered.st0","broken",6);put(src,"states/Recovered.st0.bak",new,0x440000);
 thumbnail_file(src,"states/Recovered.st0.thm",300);thumbnail_file(src,"states/Recovered.st0.thm.bak",200);
 ((unsigned char *)new)[1000]^=1;put(src,"states/Corrupt.sta",new,0x440000);((unsigned char *)new)[1000]^=1;
 unsigned sc=crcfile(src,"states/Exact Game (USA).sta"),cc=crcfile(src,"saves/Exact Game (USA).srm");
 if(argc==3) { /* low global budget: another game's backup is retired */
  strcpy(dst,"BreadBox-budget");setup(dst);char h[1024];at(h,dst,"backups");history_set_root(h);
  history_fake_now=10;assert(history_add("Other game",HIST_SAVE,".sta",old,0x440000));
  hist_entry_t e[10];assert(history_list("Other game",false,e,10)==1);hist_entry_t original=e[0];
  void *fifty=state(50);put(dst,"states/Exact Game (USA).sta",fifty,0x440000);free(fifty);
  psx_import_result r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed);
  assert(access(original.path,F_OK)); /* budget actually trimmed the other game */
  const char *base=strrchr(original.path,'/');assert(base);
  char archived[1024];assert(snprintf(archived,sizeof(archived),"import/psx3d/breadbox-history/backups/Other game/%s/%u-%08x",base+1,0x440000u,state_crc32(old,0x440000,0))<(int)sizeof(archived));
  assert(crcfile(dst,archived)==state_crc32(old,0x440000,0));
 free(new);free(old);puts("Import preserves another game's backup before global-budget trimming: PASS");return 0;
 }
 assert(psx_import_available(src));unsigned source_tree=tree_crc(src);
 for(int stop=-1;stop<28;stop++) {
  snprintf(dst,sizeof(dst),"BreadBox-%d",stop+1);setup(dst);
  put(dst,"states/Exact Game (USA).sta",old,0x440000);thumbnail(dst,100);card(dst,1);
  put(dst,"games/Exact Game (USA).ini","stretch=0\n",10);
  psx_import_stop_after=stop;psx_import_run(src,dst,NULL,NULL);
  psx_import_stop_after=-1;
  assert(psx_import_recover(dst));
  char restored[1024];at(restored,dst,"states/Exact Game (USA).sta");assert(!access(restored,F_OK));
  psx_import_result r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && r.rejected==1);
  verify(sc,cc);assert(tree_crc(src)==source_tree);
  r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && !r.copied && r.rejected==1);
  verify(sc,cc);
  char p[1024];at(p,dst,"states/Corrupt.sta");assert(access(p,F_OK));
 }
 /* A later launch finds newly created and changed PSX3D data by content,
  * without FAT timestamps, while receipts preserve once-only publication. */
 strcpy(dst,"BreadBox-later-launch");setup(dst);
 psx_import_result launch=psx_import_run(src,dst,NULL,NULL);assert(!launch.failed && launch.copied);
 launch=psx_import_run(src,dst,NULL,NULL);assert(!launch.failed && !launch.copied);
 put(src,"games/New game.ini","screen=0\n",9);
 launch=psx_import_run(src,dst,NULL,NULL);assert(!launch.failed && launch.copied==1);
 launch=psx_import_run(src,dst,NULL,NULL);assert(!launch.failed && !launch.copied);
 char newgame[1024];at(newgame,src,"games/New game.ini");assert(!unlink(newgame));
 assert(tree_crc(src)==source_tree);
 /* Short writes / full card while staging: no existing file is changed. */
 for(int k=0;k<3;k++) {
  snprintf(dst,sizeof(dst),"Short-write-%d",k);setup(dst);
  put(dst,"states/Exact Game (USA).sta",old,0x440000);thumbnail(dst,100);card(dst,1);
  write_budget=k==0?0:k==1?123:4*1024*1024;
  psx_import_result bad=psx_import_run(src,dst,NULL,NULL);assert(bad.failed);
  write_budget=-1;psx_import_result good=psx_import_run(src,dst,NULL,NULL);assert(!good.failed);verify(sc,cc);
  assert(tree_crc(src)==source_tree);
 }
 /* Newer BreadBox wins; the imported state remains available in its ring. */
 strcpy(dst,"BreadBox-newer");setup(dst);void *newer=state(300);
 put(dst,"states/Exact Game (USA).sta",newer,0x440000);
 put(dst,"states/Recovered.st0",newer,0x440000);free(newer);
 psx_import_result nr=psx_import_run(src,dst,NULL,NULL);assert(!nr.failed);
 char checkpath[1024];at(checkpath,dst,"states/Exact Game (USA).sta");unsigned why,t,id;
 assert(state_file_info(checkpath,&why,&t,&id) && t==300);
 /* A backup loser must use its backup picture, never the corrupt primary's. */
 hist_entry_t recovered[10];assert(history_list("Recovered",false,recovered,10)==1);
 char ring_picture[1024];assert(snprintf(ring_picture,sizeof(ring_picture),"%s.thm",recovered[0].path)<(int)sizeof(ring_picture));
 assert(crcfile(".",ring_picture)==crcfile(src,"states/Recovered.st0.thm.bak"));
 /* A pre-existing backup with no primary is an existing BreadBox save. */
 strcpy(dst,"BreadBox-backup");setup(dst);
 put(dst,"states/Exact Game (USA).sta.bak",old,0x440000);
 nr=psx_import_run(src,dst,NULL,NULL);assert(!nr.failed && nr.conflicts);
 hist_entry_t he[10];assert(history_list("Exact Game (USA)",false,he,10)==1);
 assert(state_file_info(he[0].path,&why,&t,&id) && t==100);
 psx_import_result r=psx_import_run(src,dst,pause_now,NULL);assert(r.interrupted && !r.copied);
 r=psx_import_run(src,src,NULL,NULL);assert(r.failed);
 assert(!symlink("PSX3D","alias"));r=psx_import_run(src,"alias",NULL,NULL);assert(r.failed);
 /* A corrupt destination stays in place. A source symlink is never followed. */
 strcpy(dst,"BreadBox-corrupt");setup(dst);put(dst,"states/Exact Game (USA).sta","precious",8);
 assert(!symlink("Exact Game (USA).sta","PSX3D/states/Link.sta"));
 r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && r.rejected==2);
 assert(crcfile(dst,"states/Exact Game (USA).sta")==state_crc32("precious",8,0));
 /* A full ten-state ring must keep its retired oldest entry permanently. */
 strcpy(dst,"BreadBox-full-ring");setup(dst);char h[1024];at(h,dst,"backups");history_set_root(h);
 for(int j=0;j<10;j++) {history_fake_now=100+j;void *v=state(100+j);assert(history_add("Exact Game (USA)",HIST_SAVE,".sta",v,0x440000));free(v);}
 hist_entry_t full[10];assert(history_list("Exact Game (USA)",false,full,10)==10);hist_entry_t oldest=full[9];
 unsigned old_crc=crcfile(".",oldest.path);
 void *fifty=state(50);put(dst,"states/Exact Game (USA).sta",fifty,0x440000);free(fifty);
 r=psx_import_run(src,dst,pause_history,(void *)(strrchr(oldest.path,'/')+1));
 assert(r.interrupted && !access(oldest.path,F_OK));
 at(checkpath,dst,"states/Exact Game (USA).sta");
 assert(state_file_info(checkpath,&why,&t,&id) && t==50);
 r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed);
 assert(history_list("Exact Game (USA)",false,full,10)==10 && access(oldest.path,F_OK));
 const char *base=strrchr(oldest.path,'/');assert(base);char archived[1024];
 assert(snprintf(archived,sizeof(archived),"import/psx3d/breadbox-history/backups/Exact Game (USA)/%s/%u-%08x",base+1,0x440000u,old_crc)<(int)sizeof(archived));
 assert(crcfile(dst,archived)==old_crc);
 /* libctru rejects mkdir on a mounted device root. Import must skip it. */
 assert(!__real_mkdir("sdmc:",0777));assert(!__real_mkdir("sdmc:/3ds",0777));
 strcpy(dst,"sdmc:/3ds/BreadBox");setup(dst);
 r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && r.copied);
 /* Different save ownership: archive only, including an empty state slot. */
 strcpy(dst,"BreadBox-identity");setup(dst);
 put(src,"saves/Exact Game (USA).id","first=SCUS12345\n",16);
 put(dst,"saves/Exact Game (USA).id","first=SLUS54321\n",16);
 r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && r.conflicts>=3);
 at(checkpath,dst,"states/Exact Game (USA).sta");assert(access(checkpath,F_OK));
  /* Production cursor + durable signature: no copy pass on the third boot,
  * changed equal-size settings/cards detected, interruption resumes receipts. */
 setup("Cursor-source");setup("Cursor-dest");card("Cursor-source",7);
 put("Cursor-source","games/Test.ini","screen=0\n",9);
 psx_import_cursor *cursor=psx_import_open("Cursor-source","Cursor-dest",false);assert(cursor);
 unsigned chunks=0;psx_import_result cr={0};
 while(!psx_import_step(cursor,&cr)){assert(++chunks<1000);}
 assert(chunks>2 && cr.copied==2 && !cr.failed && psx_import_changed(cursor));
 assert(cr.kinds==(PSX_IMPORT_SAVES|PSX_IMPORT_SETTINGS));   /* the welcome names what came: a card and a game's settings */
 assert(psx_import_finish(cursor));psx_import_close(cursor);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(!cr.copied && !psx_import_changed(cursor));psx_import_close(cursor);
 put("Cursor-source","games/Test.ini","screen=1\n",9);card("Cursor-source",8);
 put("Cursor-source","games/Added.ini","screen=0\n",9);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr) && !cr.copied){} // kill after first durable file
 assert(cr.copied);psx_import_close(cursor);assert(psx_import_recover("Cursor-dest"));
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(psx_import_changed(cursor) && !cr.failed);
 assert(psx_import_finish(cursor));psx_import_close(cursor);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(!cr.copied && !psx_import_changed(cursor));psx_import_close(cursor);
 put("Cursor-source","games/New.ini","screen=0\n",9);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);write_budget=0;
 while(!psx_import_step(cursor,&cr)){}assert(cr.failed && cr.no_space && !psx_import_finish(cursor));
 write_budget=-1;psx_import_close(cursor);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(cr.copied==1 && !cr.failed && psx_import_finish(cursor));psx_import_close(cursor);
 /* The adapter prepares Continue only for a copied automatic state whose
  * region-qualified name and ROM still exist; it never adopts global config. */
 put("Cursor-source","states/Exact Game (USA).sta",new,0x440000);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(!cr.failed && psx_import_finish(cursor));psx_import_close(cursor);
 write_file("Game.bin","disc",4);
 const char *config="last_game=Exact Game\nlast_game_save=Exact Game (USA)\nlast_game_state=Exact Game (USA)\nlast_game_path=Game.bin\n";
 put("Cursor-source","config.ini",config,strlen(config));psx_import_continue resume;
 psx_import_read_continue("Cursor-source","Cursor-dest",&resume);
 assert(resume.saved==200 && !strcmp(resume.rom,"Game.bin") && !strcmp(resume.title,"Exact Game"));
 assert(!unlink("Game.bin"));psx_import_read_continue("Cursor-source","Cursor-dest",&resume);assert(!resume.saved);
 /* An empty per-game INI means defaults; preserve it just like nonempty
  * settings. Cards/states still require their nonempty structural headers. */
 put("Cursor-source","games/Empty.ini","",0);
 cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 while(!psx_import_step(cursor,&cr)){}assert(cr.copied==1 && !cr.failed && !cr.rejected && psx_import_finish(cursor));psx_import_close(cursor);
 assert(cr.kinds==PSX_IMPORT_SETTINGS);
 assert(!access("Cursor-dest/games/Empty.ini",F_OK));
 /* Cancel during an unsealed 4 MiB streamed transaction.
  * The cursor must release both its directory and partially consumed FILE. */
 void *legacy=state(300);memset((char *)legacy+0x440000-sizeof(state_trailer_t),0,sizeof(state_trailer_t));
 put("Cursor-source","states/Legacy.st0",legacy,0x440000);free(legacy);
 int before_fds=open_descriptors();cursor=psx_import_open("Cursor-source","Cursor-dest",false);
 bool copying=false;
 for(unsigned k=0;k<10000 && !copying;k++) {
  assert(!psx_import_step(cursor,&cr));assert(!cr.failed);struct stat staged;
  copying=!stat("Cursor-dest/states/Legacy.st0.psx3d-new.import-tmp",&staged) && staged.st_size>0 && staged.st_size<0x440000;
 }
 assert(copying && open_descriptors()>before_fds);psx_import_close(cursor);assert(open_descriptors()==before_fds);
 /* A state's sealed CRC is validated once, then its picture reuses that
  * selection. A fresh cursor must validate again, never reuse stale results. */
 setup("Cache-source");setup("Cache-dest");setup("Cache-other");
 put("Cache-source","states/Exact Game (USA).sta",new,0x440000);thumbnail_file("Cache-source","states/Exact Game (USA).sta.thm",200);
 for(int k=0;k<2;k++){
  unsigned checked=state_checks;cursor=psx_import_open("Cache-source",k?"Cache-other":"Cache-dest",false);
  while(!psx_import_step(cursor,&cr)){}assert(cr.copied==2 && !cr.failed && state_checks==checked+1);
  assert(cr.kinds==PSX_IMPORT_POSTCARDS);   /* a postcard and its picture */
  assert(psx_import_finish(cursor));psx_import_close(cursor);
 }
 /* Cheats (enabled list, own codes, own .cht) and BIOS files come over byte-exact,
  * copy-only, once; BreadBox's own file of the same name stays (PSX3D's is archived). */
 {
  setup("Extra-source");setup("Extra-dest");mk("Extra-source/cheats");mk("Extra-source/bios");mk("Extra-dest/cheats");
  size_t bios_n=512*1024;unsigned char *bios=malloc(bios_n);assert(bios);for(size_t i=0;i<bios_n;i++)bios[i]=(unsigned char)(i*7+3);
  put("Extra-source","bios/scph1001.bin",bios,bios_n);
  put("Extra-source","bios/SCPH5501",bios,4096);   /* No extension, still a BIOS file */
  put("Extra-source","cheats/Exact Game (USA).on","Infinite lives\n",15);
  put("Extra-source","cheats/Exact Game (USA).txt","cheat0_desc = \"Mine\"\n",21);
  put("Extra-source","cheats/Empty.on","",0);
  put("Extra-source","cheats/Other.cht","cheats = 0\n",11);
  put("Extra-source","cheats/notes.doc","x",1);   /* not a cheat file: left alone */
  put("Extra-dest","cheats/Other.cht","cheats = 1\n",11);   /* BreadBox's own wins */
  unsigned source_tree=tree_crc("Extra-source");
  cursor=psx_import_open("Extra-source","Extra-dest",false);assert(cursor);
  while(!psx_import_step(cursor,&cr)){}
  assert(!cr.failed && !cr.rejected && cr.copied==5 && cr.conflicts==1 && psx_import_finish(cursor));psx_import_close(cursor);
  assert(cr.kinds==(PSX_IMPORT_CHEATS|PSX_IMPORT_BIOS));
  assert(crcfile("Extra-dest","bios/scph1001.bin")==state_crc32(bios,bios_n,0));
  assert(crcfile("Extra-dest","bios/SCPH5501")==state_crc32(bios,4096,0));
  assert(crcfile("Extra-dest","cheats/Exact Game (USA).on")==crcfile("Extra-source","cheats/Exact Game (USA).on"));
  assert(crcfile("Extra-dest","cheats/Exact Game (USA).txt")==crcfile("Extra-source","cheats/Exact Game (USA).txt"));
  assert(!access("Extra-dest/cheats/Empty.on",F_OK) && access("Extra-dest/cheats/notes.doc",F_OK));
  assert(crcfile("Extra-dest","cheats/Other.cht")==state_crc32("cheats = 1\n",11,0));
  assert(tree_crc("Extra-source")==source_tree);
  cursor=psx_import_open("Extra-source","Extra-dest",false);
  while(!psx_import_step(cursor,&cr)){}assert(!cr.copied && !psx_import_changed(cursor));psx_import_close(cursor);
  /* the synchronous path copies the same set */
  setup("Extra-run");psx_import_result rr=psx_import_run("Extra-source","Extra-run",NULL,NULL);
  assert(!rr.failed && rr.copied==6 && rr.kinds==(PSX_IMPORT_CHEATS|PSX_IMPORT_BIOS) && crcfile("Extra-run","bios/scph1001.bin")==state_crc32(bios,bios_n,0));
  assert(crcfile("Extra-run","bios/SCPH5501")==state_crc32(bios,4096,0));
  assert(tree_crc("Extra-source")==source_tree);free(bios);
 }
 free(new);free(old);puts("PSX3D import: fresh, partial, conflicts, corrupt, backup recovery, 28 interruption boundaries, reruns, aliases, cheats + BIOS: PASS");
 return 0;
}
