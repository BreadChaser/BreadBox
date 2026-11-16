/* Filesystem joins shared by import/save/cache callers. Empty optional parts
 * add no separator; a device or POSIX root keeps its one root slash. URLs
 * and filename suffixes are not filesystem joins. */
#ifndef BB_FS_PATH_H
#define BB_FS_PATH_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
static __attribute__((noinline,unused,section(".psx3d_cold.text"))) bool fs_path_join(char *out,size_t cap,const char *a,const char *b)
{
 size_t n=strlen(a);
 while(n && a[n-1]=='/' && n>1 && a[n-2]!=':')n--;
 while(*b=='/')b++;
 size_t m=strlen(b),sep=n && m && a[n-1]!='/'?1:0;
 if(n+sep+m>=cap){if(cap)out[0]=0;errno=ENAMETOOLONG;return false;}
 /* memmove permits appending in place. */
 memmove(out,a,n);if(sep)out[n++]='/';memmove(out+n,b,m+1);
 return true;
}
#endif
