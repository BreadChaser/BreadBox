#include <ctype.h>
#include <string.h>
#include "cold_code.h"
#include "whatsnew.h"

PSX3D_COLD bool bb_whatsnew_changed(bool had_data, const char *seen, const char *version, const char *text)
{
 if(!version || !*version || !text)return false;
 while(isspace((unsigned char)*text))text++;
 if(!*text)return false;
 if(seen && *seen)return strcmp(seen,version)!=0;
 return had_data;
}
