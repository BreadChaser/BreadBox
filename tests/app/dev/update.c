// Actual updater, fake httpc/AM/FS. Proves preflight failures never open install.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long u64;
typedef int Result;
typedef unsigned Handle;
typedef int httpcContext;
typedef int FS_Path;
typedef struct {
	u64 titleID;
	u16 version;
} AM_TitleInfo;
#define PATH_LEN 512
#include "../../../app/ui/app_policy.h"
#define PSX3D_DIR APP_SD_ROOT
#define R_FAILED(x) ((x) < 0)
#define R_SUCCEEDED(x) ((x) >= 0)
#define HTTPC_METHOD_GET 0
#define HTTPC_KEEPALIVE_DISABLED 0
#define HTTPC_RESULTCODE_DOWNLOADPENDING -2
#define ARCHIVE_SDMC 0
#define PATH_EMPTY 0
#define PATH_ASCII 1
#define FS_OPEN_READ 1
#define MEDIATYPE_SD 1
bool dev_remote;
char dev_token[96];
const char *dev_remote_source = "breadbox";
static bool homebrew, fail_write, fail_finish;
static u64 running_id = APP_TITLE_ID, cia_id = APP_TITLE_ID;
static u16 installed_version=1027, package_version=1028;
static bool fail_title_info;
static u32 offset;
static unsigned starts, cancels, finishes, am_inits;
static u8 fixture[0x5000];
bool envIsHomebrew(void) { return homebrew; }
Result APT_GetProgramID(u64 *id)
{
	*id = running_id;
	return 0;
}
bool devnet_stopping(void) { return false; }
Result httpcInit(unsigned size)
{
	(void)size;
	offset = 0;
	return 0;
}
void httpcExit(void) {}
Result httpcOpenContext(httpcContext *c, int method, const char *url, int proxy)
{
	(void)c;
	(void)method;
	(void)url;
	(void)proxy;
	return 0;
}
void httpcCloseContext(httpcContext *c) { (void)c; }
void httpcCancelConnection(httpcContext *c) { (void)c; }
void httpcSetKeepAlive(httpcContext *c, int mode)
{
	(void)c;
	(void)mode;
}
Result httpcBeginRequest(httpcContext *c)
{
	(void)c;
	return 0;
}
Result httpcGetResponseStatusCodeTimeout(httpcContext *c, u32 *code, u64 ns)
{
	(void)c;
	(void)ns;
	*code = 200;
	return 0;
}
Result httpcGetResponseHeader(httpcContext *c, const char *name, char *out, size_t n)
{
	(void)c;
	(void)name;
	snprintf(out, n, "%zu", sizeof(fixture));
	return 0;
}
Result httpcGetDownloadSizeState(httpcContext *c, u32 *done, u32 *size)
{
	(void)c;
	if (done)
		*done = offset;
	if (size)
		*size = sizeof(fixture);
	return 0;
}
Result httpcReceiveDataTimeout(httpcContext *c, u8 *out, u32 n, u64 ns)
{
	(void)c;
	(void)ns;
	assert(offset + n <= sizeof(fixture));
	memcpy(out, fixture + offset, n);
	offset += n;
	return offset == sizeof(fixture) ? 0 : HTTPC_RESULTCODE_DOWNLOADPENDING;
}
Result amInit(void)
{
	am_inits++;
	return 0;
}
void amExit(void) {}
FS_Path fsMakePath(int type, const char *path)
{
	(void)type;
	(void)path;
	return 0;
}
Result FSUSER_OpenFileDirectly(Handle *handle, int archive, FS_Path ap, FS_Path p, int flags, int attr)
{
	(void)archive;
	(void)ap;
	(void)p;
	(void)flags;
	(void)attr;
	*handle = 1;
	return 0;
}
Result AM_GetCiaFileInfo(int media, AM_TitleInfo *info, Handle h)
{
	(void)media;
	(void)h;
	info->titleID = cia_id;
	info->version = package_version;
	return 0;
}
Result AM_GetTitleInfo(int media, u32 count, u64 *ids, AM_TitleInfo *info)
{
	assert(media == MEDIATYPE_SD && count == 1 && ids[0] == APP_TITLE_ID);
	info->titleID = ids[0];info->version = installed_version;
	return fail_title_info ? -9 : 0;
}
Result AM_StartCiaInstall(int media, Handle *handle)
{
	assert(media == MEDIATYPE_SD);
	starts++;
	*handle = 2;
	return 0;
}
Result AM_CancelCIAInstall(Handle h)
{
	assert(h == 2);
	cancels++;
	return 0;
}
Result AM_FinishCiaInstall(Handle h)
{
	assert(h == 2);
	finishes++;
	return fail_finish ? -8 : 0;
}
Result FSFILE_Close(Handle h)
{
	(void)h;
	return 0;
}
Result FSFILE_Read(Handle h, u32 *done, u64 off, void *out, u32 n)
{
	assert(h == 1);
	memcpy(out, fixture + off, n);
	*done = n;
	return 0;
}
Result FSFILE_Write(Handle h, u32 *done, u64 off, const void *data, u32 n, int flags)
{
	assert(h == 2);
	(void)flags;
	assert(!memcmp(data, fixture + off, n));
	*done = fail_write ? 0 : n;
	return fail_write ? -7 : 0;
}
#include "../../../app/ui/devupdate.c"
static void reset(void)
{
	starts = cancels = finishes = am_inits = 0;
	fail_write = fail_finish = false;
}
int main(void)
{
	mkdir("sdmc:", 0777);
	mkdir("sdmc:/3ds", 0777);
	mkdir(PSX3D_DIR, 0777);
	memset(fixture, 'Z', sizeof(fixture));
	char result[512], args[512], url[64];
	// Build a generic private-LAN fixture URL without checking in a real IP.
	snprintf(url, sizeof(url), "http://%u.%u.%u.%u/x", 10u, 0u, 0u, 1u);
	mbedtls_sha256_context hash;
	u8 digest[32];char hex[65];
	mbedtls_sha256_init(&hash);
	mbedtls_sha256_starts_ret(&hash, 0);
	mbedtls_sha256_update_ret(&hash, fixture, sizeof(fixture));
	mbedtls_sha256_finish_ret(&hash, digest);
	mbedtls_sha256_free(&hash);
	for (int i = 0; i < 32; i++) sprintf(hex+i*2,"%02x",digest[i]);
	snprintf(args,sizeof(args),"%s size=%zu sha256=%s",url,sizeof(fixture),hex);
	assert(!dev_update(args, result, sizeof(result)));
	assert(!starts && !am_inits);
	dev_remote = true;
	assert(!dev_update(args, result, sizeof(result)));
	assert(!starts && !am_inits);
	strcpy(dev_token, "test-token");
	assert(!dev_update("https://localhost/x", result, sizeof(result)));
	assert(!starts);
	assert(!dev_update("http://example.invalid/x", result, sizeof(result)));
	assert(!starts);
	char public_url[64];
	snprintf(public_url, sizeof(public_url), "http://%u.%u.%u.%u/x", 203u, 0u, 113u, 1u);
	assert(!dev_update(public_url, result, sizeof(result)));
	assert(!starts);
	assert(!dev_update("http://example.com/x", result, sizeof(result)));
	assert(!starts);
	homebrew = true;
	assert(!dev_update(args, result, sizeof(result)));
	homebrew = false;
	assert(!starts);
	snprintf(args,sizeof(args),"%s size=99",url);
	assert(!dev_update(args, result, sizeof(result)));
	assert(!starts && !am_inits);
	snprintf(args,sizeof(args),"%s size=20480 sha256=0000000000000000000000000000000000000000000000000000000000000000",url);
	assert(!dev_update(args, result, sizeof(result)));
	assert(!starts && !am_inits);
	assert(strstr(result, "SHA256 mismatch"));
	snprintf(args,sizeof(args),"%s size=%zu sha256=%s",url,sizeof(fixture),hex);
	cia_id = 99;
	assert(!dev_update(args, result, sizeof(result)));
	assert(!starts);
	cia_id = SELF_TITLE;
	reset();
	fail_write = true;
	assert(!dev_update(args, result, sizeof(result)));
	assert(starts == 1 && cancels == 1 && !finishes);
	reset();
	fail_finish = true;
	assert(!dev_update(args, result, sizeof(result)));
	assert(starts == 1 && cancels == 1 && finishes == 1);
	reset();
	assert(!dev_update(url, result, sizeof(result)));
	assert(!starts && !am_inits);
	char partial[512];
	snprintf(partial,sizeof(partial),"%s sha256=%s",url,hex);
	assert(!dev_update(partial,result,sizeof(result)));
	assert(!starts && !am_inits);
	package_version=installed_version;
	assert(!dev_update(args,result,sizeof(result)));
	assert(!starts && strstr(result,"newer"));
	package_version=installed_version-1;
	assert(!dev_update(args,result,sizeof(result)));
	assert(!starts && strstr(result,"newer"));
	package_version=installed_version+1;
	fail_title_info=true;
	assert(!dev_update(args,result,sizeof(result)));
	assert(!starts && strstr(result,"installed version"));
	fail_title_info=false;
	cia_id=0x000400000C3D3D00ULL;
	assert(!dev_update(args,result,sizeof(result)));
	assert(!starts && strstr(result,"title ID mismatch"));
	cia_id=SELF_TITLE;
	FILE *f = fopen(PSX3D_DIR "/switches.txt", "w");
	assert(f);
	fprintf(f,"remote token=secret update=%s\n",url);
	fclose(f);
	assert(dev_update(args, result, sizeof(result)));
	assert(starts == 1 && finishes == 1 && !cancels);
	assert(strstr(result, "\"relaunch\":true"));
	assert(access(CIA_TEMP, F_OK));
	char text[256] = {0};
	f = fopen(PSX3D_DIR "/switches.txt", "r");
	assert(f);
	assert(fread(text, 1, sizeof(text) - 1, f));
	fclose(f);
	assert(strstr(text, "remote") && strstr(text, "token=secret") && !strstr(text, "update="));
	puts("updater LAN/size/hash/title guards, cancellation and successful AM order: PASS");
}
