#include "bb_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef BB_UPDATE_HOST
#include <openssl/evp.h>
#else
#include <mbedtls/sha256.h>
#endif

/* Bounded JSON tokenizer. Exact byte extents, validated nesting and escapes;
 * the manifest deliberately accepts only plain ASCII update_json_string fields. */
typedef struct {const char *p;size_t n,pos;unsigned depth;} json;
static void update_json_ws(json *j){while(j->pos<j->n && strchr(" \r\n\t",j->p[j->pos]))j->pos++;}
static bool update_json_string(json *j,size_t *start,size_t *len,bool plain)
{
 update_json_ws(j);if(j->pos>=j->n || j->p[j->pos++]!='"')return false;
 *start=j->pos;
 while(j->pos<j->n){unsigned char c=j->p[j->pos++];
  if(c=='"'){*len=j->pos-*start-1;return true;}
  if(c<32 || (plain && (c=='\\' || c>126)))return false;
  if(c=='\\'){
   if(j->pos>=j->n)return false;
   c=j->p[j->pos++];
   if(c=='u'){for(int i=0;i<4;i++)if(j->pos>=j->n || !isxdigit((unsigned char)j->p[j->pos++]))return false;}
   else if(!strchr("\"\\/bfnrt",c))return false;
  }
 }
 return false;
}
static bool update_json_value(json *j);
static bool update_json_collection(json *j,char close,bool object)
{
 if(++j->depth>32)return false;
 update_json_ws(j);if(j->pos<j->n && j->p[j->pos]==close){j->pos++;j->depth--;return true;}
 for(;;){size_t s,n;
  if(object){if(!update_json_string(j,&s,&n,false))return false;update_json_ws(j);if(j->pos>=j->n || j->p[j->pos++]!=':')return false;}
  if(!update_json_value(j))return false;
  update_json_ws(j);if(j->pos>=j->n)return false;
  char c=j->p[j->pos++];if(c==close){j->depth--;return true;}if(c!=',')return false;
 }
}
static bool update_json_value(json *j)
{
 update_json_ws(j);if(j->pos>=j->n)return false;char c=j->p[j->pos];size_t s,n;
 if(c=='"')return update_json_string(j,&s,&n,false);
 if(c=='{' || c=='['){j->pos++;return update_json_collection(j,c=='{'?'}':']',c=='{');}
 for(unsigned k=0;k<3;k++){const char *v=(const char *[]){"true","false","null"}[k];n=strlen(v);if(j->n-j->pos>=n && !memcmp(j->p+j->pos,v,n)){j->pos+=n;return true;}}
 if(c=='-')j->pos++;
 if(j->pos>=j->n || !isdigit((unsigned char)j->p[j->pos]))return false;
 if(j->p[j->pos]=='0')j->pos++;else while(j->pos<j->n && isdigit((unsigned char)j->p[j->pos]))j->pos++;
 if(j->pos<j->n && j->p[j->pos]=='.'){j->pos++;s=j->pos;while(j->pos<j->n && isdigit((unsigned char)j->p[j->pos]))j->pos++;if(s==j->pos)return false;}
 if(j->pos<j->n && strchr("eE",j->p[j->pos])){j->pos++;if(j->pos<j->n && strchr("+-",j->p[j->pos]))j->pos++;s=j->pos;while(j->pos<j->n && isdigit((unsigned char)j->p[j->pos]))j->pos++;if(s==j->pos)return false;}
 return true;
}
static bool update_json_number(const char *s,unsigned max,unsigned *out)
{
 unsigned v=0;if(!*s || (s[0]=='0' && s[1]))return false;
 for(;*s;s++){if(*s<'0'||*s>'9'||v>(max-(unsigned)(*s-'0'))/10)return false;v=v*10+*s-'0';}
 *out=v;return true;
}
bool bb_update_newer(uint64_t title,unsigned candidate,unsigned installed)
{return title==BB_UPDATE_TITLE && candidate<=65535 && candidate>installed;}
/* One literal tag segment and one fixed asset name; never let curl normalize
 * a manifest URL into another repository or decode an escaped separator. */
static bool update_json_asset_url(const char *url,size_t len,const char *file)
{
 const char *prefix="https://github.com/BreadChaser/BreadBox/releases/download/";
 size_t base=strlen(prefix),tail=strlen(file);
 if(len<=base+tail+1 || memcmp(url,prefix,base) || url[len-tail-1]!='/' || memcmp(url+len-tail,file,tail))return false;
 size_t tag=len-base-tail-1;
 if((tag==1 && url[base]=='.') || (tag==2 && url[base]=='.' && url[base+1]=='.'))return false;
 for(size_t i=base;i<base+tag;i++){
  unsigned char c=url[i];if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || strchr("._+-",c)))return false;
 }
 return true;
}
bool bb_update_manifest_parse(const char *data,size_t len,bb_update_manifest *out)
{
 if(!data || !out || !len || len>2048 || memchr(data,0,len))return false;
 json j={data,len,0,0};update_json_ws(&j);if(j.pos==len || data[j.pos++]!='{')return false;
 const char *names[]={"title_id","title_version","version","size","sha256","url"};
 char fields[6][512]={{0}};unsigned seen=0;
 for(;;){size_t s,n;if(!update_json_string(&j,&s,&n,true))return false;int field=-1;
  for(int k=0;k<6;k++)if(n==strlen(names[k]) && !memcmp(data+s,names[k],n))field=k;
  if(field<0 || (seen&(1u<<field)))return false;
  seen|=1u<<field;update_json_ws(&j);if(j.pos>=len || data[j.pos++]!=':' || !update_json_string(&j,&s,&n,true) || n>=512)return false;
  memcpy(fields[field],data+s,n);update_json_ws(&j);if(j.pos>=len)return false;
  char c=data[j.pos++];if(c=='}')break;if(c!=',')return false;
 }
 update_json_ws(&j);if(j.pos!=len || seen!=63 || strcmp(fields[0],"000400000B0B0B00"))return false;
 unsigned ver,size;if(!update_json_number(fields[1],65535,&ver) || !update_json_number(fields[3],BB_UPDATE_MAX,&size) || size<0x2020)return false;
 if(!fields[2][0] || strlen(fields[2])>=sizeof(out->display))return false;
 for(const char *p=fields[2];*p;p++)if(!isalnum((unsigned char)*p) && !strchr(".-+",*p))return false;
 if(strlen(fields[4])!=64)return false;
 for(int i=0;i<64;i++)if(!strchr("0123456789abcdef",fields[4][i]))return false;
 bool url=update_json_asset_url(fields[5],strlen(fields[5]),"BreadBox.cia");
#ifdef BB_UPDATE_DEV_URL
 url=url || !strncmp(fields[5],BB_UPDATE_DEV_URL,strlen(BB_UPDATE_DEV_URL));
#endif
 size_t n=strlen(fields[5]);if(!url || n<13 || strcmp(fields[5]+n-13,"/BreadBox.cia") || strchr(fields[5],'?') || strchr(fields[5],'#') || strchr(fields[5],' '))return false;
 *out=(bb_update_manifest){.version=ver,.size=size};
 strcpy(out->display,fields[2]);strcpy(out->sha256,fields[4]);strcpy(out->url,fields[5]);return true;
}
bool bb_update_release_asset(const char *data,size_t len,char *out,size_t cap)
{
 if(!data || !len || len>98304 || memchr(data,0,len))return false;
 json j={data,len,0,0};if(!update_json_value(&j))return false;update_json_ws(&j);if(j.pos!=len)return false;
 /* After full JSON validation, visit every update_json_string token. A candidate must be
  * the update_json_value of browser_download_url and have the fixed repository prefix. */
 j.pos=0;bool found=false;
 while(j.pos<len){if(data[j.pos]!='"'){j.pos++;continue;}size_t s,n;
  if(!update_json_string(&j,&s,&n,false))return false;
  if(n!=20 || memcmp(data+s,"browser_download_url",20))continue;
  update_json_ws(&j);if(j.pos>=len || data[j.pos++]!=':' || !update_json_string(&j,&s,&n,true))return false;
  if(n<cap && update_json_asset_url(data+s,n,"update.json")){
   if(found)return false;
   memcpy(out,data+s,n);out[n]=0;found=true;
  }
 }
 return found;
}
static bool update_json_digest(const void *data,size_t n,unsigned char out[32])
{
#ifdef BB_UPDATE_HOST
 unsigned size=0;return EVP_Digest(data,n,out,&size,EVP_sha256(),NULL)==1 && size==32;
#else
 return !mbedtls_sha256_ret(data,n,out,0);
#endif
}
bb_update_hash_result bb_update_hash_check_cancel(const char *path,const bb_update_manifest *m,bool (*cancel)(void *),void *arg)
{
 FILE *f=fopen(path,"rb");if(!f)return BB_UPDATE_HASH_IO;
 unsigned char buf[16384],hash[32]={0};size_t total=0,n;bool ok=true;
#ifdef BB_UPDATE_HOST
 EVP_MD_CTX *ctx=EVP_MD_CTX_new();if(!ctx){fclose(f);return BB_UPDATE_HASH_IO;}ok=EVP_DigestInit_ex(ctx,EVP_sha256(),NULL)==1;
#else
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);ok=!mbedtls_sha256_starts_ret(&ctx,0);
#endif
 bool aborted=false;
 while(ok){if(cancel && cancel(arg)){aborted=true;break;}
  n=fread(buf,1,sizeof(buf),f);if(!n)break;total+=n;if(total>m->size){ok=false;break;}
#ifdef BB_UPDATE_HOST
  ok=EVP_DigestUpdate(ctx,buf,n)==1;
#else
  ok=!mbedtls_sha256_update_ret(&ctx,buf,n);
#endif
 }
 bool io=ferror(f)!=0;
 if(fclose(f))io=true;
 bool length_ok=total==m->size;
#ifdef BB_UPDATE_HOST
 unsigned count;ok=(EVP_DigestFinal_ex(ctx,hash,&count)==1) && ok;EVP_MD_CTX_free(ctx);
#else
 ok=!mbedtls_sha256_finish_ret(&ctx,hash) && ok;mbedtls_sha256_free(&ctx);
#endif
 char hex[65];for(int i=0;i<32;i++)sprintf(hex+i*2,"%02x",hash[i]);
 if(aborted || io || (!ok && total<=m->size))return BB_UPDATE_HASH_IO;
 return ok && length_ok && !strcmp(hex,m->sha256)?BB_UPDATE_HASH_OK:BB_UPDATE_HASH_MISMATCH;
}
bb_update_hash_result bb_update_hash_check(const char *path,const bb_update_manifest *m)
{return bb_update_hash_check_cancel(path,m,NULL,NULL);}
bool bb_update_hash(const char *path,const bb_update_manifest *m)
{return bb_update_hash_check(path,m)==BB_UPDATE_HASH_OK;}
void bb_update_step(bb_update_machine *m,const bb_update_ops *ops,bool idle)
{
 if(!idle)return;
 bool ok=true;
 switch(m->state){
 case BB_UPDATE_DOWNLOAD:ok=ops->download(ops->ctx);if(ok)m->state=BB_UPDATE_VERIFY;break;
 case BB_UPDATE_VERIFY:ok=ops->verify(ops->ctx);if(ok)m->state=BB_UPDATE_READY;break;
 case BB_UPDATE_READY:ok=ops->flush(ops->ctx);if(ok)m->state=BB_UPDATE_INSTALL;break;
 case BB_UPDATE_INSTALL:ok=ops->install(ops->ctx);if(ok)m->state=BB_UPDATE_RELAUNCH;break;
 default:break;
 }
 if(!ok)m->state=BB_UPDATE_FAILED;
}

#include "update_vector.h"
bool bb_update_known_answer(void)
{
 unsigned char hash[32];bb_update_manifest manifest;
 if(!update_json_digest(update_vector_data,sizeof(update_vector_data)-1,hash) ||
    memcmp(hash,update_vector_sha256,sizeof(hash)) ||
    !bb_update_manifest_parse(update_vector_data,sizeof(update_vector_data)-1,&manifest))return false;
 char bad[sizeof(update_vector_data)];memcpy(bad,update_vector_data,sizeof(bad));bad[0]='[';
 return !bb_update_manifest_parse(bad,sizeof(bad)-1,&manifest);
}
