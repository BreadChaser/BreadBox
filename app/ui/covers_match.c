#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "bb_system.h"
#include "fs_path.h"
#include "covers_match.h"

// ---------------------------------------------------------------- names

// libretro-thumbnails replaces these characters with '_' in file names
void covers_thumb_name(const char *key, char *out, size_t len)
{
	size_t n = 0;
	for (const char *p = key; *p && n < len - 1; p++)
		out[n++] = strchr("&*/:`<>?\\|\"", *p) ? '_' : *p;
	out[n] = 0;
}

void covers_url_escape(const char *in, char *out, size_t len)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && n < len - 4; p++) {
		if (isalnum(*p) || strchr("-_.~", *p)) {
			out[n++] = *p;
		} else {
			out[n++] = '%';
			out[n++] = hex[*p >> 4];
			out[n++] = hex[*p & 15];
		}
	}
	out[n] = 0;
}

void covers_cache_path(const char *key, const char *ext, char *out, size_t len)
{
	char name[256];
 const char *slash=strchr(key,'/');
 char id[16];if(slash && (size_t)(slash-key)<sizeof(id)) {
  snprintf(id,sizeof(id),"%.*s",(int)(slash-key),key);
  if(!bb_system_by_id(id))return;
  covers_thumb_name(slash+1,name,sizeof(name));
  char folder[512],file[280];
  if(snprintf(file,sizeof(file),"%s%s",name,ext)>=(int)sizeof(file) || !fs_path_join(folder,sizeof(folder),COVER_DIR,id) || !fs_path_join(out,len,folder,file))out[0]=0;
  return;
 }
	covers_thumb_name(key, name, sizeof(name));
	char file[280];
 if(snprintf(file,sizeof(file),"%s%s",name,ext)>=(int)sizeof(file) || !fs_path_join(out,len,COVER_DIR,file))out[0]=0;
}
