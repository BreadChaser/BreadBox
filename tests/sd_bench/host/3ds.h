#ifndef SD_BENCH_HOST_3DS_H
#define SD_BENCH_HOST_3DS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int Handle;
typedef int Result;
typedef int FS_Archive;
typedef struct {const char *data;} FS_Path;
#define SYSCLOCK_ARM11 1000000ULL
#define R_SUCCEEDED(r) ((r)>=0)
#define R_FAILED(r) ((r)<0)
enum {ARCHIVE_SDMC,PATH_EMPTY,PATH_ASCII,FS_OPEN_WRITE,FS_OPEN_READ};
static inline FS_Path fsMakePath(int type,const char *p){(void)type;return (FS_Path){p};}
u64 svcGetSystemTick(void);
Result FSUSER_OpenArchive(FS_Archive *,int,FS_Path);
Result FSUSER_CloseArchive(FS_Archive);
Result FSUSER_CreateFile(FS_Archive,FS_Path,int,u64);
Result FSUSER_OpenFileDirectly(Handle *,int,FS_Path,FS_Path,int,int);
Result FSFILE_SetSize(Handle,u64);
Result FSFILE_Read(Handle,u32 *,u64,void *,u32);
Result FSFILE_Write(Handle,u32 *,u64,const void *,u32,u32);
Result FSFILE_Flush(Handle);
Result FSFILE_Close(Handle);
#endif
