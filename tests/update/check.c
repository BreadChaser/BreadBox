#include "bb_update.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
static const char valid[]="{\"title_id\":\"000400000B0B0B00\",\"title_version\":\"16401\",\"version\":\"1.0.1\",\"size\":\"9000\",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"url\":\"https://github.com/BreadChaser/BreadBox/releases/download/v1.0.1/BreadBox.cia\"}";
static unsigned calls,fail;
static bool phase(void *ctx){(void)ctx;calls++;return calls!=fail;}
static void replace(char *out,const char *needle,const char *replacement)
{
 const char *p=strstr(valid,needle);assert(p);size_t n=p-valid;
 memcpy(out,valid,n);strcpy(out+n,replacement);strcat(out,p+strlen(needle));
}
int main(int argc,char **argv)
{
 bb_update_manifest m;assert(bb_update_manifest_parse(valid,strlen(valid),&m));assert(m.version==16401 && m.size==9000);
 assert(bb_update_newer(BB_UPDATE_TITLE,16401,16400));
 assert(!bb_update_newer(BB_UPDATE_TITLE,16400,16400));assert(!bb_update_newer(BB_UPDATE_TITLE,16399,16400));assert(!bb_update_newer(BB_UPDATE_TITLE,65536,16400));assert(!bb_update_newer(BB_UPDATE_TITLE+1,16401,16400));
 for(size_t n=0;n<strlen(valid);n++)assert(!bb_update_manifest_parse(valid,n,&m));
 char bad[4096];
 const char *pairs[][2]={{"16401","65536"},{"16401","016401"},{"16401","-1"},{"9000","67108865"},{"9000","0"},{"9000","1e6"},{"000400000B0B0B00","000400000C3D3D00"},{"1.0.1","1\\u002e0"},{"https://github.com/BreadChaser/BreadBox/","http://github.com/BreadChaser/BreadBox/"},{"BreadBox.cia","Other.cia"},{"title_id","other"},{"size","title_version"},{"1.0.1","bad version"},{"16401","42949672960"},{"BreadChaser/BreadBox/","BreadChaser/Other/"},{"github.com/","github.com.evil/"},{"BreadBox.cia","BreadBox.cia?download=1"},{"BreadBox.cia","BreadBox.cia#fragment"},{"0000000000000000000000000000000000000000000000000000000000000000","FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"}};
 for(unsigned i=0;i<sizeof(pairs)/sizeof(*pairs);i++){replace(bad,pairs[i][0],pairs[i][1]);assert(!bb_update_manifest_parse(bad,strlen(bad),&m));}
 const char *tags[]={"../../../../Evil/Repo/raw/main","v1/../../../Evil","%2e%2e/%2e%2e/x",".","..","%2e","v1%2f..","v1%2Fother","v1%5cother","","v1/extra","v1?x","v1#x"};
 for(unsigned i=0;i<sizeof(tags)/sizeof(*tags);i++){
  replace(bad,"v1.0.1",tags[i]);assert(!bb_update_manifest_parse(bad,strlen(bad),&m));
  char asset[1024],out[512];snprintf(asset,sizeof(asset),"{\"browser_download_url\":\"https://github.com/BreadChaser/BreadBox/releases/download/%s/update.json\"}",tags[i]);
  assert(!bb_update_release_asset(asset,strlen(asset),out,sizeof(out)));
 }
 replace(bad,"v1.0.1","v1.2_3+rc-4");assert(bb_update_manifest_parse(bad,strlen(bad),&m));
 memset(bad,' ',sizeof(bad));assert(!bb_update_manifest_parse(bad,sizeof(bad),&m));
 memcpy(bad,valid,sizeof(valid));bad[10]=0;assert(!bb_update_manifest_parse(bad,strlen(valid),&m));
 snprintf(bad,sizeof(bad),"%s garbage",valid);assert(!bb_update_manifest_parse(bad,strlen(bad),&m));
 const char release[]="{\"tag_name\":\"v1.0.1\",\"assets\":[{\"name\":\"BreadBox.cia\",\"size\":9000},{\"name\":\"update.json\",\"browser_download_url\":\"https://github.com/BreadChaser/BreadBox/releases/download/v1.0.1/update.json\"}]}";
 char url[512];assert(bb_update_release_asset(release,strlen(release),url,sizeof(url)));assert(strstr(url,"/update.json"));
 for(size_t n=0;n<strlen(release);n++)assert(!bb_update_release_asset(release,n,url,sizeof(url)));
 assert(!bb_update_release_asset("{}",2,url,sizeof(url)));
 bb_update_ops ops={phase,phase,phase,phase,NULL};
 for(fail=0;fail<=4;fail++){
  calls=0;bb_update_machine machine={BB_UPDATE_DOWNLOAD};
  for(unsigned step=0;step<4;step++){
   bb_update_state before=machine.state;unsigned c=calls;
   bb_update_step(&machine,&ops,false);assert(machine.state==before && calls==c);
   bb_update_step(&machine,&ops,true);
  }
  assert(machine.state==(fail?BB_UPDATE_FAILED:BB_UPDATE_RELAUNCH));assert(calls==(fail?fail:4));
  bb_update_step(&machine,&ops,true);assert(calls==(fail?fail:4));
 }
 assert(bb_update_known_answer());
 assert(argc==3);FILE *f=fopen(argv[1],"wb");assert(f);for(unsigned i=0;i<9000;i++)fputc('Z',f);fclose(f);
 assert(bb_update_manifest_parse(valid,strlen(valid),&m));
 unsigned char bytes[9000],hash[32];memset(bytes,'Z',sizeof(bytes));unsigned len;assert(EVP_Digest(bytes,sizeof(bytes),hash,&len,EVP_sha256(),NULL));
 for(unsigned i=0;i<32;i++)sprintf(m.sha256+i*2,"%02x",hash[i]);
 assert(bb_update_hash(argv[1],&m));
 f=fopen(argv[1],"r+b");assert(f);fputc('Y',f);fclose(f);assert(bb_update_hash_check(argv[1],&m)==BB_UPDATE_HASH_MISMATCH);
 f=fopen(argv[1],"r+b");assert(f);fputc('Z',f);fclose(f);
 m.size--;assert(!bb_update_hash(argv[1],&m));m.size+=2;assert(!bb_update_hash(argv[1],&m));m.size--;m.sha256[0]^=1;assert(!bb_update_hash(argv[1],&m));assert(!bb_update_hash(argv[2],&m));assert(bb_update_hash_check(argv[2],&m)==BB_UPDATE_HASH_IO);
 puts("PASS: manifest/JSON bounds, title version, SHA-256/parser self-test, SHA/size, all pipeline failures, never-in-game");
}
