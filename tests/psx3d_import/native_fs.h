/* Host model for the production explicit-offset FSFILE transfer branch. */
#ifndef IMPORT_NATIVE_FS_H
#define IMPORT_NATIVE_FS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef int32_t Result;
typedef unsigned Handle;
typedef struct {const void *data;} FS_Path;
#define ARCHIVE_SDMC 1
#define PATH_EMPTY 0
#define PATH_UTF16 1
#define FS_OPEN_READ 1
#define FS_OPEN_WRITE 2
#define FS_OPEN_CREATE 4
#define RD_OUT_OF_MEMORY 10
#define RS_OUTOFRESOURCE 3
#define R_FAILED(r) ((r)<0)
#define R_DESCRIPTION(r) (-(r)&255)
#define R_SUMMARY(r) ((-(r)>>8)&255)
static inline FS_Path fsMakePath(int type,const void *data){(void)type;return (FS_Path){data};}
ssize_t utf8_to_utf16(uint16_t *,const uint8_t *,size_t);
Result FSUSER_OpenFileDirectly(Handle *,int,FS_Path,FS_Path,unsigned,unsigned);
Result FSFILE_GetSize(Handle,uint64_t *);
Result FSFILE_Close(Handle);
Result FSFILE_Read(Handle,uint32_t *,uint64_t,void *,uint32_t);
Result FSFILE_Write(Handle,uint32_t *,uint64_t,const void *,uint32_t,unsigned);
Result FSFILE_Flush(Handle);
#endif
