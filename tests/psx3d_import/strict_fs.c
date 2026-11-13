/* Deliberately stricter than POSIX: model sdmc path parsing before libc can
 * normalize invalid separators. Host absolute roots remain usable. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef PSX_IMPORT_METADATA_TEST
extern void import_metadata_operation(void);
#define METADATA() import_metadata_operation()
#else
#define METADATA() ((void)0)
#endif
static int valid(const char *p)
{
 size_t n=strlen(p);const char *s=p,*colon=strchr(p,':');
 if(colon){if(colon==p || colon[1]!='/')goto bad;s=colon+2;}
 else if(*s=='/')s++;
 if(n>1023){errno=ENAMETOOLONG;return 0;}
 if(!*p || strstr(p,"//") || (*s && s[strlen(s)-1]=='/'))goto bad;
 for(const char *a=s;*a;){const char *b=strchr(a,'/');size_t len=b?(size_t)(b-a):strlen(a);
  if(!len || (len==1 && *a=='.') || (len==2 && !memcmp(a,"..",2)))goto bad;
  if(len>255){errno=ENAMETOOLONG;return 0;}
  for(size_t i=0;i<len;i++)if((unsigned char)a[i]<32 || strchr("\\:*?\"<>|",a[i]))goto bad;
  if(!b)break;a=b+1;
 }
 return 1;
bad: fprintf(stderr,"STRICT-PATH rejected: %s\n",p);errno=EINVAL;return 0;
}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *p,struct stat *s){METADATA();
#ifdef PSX_IMPORT_FAULT_TEST
 extern int import_probe_error(const char *);int fault=import_probe_error(p);if(fault){errno=fault;return -1;}
#endif
return valid(p)?__real_lstat(p,s):-1;}
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s){METADATA();return valid(p)?__real_stat(p,s):-1;}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m){METADATA();
#ifdef PSX_IMPORT_FAULT_TEST
 extern void import_opened(const char *,const char *);import_opened(p,m);
#endif
 return valid(p)?__real_fopen(p,m):NULL;}
DIR *__real_opendir(const char *);
DIR *__wrap_opendir(const char *p){METADATA();return valid(p)?__real_opendir(p):NULL;}
int __real_open(const char *,int,...);
int __wrap_open(const char *p,int f,...){mode_t m=0;if(f&O_CREAT){va_list a;va_start(a,f);m=va_arg(a,int);va_end(a);}return valid(p)?__real_open(p,f,m):-1;}
